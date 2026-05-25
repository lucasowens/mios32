// $Id$
/*
 * Emission tap + pattern-slot commit ("bounce-in-place")
 *
 * See seq_capture.h for the public contract.
 *
 * The ring is single-producer (SEQ_CORE_Tick task via SEQ_CORE_ScheduleEvent)
 * and single-consumer (commit, in user-task context). They are mutually
 * exclusive in practice because commit holds MUTEX_MIDIOUT, which gates
 * outgoing emission, and also calls MUTEX_SDCARD which prevents tick-path
 * SD work. We do not write to the ring during commit; the head index is
 * snapshotted at the start of commit and used to bound the read.
 *
 * ==========================================================================
 *
 *  Copyright (C) 2026 Lucas Owens (lowens@outcomemd.com)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 *
 * ==========================================================================
 */


/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>
#include <seq_bpm.h>
#include <string.h>

#include "tasks.h"

#include "seq_capture.h"
#include "seq_core.h"
#include "seq_cc.h"
#include "seq_par.h"
#include "seq_trg.h"
#include "seq_layer.h"
#include "seq_pattern.h"
#include "seq_file.h"
#include "seq_file_b.h"
#include "seq_ui.h"


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static seq_capture_event_t ring[SEQ_CORE_NUM_TRACKS][SEQ_CAPTURE_RING_SIZE];
static u16 ring_head[SEQ_CORE_NUM_TRACKS];        // next write slot
static u16 ring_count[SEQ_CORE_NUM_TRACKS];       // entries filled (saturates at RING_SIZE)
static u8  ring_armed[SEQ_CORE_NUM_TRACKS];


/////////////////////////////////////////////////////////////////////////////
// Init
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CAPTURE_Init(u32 mode)
{
  int t;
  for(t=0; t<SEQ_CORE_NUM_TRACKS; ++t) {
    ring_head[t] = 0;
    ring_count[t] = 0;
    // always armed: bounce should work from any UI page, on any track,
    // without requiring the user to have visited a "warm-up" page first.
    // The hot-path cost is one branch on a u8 + one packed write per emit.
    ring_armed[t] = 1;
  }
  memset(ring, 0, sizeof(ring));
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Arm / state
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CAPTURE_ArmTrack(u8 track, u8 armed)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;
  ring_armed[track] = armed ? 1 : 0;
  return 0;
}

u8 SEQ_CAPTURE_IsArmed(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return 0;
  return ring_armed[track];
}

s32 SEQ_CAPTURE_Clear(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;
  ring_head[track] = 0;
  ring_count[track] = 0;
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Hot-path tap
//
// Called once at the top of SEQ_CORE_ScheduleEvent for every emitted MIDI
// event (after all FX, before SEQ_MIDI_OUT_Send). One unaligned 8-byte write
// to a static ring; oldest event overwritten when ring wraps.
//
// We filter out non-musical events (clock/active-sense/SysEx) and also
// off-events scheduled with timestamp 0xffffffff -- those are paired with
// their on-event and a single on/off pair will write two ring entries when
// the OnEvent gets re-emitted as the live off (see seq_core.c). For length
// resolution we currently rely on the next-NoteOn-or-end-of-window heuristic
// rather than NoteOff matching.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CAPTURE_TapEvent(u8 track, mios32_midi_package_t pkg, u32 bpm_tick)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;
  if( !ring_armed[track] )
    return 0;

  // only musical events: NoteOn, NoteOff, CC, PitchBend, ProgramChange, Aftertouch
  u8 ev = pkg.event;
  if( ev < 0x8 || ev > 0xE )
    return 0;

  u16 idx = ring_head[track];
  seq_capture_event_t *e = &ring[track][idx];
  e->bpm_tick = bpm_tick;
  e->status   = pkg.evnt0;
  e->data1    = pkg.evnt1;
  e->data2    = pkg.evnt2;
  e->flags    = 0;

  idx = (idx + 1) % SEQ_CAPTURE_RING_SIZE;
  ring_head[track] = idx;
  if( ring_count[track] < SEQ_CAPTURE_RING_SIZE )
    ++ring_count[track];

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Find next free pattern slot in `group`. A slot is considered "free" if its
// stored name is all-blank (the bank file rewrites a blank name on Create).
// Caller must skip the currently playing pattern via `current_pattern`.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CAPTURE_FindNextFreeSlot(u8 group, u8 current_pattern)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return -1;

  // probe up to 8 pattern slots in the current bank
  u8 bank = seq_pattern[group].bank;
  int p;
  for(p=0; p<8; ++p) {
    if( p == (current_pattern & 0x07) )
      continue;
    char name[21];
    s32 status;
    seq_pattern_t probe;
    probe.ALL = 0;
    probe.bank = bank;
    probe.pattern = p;
    MUTEX_SDCARD_TAKE;
    status = SEQ_PATTERN_PeekName(probe, name);
    MUTEX_SDCARD_GIVE;
    if( status < 0 )
      continue;
    // empty if name is all spaces / null
    u8 has_content = 0;
    int i;
    for(i=0; i<20 && name[i] != 0; ++i) {
      if( name[i] != ' ' ) {
        has_content = 1;
        break;
      }
    }
    if( !has_content )
      return p;
  }
  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Commit: replay last `num_measures` of captured events on `src_track` into
// the destination pattern slot.
//
// Steps:
//   1. Snapshot src_track's RAM state (CC, par/trg layers, name, play_section).
//   2. Resolve the bounce range (start_tick, end_tick).
//   3. Match NoteOn -> NoteOff pairs and compute lengths.
//   4. Mutate src_track in-RAM: clear layers, sanitize generative CC, label BNC.
//   5. Walk pairs; call SEQ_LAYER_RecEvent per NoteOn with the resolved length.
//   6. PatternWrite to destination slot — reads the mutated RAM as the source.
//   7. Restore src_track from the snapshot in step 1. Source is byte-identical.
//
// The snapshot/restore avoids SD round-trips on the source — historically we
// used SEQ_PATTERN_Save + SEQ_PATTERN_Load to round-trip through SD, but a
// non-zero seq_pattern_remix_map silently skipped the source track on reload,
// and play_section (runtime state, not in the pattern file) couldn't be
// restored that way at all. The RAM snapshot handles both.
//
// Note: this routine must run in user-task context. It takes both the SD-card
// and MIDI-out mutexes; never call it from SEQ_CORE_Tick.
/////////////////////////////////////////////////////////////////////////////

// per-pair scratch table. Bounded by RING_SIZE (one NoteOn per slot worst-case).
typedef struct {
  u32 on_tick;
  u32 off_tick;       // 0xffffffff = unresolved
  u8  status;
  u8  note;
  u8  vel;
} bounce_note_t;

static bounce_note_t pair_table[SEQ_CAPTURE_RING_SIZE];

// In-RAM snapshot buffers used to preserve the source track across the bounce
// without touching SD. Sized to one track's worth of state.
static seq_cc_trk_t bounce_cc_snapshot;
static u8           bounce_par_snapshot[SEQ_PAR_MAX_BYTES];
static u8           bounce_trg_snapshot[SEQ_TRG_MAX_BYTES];
static char         bounce_name_snapshot[20];

s32 SEQ_CAPTURE_CommitToSlot(u8 src_track, u8 dst_group, seq_pattern_t dst_pattern, u8 num_measures)
{
  if( src_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_group >= SEQ_CORE_NUM_GROUPS ) return -1;
  if( num_measures == 0 || num_measures > 16 ) num_measures = 1;

  u8 src_group = src_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
  s32 status;

  // 1. Snapshot the in-RAM source state. We mutate seq_cc_trk[src_track] and
  //    the layer/trigger arrays in place (PatternWrite reads from them) and
  //    restore at the end. SD-based round-trips have two failure modes that
  //    bit us in practice: a non-zero seq_pattern_remix_map silently skips
  //    the source track on reload, and runtime fields like play_section live
  //    outside the pattern file entirely. Restoring from a RAM snapshot makes
  //    source state byte-identical after the bounce regardless.
  u8 play_section_snapshot = seq_core_trk[src_track].play_section;
  memcpy(&bounce_cc_snapshot, &seq_cc_trk[src_track], sizeof(seq_cc_trk_t));
  memcpy(bounce_par_snapshot, seq_par_layer_value[src_track], SEQ_PAR_MAX_BYTES);
  memcpy(bounce_trg_snapshot, seq_trg_layer_value[src_track], SEQ_TRG_MAX_BYTES);
  memcpy(bounce_name_snapshot, seq_pattern_name[src_group], 20);

  // 2. Resolve bounce range.
  //    Step period in BPM ticks derived from the track's clock divider,
  //    matching the formula used in SEQ_CORE_Tick (seq_core.c:663/920).
  //    t->step_length is the *current step's actual duration including
  //    groove/swing*, which is not what we want here.
  seq_cc_trk_t *tcc = &seq_cc_trk[src_track];
  u32 step_ticks = ((u32)(tcc->clkdiv.value + 1)) * (tcc->clkdiv.TRIPLETS ? 4 : 6);
  if( step_ticks == 0 ) step_ticks = 24; // fallback: 1/16th at 384 ppqn
  u32 num_steps  = (u32)tcc->length + 1;
  u32 measure_ticks = step_ticks * num_steps;

  // Anchor the bounce window to the most recent captured event rather than
  // the live BPM tick. This way a paused/stopped sequencer still bounces
  // "the last measure of what played" instead of "an empty window because
  // bpm_tick stalled."
  u32 end_tick = 0;
  u16 count = ring_count[src_track];
  if( count > 0 ) {
    u16 last_idx = (ring_head[src_track] + SEQ_CAPTURE_RING_SIZE - 1) % SEQ_CAPTURE_RING_SIZE;
    end_tick = ring[src_track][last_idx].bpm_tick + 1;
  } else {
    // empty ring: nothing to bounce
    MIOS32_MIDI_SendDebugMessage("[BOUNCE] ring is empty; nothing to commit\n");
    return -2;
  }
  u32 window     = measure_ticks * num_measures;
  u32 start_tick = (end_tick > window) ? (end_tick - window) : 0;

  // 3. Walk ring, build NoteOn/NoteOff pair table.
  //    Iterate from oldest to newest (count entries before head).
  int n_pairs = 0;
  u16 idx_start = (ring_head[src_track] + SEQ_CAPTURE_RING_SIZE - count) % SEQ_CAPTURE_RING_SIZE;
  int i;
  for(i=0; i<count; ++i) {
    seq_capture_event_t *e = &ring[src_track][(idx_start + i) % SEQ_CAPTURE_RING_SIZE];
    if( e->bpm_tick < start_tick || e->bpm_tick >= end_tick )
      continue;
    u8 evnt_type = e->status >> 4;
    u8 chn       = e->status & 0x0f;

    if( evnt_type == 0x9 && e->data2 > 0 ) {
      // NoteOn
      if( n_pairs < SEQ_CAPTURE_RING_SIZE ) {
        pair_table[n_pairs].on_tick  = e->bpm_tick;
        pair_table[n_pairs].off_tick = 0xffffffff;
        pair_table[n_pairs].status   = 0x90 | chn;
        pair_table[n_pairs].note     = e->data1;
        pair_table[n_pairs].vel      = e->data2;
        ++n_pairs;
      }
    } else if( evnt_type == 0x8 || (evnt_type == 0x9 && e->data2 == 0) ) {
      // NoteOff: match most-recent unmatched NoteOn with same note/channel
      int p;
      for(p=n_pairs-1; p>=0; --p) {
        if( pair_table[p].off_tick == 0xffffffff &&
            pair_table[p].note == e->data1 &&
            (pair_table[p].status & 0x0f) == chn ) {
          pair_table[p].off_tick = e->bpm_tick;
          break;
        }
      }
    }
    // Non-note events (CC/PB/Aftertouch) are currently dropped on commit;
    // adding parameter-layer CC recording is a Phase 2 concern.
  }

  // 4. Clear src track layers in RAM. We are about to write the dst slot;
  //    keep all other tracks intact so the dst pattern inherits them.
  memset((u8 *)&seq_par_layer_value[src_track], 0, SEQ_PAR_MAX_BYTES);
  memset((u8 *)&seq_trg_layer_value[src_track], 0, SEQ_TRG_MAX_BYTES);
  SEQ_CORE_RenderDirtySet(src_track);

  // Sanitize generative state on the in-RAM src_track config before writing
  // to dst. The dst pattern is a *frozen* capture of what was emitted; every
  // generative feature (FX, direction shaping, bus/transposer, groove,
  // global transpose, morph, per-step Probability/Delay/Roll/Nth/etc.) is
  // already baked into the captured notes — re-applying them on playback
  // would double-modulate or mute the tape. SEQ_CC_ResetGenerativeForBounce
  // is the canonical list; extend that function when new generative CCs
  // land. Source state is restored from the snapshot at step 7.
  SEQ_CC_ResetGenerativeForBounce(src_track);

  MUTEX_MIDIOUT_TAKE;

  MIOS32_MIDI_SendDebugMessage("[BOUNCE] n_pairs=%d window=[%d..%d] step_ticks=%d num_steps=%d ring_count=%d\n",
                               n_pairs, (int)start_tick, (int)end_tick,
                               (int)step_ticks, (int)num_steps, (int)count);

  // 5. Replay pairs: for each NoteOn, compute its step + length, build a
  //    layer_evnt_t, hand to SEQ_LAYER_RecEvent (which knows drum vs normal,
  //    handles link_par_layer_velocity / length and poly note placement).
  int p;
  for(p=0; p<n_pairs; ++p) {
    u32 rel = pair_table[p].on_tick - start_tick;
    u16 step = (u16)((rel / step_ticks) % num_steps);

    // Convert note duration (in BPM ticks) to the par-layer length encoding
    // (0..95, where 96 means "tied to next step"). Playback rescales via
    // (t->step_length * e->len) / 96. If the NoteOff didn't get matched,
    // default to ~74% of a step (the same value the live recorder uses).
    u32 param_len;
    if( pair_table[p].off_tick != 0xffffffff ) {
      u32 dur_ticks = pair_table[p].off_tick - pair_table[p].on_tick;
      param_len = (dur_ticks * 96) / step_ticks;
    } else {
      param_len = 71; // default staccato; matches seq_record.c
    }
    if( param_len < 1 ) param_len = 1;
    if( param_len > 95 ) param_len = 95;  // 96 would tie to next step

    seq_layer_evnt_t le;
    le.midi_package.ALL = 0;
    le.midi_package.type = 0x9;     // NoteOn type nibble (USB-style)
    le.midi_package.event = 0x9;
    le.midi_package.chn = pair_table[p].status & 0x0f;
    le.midi_package.note = pair_table[p].note;
    le.midi_package.velocity = pair_table[p].vel;
    le.midi_package.cable = src_track;  // used as tag by RecEvent
    le.len = (s16)param_len;
    le.layer_tag = 0;

    s32 rec_r = SEQ_LAYER_RecEvent(src_track, step, le);
    MIOS32_MIDI_SendDebugMessage("[BOUNCE]  p=%d step=%d note=%d vel=%d len=%d -> RecEvent=%d\n",
                                 p, step, pair_table[p].note, pair_table[p].vel,
                                 (int)param_len, (int)rec_r);
  }

  MUTEX_MIDIOUT_GIVE;

  // 6. Tag the in-RAM pattern name with a "BNC" prefix so bounced slots are
  //    visually distinguishable from hand-edited ones in the pattern picker.
  //    The name field is 20 chars wide, no zero terminator. We overwrite the
  //    first 3 chars and leave the rest of the source name visible.
  seq_pattern_name[src_group][0] = 'B';
  seq_pattern_name[src_group][1] = 'N';
  seq_pattern_name[src_group][2] = 'C';

  // 6b. Write the mutated RAM (still labelled as src_group) to the destination
  //    slot. PatternWrite uses the source_group parameter only to source the
  //    in-RAM track layers; the slot is identified by (bank, pattern).
  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_PatternWrite(seq_file_session_name, dst_pattern.bank, dst_pattern.pattern, src_group, 1);
  MUTEX_SDCARD_GIVE;

  // 7. Restore source RAM from the snapshot taken at step 1. Always run, even
  //    on write failure — the in-RAM state has to be returned to its pre-
  //    bounce value either way. SEQ_CC_LinkUpdate re-derives the
  //    link_par_layer_* fields from the restored par-layer assignments.
  memcpy(&seq_cc_trk[src_track], &bounce_cc_snapshot, sizeof(seq_cc_trk_t));
  memcpy(seq_par_layer_value[src_track], bounce_par_snapshot, SEQ_PAR_MAX_BYTES);
  memcpy(seq_trg_layer_value[src_track], bounce_trg_snapshot, SEQ_TRG_MAX_BYTES);
  SEQ_CORE_RenderDirtySet(src_track);
  memcpy(seq_pattern_name[src_group], bounce_name_snapshot, 20);
  seq_core_trk[src_track].play_section = play_section_snapshot;
  SEQ_CC_LinkUpdate(src_track);

  // ring keeps rolling; do not clear so the user can bounce again immediately.

  return status;
}
