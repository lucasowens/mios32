// $Id$
/*
 * Pattern Routines
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>

#include <string.h>

#include <seq_bpm.h>

#include "tasks.h"

#include "seq_pattern.h"
#include "seq_cc.h"
#include "seq_core.h"
#include "seq_ui.h"
#include "seq_file.h"
#include "seq_file_b.h"
#include "seq_statistics.h"
#include "seq_song.h"
#include "seq_mixer.h"
#include "seq_generator.h"
#include "seq_par.h"
#include "seq_trg.h"
#include "seq_layer.h"
#include "seq_random.h"


/////////////////////////////////////////////////////////////////////////////
// Local defines
/////////////////////////////////////////////////////////////////////////////

// set this to 1 if performance of pattern handler should be measured with a scope
// (LED toggling in APP_Background() has to be disabled!)
#define LED_PERFORMANCE_MEASURING 0


/////////////////////////////////////////////////////////////////////////////
// Global variables
/////////////////////////////////////////////////////////////////////////////

seq_pattern_t seq_pattern[SEQ_CORE_NUM_GROUPS];
seq_pattern_t seq_pattern_req[SEQ_CORE_NUM_GROUPS];
char seq_pattern_name[SEQ_CORE_NUM_GROUPS][21];

mios32_sys_time_t seq_pattern_start_time;
u8 seq_pattern_log_load_time; // can be changed in terminal with "set seq_pattern_log_load_time on"
u8 seq_pattern_mixer_num;
u16 seq_pattern_remix_map;

u8 seq_pattern_dirty;
u32 seq_pattern_writeback_count;

// Gates the writeback: bit per group, set once the group has actually loaded
// slot content this power-cycle. Boot-time init paths (track presets, CC
// defaults) run through the SEQ_CC_Set chokepoint BEFORE any session loads —
// without this gate the first session load's SEQ_PATTERN_Change would commit
// that boot debris over slots that never sounded.
static u8 pattern_loaded;

// PHRASES — occupancy of the MBSEQ_PH.V4 snapshot library: bit n set when phrase
// n has committed records. Set on capture this session, and RE-SEEDED from disk
// on session load by SEQ_PATTERN_ProbePhrasesOnLoad (cross-session probe), so a
// reloaded set lights up + recalls. `last_recalled_phrase` (-1 = none) tracks the
// session-scoped "current" phrase for the navigation-map LEDs.
static u16 phrase_present_mask;
static s8 last_recalled_phrase;

// PHRASES drift — per-group bitmask "edited since the last phrase recall/capture"
// (the clean "drifted-since" signal seq_pattern_dirty can't be: recall's own
// inversion ORs all of seq_pattern_dirty, so it always reads dirty after recall).
// Set at the SAME source-write chokepoint as seq_pattern_dirty (SEQ_PATTERN_
// DirtySetTrack) but GATED to exclude the generator's ambient auto-mutate
// (seq_generator_in_automutate) — user choice: drift = MY edits, not the living
// organism wandering. Cleared by the recall/capture acts (re-baseline to "on the
// waypoint") + session-load probe + harness reset. Drives the PHRASE-view drift
// LED. NB: deliberate ROLL/Snap/ForceMutate/Engage and group switches DO drift.
static u8 phrase_drift;

// PHRASES naming — in-session source of truth for each phrase's name (20 chars,
// space-padded + NUL). Persisted in the phrase's base (group-0) record on disk
// (free: the record already carries a name field) and RE-SEEDED on session load
// by SEQ_PATTERN_ProbePhrasesOnLoad. Blank (all-spaces) => the UI shows the slot
// number instead. Edited in place by the keypad (SEQ_PATTERN_PhraseName) and
// committed to disk by SEQ_PATTERN_PhraseNameCommit; capture also stamps it so
// disk == RAM (a never-named slot stays blank, not the inherited A-group name).
static char seq_phrase_name[SEQ_FILE_B_NUM_PHRASES][21];

// POSTURE-MORPH (Loop A+B, design §10) — per-group posture interpolation from
// the group's LIVE posture (pos 0, true pass-through) toward a target phrase's
// same-group slice (pos PHRASE_MORPH_MAX).  A is snapshotted at arm time so
// pos 0 returns to it exactly (reversible); B is read once from disk at arm
// time (no per-step SD).  The apply is DEFERRED to the per-measure boundary
// (SEQ_PATTERN_PhraseMorphTick); the encoder/GP handlers only move the position.
//
// Loop A: ext CCs 0x80..0x9f (robotize / chord-mask / GRIP) — ~260 B .bss.
// Loop B: main CC whitelist (GROOVE_VALUE, TRANSPOSE_SEMI lerp; GROOVE_STYLE,
//         TRANSPOSE_OCT reversible snap) + per-step velocity lerp + reversible
//         per-step gate crossfade + discrete per-step NOTE swap (Phase 1) for
//         non-drum tracks.  Extra ~6.5 KB .bss.  Grid and the other 3 groups
//         are untouched.  Note swap and gate share one frozen per-step threshold
//         so each step commits to B as a unit.
#if SEQ_PHRASE_MORPH
static u8 phrase_morph_target;  // armed target phrase B (0xff = disarmed)
static u8 phrase_morph_group;   // focused group, LATCHED at arm time (0..NUM_GROUPS-1)
static u8 phrase_morph_pos;     // morph position 0..PHRASE_MORPH_MAX
static u8 phrase_morph_dirty;   // pos moved since last boundary apply
// Loop A buffers
static u8 phrase_morph_a[SEQ_CORE_NUM_TRACKS_PER_GROUP][SEQ_FILE_B_TRK_EXT_CC_COUNT]; // arm-time ext CCs
static u8 phrase_morph_b[SEQ_CORE_NUM_TRACKS_PER_GROUP][SEQ_FILE_B_TRK_EXT_CC_COUNT]; // target ext CCs
// Loop B buffers
static u8 phrase_morph_main_a[SEQ_CORE_NUM_TRACKS_PER_GROUP][128]; // arm-time main CC[0..127]
static u8 phrase_morph_main_b[SEQ_CORE_NUM_TRACKS_PER_GROUP][128]; // target main CC[0..127]
static u8 phrase_morph_vel_a[SEQ_CORE_NUM_TRACKS_PER_GROUP][256];  // arm-time velocity steps
static u8 phrase_morph_vel_b[SEQ_CORE_NUM_TRACKS_PER_GROUP][256];  // target velocity steps
static u8 phrase_morph_note_a[SEQ_CORE_NUM_TRACKS_PER_GROUP][256]; // arm-time note pitches
static u8 phrase_morph_note_b[SEQ_CORE_NUM_TRACKS_PER_GROUP][256]; // target note pitches
static u8 phrase_morph_gate_a[SEQ_CORE_NUM_TRACKS_PER_GROUP][32];  // arm-time gate bits (256 steps packed)
static u8 phrase_morph_gate_b[SEQ_CORE_NUM_TRACKS_PER_GROUP][32];  // target gate bits (256 steps packed)
// per-step gate switchover threshold in [0..MAX-1], FROZEN at arm: a step that
// differs between A and B shows B when thresh < pos, else A. The random seeding
// makes each differing step B with prob pos/MAX, but the crossfade is then
// DETERMINISTIC + REVERSIBLE (pos->0 restores A bit-exactly) + STABLE at a held
// position (no per-measure creep).
static u8 phrase_morph_gate_thresh[SEQ_CORE_NUM_TRACKS_PER_GROUP][256];
static u16 phrase_morph_p_size[SEQ_CORE_NUM_TRACKS_PER_GROUP];     // LIVE par step count at arm = the index stride
static u8  phrase_morph_vel_layer[SEQ_CORE_NUM_TRACKS_PER_GROUP];  // velocity par layer index (0xff = none)
static u8  phrase_morph_note_layer[SEQ_CORE_NUM_TRACKS_PER_GROUP]; // note par layer index (0xff = none)
static u8  phrase_morph_has_steps[SEQ_CORE_NUM_TRACKS_PER_GROUP];  // 1 = step data valid for this slot
#endif

// fill a 20-char name field with spaces + NUL (blank => UI shows the number)
static void phrase_name_blank(char *p)
{
  int i;
  for(i=0; i<20; ++i)
    p[i] = ' ';
  p[20] = 0;
}

/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_Init(u32 mode)
{
  seq_pattern_start_time.seconds = 0;
  seq_pattern_mixer_num = 0;
  seq_pattern_remix_map = 0;
  seq_pattern_log_load_time = 0;
  seq_pattern_dirty = 0;
  seq_pattern_writeback_count = 0;
  pattern_loaded = 0;
  phrase_present_mask = 0;
  last_recalled_phrase = -1;
  phrase_drift = 0;
#if SEQ_PHRASE_MORPH
  phrase_morph_target = 0xff; // disarmed
  phrase_morph_pos = 0;
  phrase_morph_dirty = 0;
  {
    u8 s;
    for(s=0; s<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++s)
      phrase_morph_has_steps[s] = 0;
  }
#endif
  {
    u8 n;
    for(n=0; n<SEQ_FILE_B_NUM_PHRASES; ++n)
      phrase_name_blank(seq_phrase_name[n]);
  }

  // pre-init pattern numbers
  u8 group;
  for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group) {
    seq_pattern[group].ALL = 0;
#if 0
    seq_pattern[group].group = 2*group; // A/C/E/G
#else
    seq_pattern[group].bank = group; // each group has it's own bank
#endif
    seq_pattern_req[group].ALL = 0;

#if 0
    sprintf((char *)seq_pattern_name[group], "Pattern %c%d          ", ('A'+((pattern>>3)&7)), (pattern&7)+1);
#else
    // if pattern name only contains spaces, the UI will print 
    // the pattern number instead of an empty message
    // this ensures highest flexibility (e.g. any pattern can be copied to another slot w/o name inconsistencies)
    // -> see SEQ_LCD_PrintPatternName()
    int i;
    for(i=0; i<20; ++i)
      seq_pattern_name[group][i] = ' ';
    seq_pattern_name[group][20] = 0;
#endif

  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Returns the name of a pattern (20 characters)
/////////////////////////////////////////////////////////////////////////////
char *SEQ_PATTERN_NameGet(u8 group)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return "<invalid group>     ";
  return seq_pattern_name[group];
}


/////////////////////////////////////////////////////////////////////////////
// FEARLESS SWITCHING — the save-model inversion (design §9 2026-06-11): the
// working state always persists; protection is the explicit CHECKPOINT/REVERT
// act. Live state that diverged from the group's working slot (dirty) is
// written back to that slot before any pattern load replaces it.
/////////////////////////////////////////////////////////////////////////////

// Marks the track's group dirty. Called from the source-write chokepoints
// (SEQ_PAR_Set / SEQ_TRG_Set / SEQ_TRG_Set8 / SEQ_CC_Set — the render mirror
// never passes through them, so per-tick rendering can't false-dirty) and
// from direct-memcpy writers that bypass them (e.g. the pattern-clear trg
// memset / the paste-clear helpers in seq_ui_util.c). Loads
// re-clear at the end of SEQ_PATTERN_Load, so load paths that replay CCs
// through SEQ_CC_Set don't leave a stale flag.
// IRQ-guarded: the mask is a read-modify-write shared across tasks.
void SEQ_PATTERN_DirtySetTrack(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  MIOS32_IRQ_Disable();
  u8 group_bit = (1 << (track / SEQ_CORE_NUM_TRACKS_PER_GROUP));
  seq_pattern_dirty |= group_bit;
  // PHRASES drift: same chokepoint, but the generator's ambient auto-mutate
  // doesn't count as a deliberate edit. seq_pattern_dirty above still sets so
  // the wandered organism is written back faithfully on the next switch.
  // (CC-replay during recall/Load also passes here; the recall/capture/probe
  // tails clear phrase_drift afterward, mirroring seq_pattern_dirty's
  // over-fire-then-normalize discipline.)
  if( !seq_generator_in_automutate )
    phrase_drift |= group_bit;
  MIOS32_IRQ_Enable();
}

// Discard the group's divergence bookkeeping WITHOUT writing — for flows
// whose intent is "the slot content wins over stale live state" (e.g. the
// cross-group bounce: capture into a slot, then bring that slot up live; a
// writeback in between would clobber the fresh capture with the pre-capture
// live state).
void SEQ_PATTERN_DirtyClearGroup(u8 group)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return;
  MIOS32_IRQ_Disable();
  seq_pattern_dirty &= ~(1 << group);
  MIOS32_IRQ_Enable();
}

// Snapshot / restore a single group's phrase_drift bit — the slot-capture verbs
// in seq_core.c (CaptureToSlotTrack / CaptureSpanToSlotTrack / CopyTrackLiveToSlot)
// do a staged load-modify-save into the dst group: they read the target slot INTO
// the live dst group (CC-replay through SEQ_CC_Set raises drift for that group),
// then restore the dst group's live RAM byte-identical. They already snapshot +
// restore seq_pattern_dirty around this (a clean group must not come out flagged
// for auto-writeback); phrase_drift has to travel with it, or a clean group comes
// out flagged "deliberately edited" and the NEXT phrase recall pays a spurious
// ~290 ms drift-gated writeback (design §9 leak note). phrase_drift is static here,
// so the verbs can't do the read-modify-write inline like they do for the global
// seq_pattern_dirty — hence this pair.
u8 SEQ_PATTERN_DriftGroupGet(u8 group)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return 0;
  return (phrase_drift & (1 << group)) ? 1 : 0;
}

void SEQ_PATTERN_DriftGroupRestore(u8 group, u8 drifted)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return;
  MIOS32_IRQ_Disable();
  if( drifted )
    phrase_drift |= (1 << group);
  else
    phrase_drift &= ~(1 << group);
  MIOS32_IRQ_Enable();
}

// Write the group's live state back to its working slot if dirty. The dirty
// bit is cleared inside SEQ_PATTERN_Save (target == working slot). Safe in
// both switch contexts: task context (immediate change) and inside
// SEQ_PATTERN_Handler's SDCARD-mutex + critical section (the nested
// MUTEX_SDCARD_TAKE in Save follows the same pattern SEQ_PATTERN_Load
// already relies on there).
s32 SEQ_PATTERN_WritebackIfDirty(u8 group)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return -1;
  if( !(seq_pattern_dirty & (1 << group)) )
    return 0;
  if( !(pattern_loaded & (1 << group)) )
    return 0; // never loaded -> "dirty" is boot-init debris, not a jam

  ++seq_pattern_writeback_count;
  if( seq_pattern_log_load_time ) {
    DEBUG_MSG("[SEQ_PATTERN:%d] Writeback G%d -> %c%d", SEQ_BPM_TickGet(), group+1, 'A'+seq_pattern[group].group, seq_pattern[group].num+1);
  }
  return SEQ_PATTERN_Save(group, seq_pattern[group]);
}

// All-groups variant for the session-switch path: must run while
// seq_file_session_name still names the OUTGOING session, or the jam gets
// written into the new session's banks.
s32 SEQ_PATTERN_WritebackAllDirty(void)
{
  s32 status = 0;
  u8 group;
  for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group)
    status |= SEQ_PATTERN_WritebackIfDirty(group);
  return status;
}

// DRIFT-gated writeback — the recall-freeze cure (design §9 2026-06-22). Identical
// to WritebackIfDirty but gated on phrase_drift (MY deliberate edits) instead of
// seq_pattern_dirty (which generator auto-mutate also sets). A group carrying only
// ambient wander has phrase_drift clear, so it is NOT saved on recall — it loads the
// pristine phrase without paying a ~290 ms flash SAVE. Faithful to the capture-centric
// model: recall = select a static grab; un-captured wander is abandoned by design
// (you'd have CAPTUREd it via the ring). A group with a real edit (drift set) still
// writes back, so a live nudge is never lost. SnapshotRead's tail still ORs all groups
// into seq_pattern_dirty, so the recalled state writes back to working slots on the
// next switch regardless.
static s32 SEQ_PATTERN_WritebackIfDrifted(u8 group)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return -1;
  if( !(phrase_drift & (1 << group)) )
    return 0;
  if( !(pattern_loaded & (1 << group)) )
    return 0; // never loaded -> not a jam
  ++seq_pattern_writeback_count;
  if( seq_pattern_log_load_time ) {
    DEBUG_MSG("[SEQ_PATTERN:%d] Writeback(drift) G%d -> %c%d", SEQ_BPM_TickGet(), group+1, 'A'+seq_pattern[group].group, seq_pattern[group].num+1);
  }
  return SEQ_PATTERN_Save(group, seq_pattern[group]);
}

static s32 SEQ_PATTERN_WritebackAllDrifted(void)
{
  s32 status = 0;
  u8 group;
  for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group)
    status |= SEQ_PATTERN_WritebackIfDrifted(group);
  return status;
}


/////////////////////////////////////////////////////////////////////////////
// FEARLESS SWITCHING Stage C — CHECKPOINT / REVERT (the blessed anchor;
// design §9 2026-06-11, plan doc 2026-06-12 §1/§2). One-deep, organism-grain:
// CHECKPOINT blesses all four groups' live state (incl. generator state) into
// the internal anchor carrier MBSEQ_AN.V4; REVERT restores all four. The
// anchor is a fifth, non-navigable "bank" (SEQ_FILE_B_ANCHOR_BANK) created
// lazily on first CHECKPOINT. Every verb re-opens it against the CURRENT
// session first — the file is per-session, and a cached valid bit from a prior
// session would otherwise read the wrong session's anchor. It reuses the bank
// record serializer wholesale, so gen state rides along (V4 ext tag), and it
// never touches the four user banks' load/save loops (CHECKPOINT writes into
// MBSEQ_AN.V4, not a working slot — so a session writeback leaves it intact).
/////////////////////////////////////////////////////////////////////////////

// 1 if the current session has a blessed anchor, 0 otherwise. Re-opens (and so
// re-resolves the path for) the current session's anchor file.
s32 SEQ_PATTERN_AnchorPresent(void)
{
  s32 status;
  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_Open(seq_file_session_name, SEQ_FILE_B_ANCHOR_BANK);
  MUTEX_SDCARD_GIVE;
  return (status >= 0) ? 1 : 0;
}

// CHECKPOINT: write all four groups' live state into the anchor, lazy-creating
// the file on first use. Groups are written ascending so a freshly-created
// file (header only) extends contiguously (no seek-past-EOF gaps). Reads live
// state only — leaves seq_pattern_dirty untouched (an anchor write is not a
// working-slot save).
//
// Partial-write exposure (accepted POC cost, plan §4): a mid-loop SD failure
// leaves a partial/mixed anchor — same power-loss class as a working-slot
// save, at lower frequency. The failure is surfaced as a negative return
// (cmd_checkpoint -> ok=0); the caller must heed it before trusting a later
// REVERT. Atomic temp+rename is the fix if this ever bites in practice.
// Shared write half of CHECKPOINT and phrase-capture: snapshot all four live
// groups into a sentinel bank, lazy-creating the file on first use. `base_pattern`
// selects the destination block (anchor = 0; phrase N = 4N). Groups are written
// ASCENDING from base_pattern so a freshly-created file extends contiguously for
// the anchor (base 0); for phrases captured out of order f_lseek expands the file
// to the target offset (the written records are well-defined — only never-written
// gaps hold undefined content, which the occupancy mask keeps us from reading).
// Reads live state only — leaves seq_pattern_dirty untouched (not a working-slot
// save). Partial-write exposure (accepted POC cost): a mid-loop SD failure leaves
// a partial/mixed snapshot; surfaced as a negative return for the caller to heed.
static s32 SEQ_PATTERN_SnapshotWrite(u8 bank, u8 base_pattern)
{
  s32 status;

  MUTEX_SDCARD_TAKE;

  // lazy create: open the session's sentinel bank, create-then-open if absent
  status = SEQ_FILE_B_Open(seq_file_session_name, bank);
  if( status < 0 ) {
    if( (status=SEQ_FILE_B_Create(seq_file_session_name, bank)) >= 0 )
      status = SEQ_FILE_B_Open(seq_file_session_name, bank);
  }

  if( status >= 0 ) {
    // PHRASES cross-session probe support: capturing phrase N out of order
    // f_lseek-expands the file past lower never-captured slots, leaving them as
    // undefined gaps. Stamp an EMPTY marker into each gap slot below this capture
    // (walking down until the nearest already-present phrase — below it the file
    // is already real-or-marked) so SEQ_FILE_B_PhraseOccupancyProbe can't mistake
    // an undefined gap for a real phrase on the next session load. Ascending
    // capture hits a present phrase immediately => zero extra writes. The anchor
    // bank captures contiguously from base 0, so it never needs this.
    if( bank == SEQ_FILE_B_PHRASE_BANK && base_pattern > 0 ) {
      s8 phrase_n = base_pattern / SEQ_CORE_NUM_GROUPS;
      s8 m;
      for(m=phrase_n-1; m>=0; --m) {
        if( phrase_present_mask & (1 << m) )
          break;
        SEQ_FILE_B_PatternWriteEmpty(seq_file_session_name, bank, SEQ_CORE_NUM_GROUPS * m);
      }
    }

    // ASCENDING write order is load-bearing: the occupancy probe
    // (SEQ_FILE_B_PhraseOccupancyProbe) treats the LAST group's header as the
    // "whole block committed" witness, so group 0 must land first and the last
    // group last. Do not reorder without updating that probe.
    u8 group;
    for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group) {
      s32 err = SEQ_FILE_B_PatternWrite(seq_file_session_name, bank, base_pattern + group, group, 1);
      if( err < 0 )
        status = err;
    }
  }

  MUTEX_SDCARD_GIVE;

  return status;
}

s32 SEQ_PATTERN_Checkpoint(void)
{
  s32 status = SEQ_PATTERN_SnapshotWrite(SEQ_FILE_B_ANCHOR_BANK, 0);

  // A fresh checkpoint sets a new baseline: drop any pending UNDO/REDO. Without
  // this, an ORGANISM arm left by a prior REVERT would keep pointing at the
  // anchor slots — and CHECKPOINT just rewrote slot 0 (the REDO target), so a
  // later REDO would restore the NEW checkpoint, not the reverted state. Only
  // on a committed write (a failed checkpoint changed nothing).
  if( status >= 0 )
    SEQ_CORE_JournalInvalidate();

  if( seq_pattern_log_load_time )
    DEBUG_MSG("[SEQ_PATTERN:%d] CHECKPOINT status %d", SEQ_BPM_TickGet(), status);

  return status;
}

// REVERT: restore all four groups' live state (incl. generator state) from the
// blessed anchor — the organism comes back to the checkpoint. Refuses cleanly
// (returns the open error, no live change) when the session has no anchor yet.
//
// Restores live RAM via SEQ_FILE_B_PatternRead directly — NOT SEQ_PATTERN_Load
// — so seq_pattern[group] keeps pointing at the group's real working slot (the
// anchor is a fifth bank; repointing the live group at it would corrupt the
// working-slot identity and the next writeback would clobber the anchor). Then
// the SEQ_CORE_LoadTrackFromSlot fan generalized to all 16 tracks: forced full
// render (the RenderDirtySet armed inside PatternRead can be consumed
// window-only by a sweep-regime tick) + sustain-cancel + PC/bank send +
// bar-aligned restart. Sets every group dirty afterward (live now diverges
// from the working slot — the next switch writes the reverted state into the
// slot, the inversion working normally).
// Shared read half of REVERT and phrase-recall: restore all four groups' live
// state (incl. generator state via the V4 ext tag) from a sentinel-bank snapshot.
// `base_pattern` selects the source block (anchor = 0; phrase N = 4N). Refuses
// cleanly (no live change) when the file is absent. Restores live RAM via
// SEQ_FILE_B_PatternRead directly — NOT SEQ_PATTERN_Load — so seq_pattern[group]
// keeps pointing at the group's real working slot (the sentinel is a fifth bank;
// repointing the live group at it would corrupt the working-slot identity and the
// next writeback would clobber the snapshot). Then the SEQ_CORE_LoadTrackFromSlot
// fan generalized to all 16 tracks, and every group set dirty+loaded LAST (the
// inversion: the next switch writes the restored state into the working slots).
//
// `writeback_dirty_first`: FEARLESS "never lose work". When set (phrase recall),
// any dirty group is written back to its working slot BEFORE the snapshot
// overwrites live — so a live nudge made on a recalled phrase lands in the
// working slot (recoverable), not silently discarded when you recall the next
// phrase. The phrase file is untouched, so the phrase stays a pristine committed
// snapshot. REVERT passes 0: it deliberately discards live state back to the
// checkpoint, so preserving the pre-revert divergence would defeat the gesture.
// SnapshotRead landing flags — how the restored organism lands relative to the
// running groove. Default 0 = the immediate hard restore (REVERT / stopped recall):
// cut sustained notes + bar-aligned restart. Phrase recall WHILE PLAYING sets these
// per the RECALL_SEAMLESS option so a switch doesn't click/jump (design: recall feel).
#define SEQ_SNAPSHOT_NO_CANCEL  0x01  // don't cut sustained notes (let them ring through) -> kills the switch click
#define SEQ_SNAPSHOT_NO_RESYNC  0x02  // don't bar-align-restart -> the groove continues in phase (SEAMLESS)
#define SEQ_SNAPSHOT_GRID_REPHASE 0x04  // re-phase on the switch-quantize grid (deferred to the tick) instead of the immediate bar restart -> phrase recall lands on the selected grid

static s32 SEQ_PATTERN_SnapshotRead(u8 bank, u8 base_pattern, u8 writeback_dirty_first, u8 land_flags)
{
  s32 status;

  // refuse cleanly if the current session has no such file (no live change). This
  // also (re-)resolves the file/handle for the current session before the reads.
  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_Open(seq_file_session_name, bank);
  MUTEX_SDCARD_GIVE;
  if( status < 0 )
    return status; // SEQ_FILE_B_ERR_NO_FILE / FILE open error -> caller refuses

  // pre-generate output across the SD stall, then exclude ticks for the whole
  // live-write window (the proven group-change recipe — SDCARD must be taken
  // BEFORE entering the critical section or the take hangs).
  if( SEQ_BPM_IsRunning() ) {
    MUTEX_MIDIOUT_TAKE;
    SEQ_CORE_AddForwardDelay(seq_core_pattern_switch_margin_ms);
    MUTEX_MIDIOUT_GIVE;
  }

  // FEARLESS "never lose work" (phrase recall only): persist any live nudge into
  // its working slot before the snapshot overwrites it — mirrors the pattern-
  // switch path's WritebackIfDirty-before-Load. Inside the forward-delay window,
  // outside the SDCARD critical section (Save takes the SD mutex itself). The
  // recalled phrase still loads pristine from the file below.
  //
  // Recall-freeze cure (design §9 2026-06-22): gate on phrase_drift (deliberate
  // edits), NOT seq_pattern_dirty — so a group carrying only generator wander is
  // NOT written back (no ~290 ms SAVE), killing the up-to-1.3 s recall freeze. A
  // real edit still writes back. Un-captured wander is abandoned by design (recall =
  // a static grab; you'd have CAPTUREd wander you wanted). By-ear-gated; revert this
  // one call to WritebackAllDirty if wander must survive a recall.
  if( writeback_dirty_first )
    SEQ_PATTERN_WritebackAllDrifted();

  // Read the snapshot into live with INTERRUPTS ON (mirror SEQ_PATTERN_Load, the
  // clean pattern-change path). The 4-group SD read takes several ms; the old
  // portENTER_CRITICAL around it disabled interrupts for the whole read, so the
  // higher-priority emission task (TASK_MIDI) couldn't preempt the recall (which
  // runs in TASK_Hooks) -> the clock/emission stalled mid-bar = the audible
  // timing glitch on a live phrase switch. The SD mutex still serializes the
  // read; a tick mid-read keeps emitting the CURRENT output mirror (the new
  // mirror is built by the forced render below, only after the read completes),
  // exactly as SEQ_PATTERN_Load does for a normal pattern change.
  MUTEX_SDCARD_TAKE;
  {
    u8 group;
    for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group) {
      s32 err = SEQ_FILE_B_PatternRead(bank, base_pattern + group, group, 0);
      if( err < 0 )
        status = err;
    }
  }
  MUTEX_SDCARD_GIVE;

  // force a full quiet render for every track (mirrors the SEQ_PATTERN_Load /
  // SEQ_CORE_LoadTrackFromSlot fan, all groups at once). Sustain-cancel is the
  // immediate note-cut that clicks on a live phrase switch -> skip it for a
  // SEAMLESS/QUANTIZE recall (NO_CANCEL); REVERT/stopped keep the clean cut.
  {
    u8 track;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
      seq_render_touched_ms[track] = 0;
      seq_render_dirty[track] = 1;
      SEQ_CORE_RenderTrack(track);
      if( !(land_flags & SEQ_SNAPSHOT_NO_CANCEL) )
        SEQ_CORE_CancelSustainedNotes(track);
    }
  }

  // optionally unmute (mutes are not in the anchor; this only mirrors a normal
  // load when the option is set — typically off for deliberate mute control)
  if( seq_core_options.UNMUTE_ON_PATTERN_CHANGE ) {
    portENTER_CRITICAL();
    seq_core_trk_muted = 0;
    seq_core_trk_synched_mute = 0;
    seq_core_trk_synched_unmute = 0;
    portEXIT_CRITICAL();
  }

  if( !seq_core_options.PATTERN_CHANGE_DONT_RESET_LATCHED_PC )
    SEQ_LAYER_ResetLatchedValues();

  {
    MUTEX_MIDIOUT_TAKE;
    u8 track;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track)
      SEQ_LAYER_SendPCBankValues(track, 0, 1);
    MUTEX_MIDIOUT_GIVE;
  }

  // REVERT sets every group dirty (live != working slot now) + marks the
  // groups loaded (writeback-eligible). PatternRead's CC replay (via
  // SEQ_CC_Set) only SETS dirty bits mid-restore, so this final OR over all
  // four groups is order-insensitive — kept here for symmetry with
  // SEQ_PATTERN_Load's tail.
  MIOS32_IRQ_Disable();
  seq_pattern_dirty |= ((1 << SEQ_CORE_NUM_GROUPS) - 1);
  pattern_loaded |= ((1 << SEQ_CORE_NUM_GROUPS) - 1);
  MIOS32_IRQ_Enable();

  // every track's par-buffer was just replaced from disk -> the one-deep auto-undo
  // snapshot is stale (an UNDO would clobber the restored state). Drop it.
  SEQ_GENERATOR_UndoInvalidate();

  // bar-aligned restart of all tracks (pull precedent; also delivers RATOPC's
  // musical intent — a mid-jam restore lands on the next bar, not off-phase).
  // SEAMLESS recall skips it (NO_RESYNC) so the groove continues in phase.
  // GRID_REPHASE (SWITCH-QUANTIZE Phase 2): defer the re-phase to the next grid
  // boundary in the tick (reset_trkpos_req) instead of restarting at the next bar
  // now — the recalled phrase lands on the selected grid (1/2/4/8 bar etc.).
  if( !(land_flags & SEQ_SNAPSHOT_NO_RESYNC) ) {
    if( land_flags & SEQ_SNAPSHOT_GRID_REPHASE )
      seq_core_recall_rephase_req = 1;
    else
      SEQ_CORE_ManualSynchToMeasure(0xffff);
  }

  // a whole-organism restore (recall/revert) replaces every group's live CCs ->
  // any armed posture-morph's arm-time A is now stale. Release it (recall is "the
  // arrival" — the morph's transition is done). Covers PhraseRecall + Revert.
  SEQ_PATTERN_PhraseMorphCancel();

  return status;
}

// REVERT-undoable (Stage 2b): the live organism is stashed into a pre-revert
// block in the SAME anchor bank (slots 4..7) before REVERT overwrites live with
// the checkpoint (slots 0..3), so a mis-fired REVERT is recoverable via the
// unified UNDO net. 4 is free in the anchor bank (num_patterns=64; CHECKPOINT
// only writes base 0) and mirrors the phrase 4*n block layout. The anchor file
// always exists here — REVERT runs only when AnchorPresent — so the write
// extends slots 0..3 contiguously.
#define SEQ_PATTERN_PREREVERT_BASE 4
// The pre-revert block (PREREVERT_BASE..+NUM_GROUPS-1) MUST NOT overlap the
// checkpoint block (0..NUM_GROUPS-1) — an overlap would make the stash corrupt
// the very checkpoint it lets you fall back to. 4 == today's NUM_GROUPS; this
// guard fails the build if the group count ever grows past the gap.
#if SEQ_PATTERN_PREREVERT_BASE < SEQ_CORE_NUM_GROUPS
# error "SEQ_PATTERN_PREREVERT_BASE overlaps the checkpoint block — raise it past SEQ_CORE_NUM_GROUPS"
#endif

s32 SEQ_PATTERN_Revert(void)
{
  // Stash live -> the pre-revert block FIRST so a mis-fired REVERT is
  // recoverable via the unified UNDO net. The ~1 s write (same cost as a
  // CHECKPOINT) is acceptable for a rare panic gesture; the control surface
  // stays live across it (2026-06-23 SD-write fix).
  //
  // EXCEPT when a REVERT is already the undoable gesture (ORGANISM/UNDOABLE):
  // live is already the checkpoint (nothing new to lose), and re-stashing would
  // OVERWRITE the pre-revert jam with the checkpoint — so a double-tap REVERT
  // would destroy the very state the undo is meant to recover. Skip the write
  // and keep the existing slot-4 jam undoable (stash stays 0 = success).
  u8 jstate = 0, jscope = 0;
  SEQ_CORE_JournalInfoGet(&jstate, NULL, &jscope);
  s32 stash = ( jscope == SEQ_CORE_JRNL_ORGANISM && jstate == SEQ_CORE_JRNL_UNDOABLE )
              ? 0
              : SEQ_PATTERN_SnapshotWrite(SEQ_FILE_B_ANCHOR_BANK, SEQ_PATTERN_PREREVERT_BASE);

  s32 status = SEQ_PATTERN_SnapshotRead(SEQ_FILE_B_ANCHOR_BANK, 0, 0, 0); // REVERT = immediate hard restore

  // SnapshotRead just invalidated the journal (a whole-organism restore stales
  // any track undo). Arm the ORGANISM-scope undo whenever the pre-revert jam is
  // safely stashed — EVEN IF the revert read failed (status < 0): a torn/failed
  // revert is then still recoverable back to the clean stashed jam via UNDO.
  if( stash >= 0 )
    SEQ_CORE_JournalArmOrganism();

  if( seq_pattern_log_load_time )
    DEBUG_MSG("[SEQ_PATTERN:%d] REVERT status %d stash %d", SEQ_BPM_TickGet(), status, stash);

  return status;
}

// UNDO of REVERT (ORGANISM scope): restore the pre-revert jam stashed above. The
// SnapshotRead invalidates the journal as it lands; SEQ_CORE_JournalUndo re-arms
// it REDOABLE on return.
s32 SEQ_PATTERN_RevertUndoRead(void)
{
  return SEQ_PATTERN_SnapshotRead(SEQ_FILE_B_ANCHOR_BANK, SEQ_PATTERN_PREREVERT_BASE, 0, 0);
}

// REDO of REVERT (ORGANISM scope): re-restore the checkpoint (slot 0).
s32 SEQ_PATTERN_RevertRedoRead(void)
{
  return SEQ_PATTERN_SnapshotRead(SEQ_FILE_B_ANCHOR_BANK, 0, 0, 0);
}


/////////////////////////////////////////////////////////////////////////////
// PHRASES — the snapshot library ("a set is a path"). A phrase is a whole-
// organism snapshot, capture/recall generalizing CHECKPOINT/REVERT from the one
// anchor to SEQ_FILE_B_NUM_PHRASES named slots in the MBSEQ_PH.V4 sentinel bank
// (phrase N -> patterns 4N..4N+3). Faithful (par+trg+CC+generators ride the V4
// ext tag), drift-free, and outside every for(bank<NUM_BANKS) loop.
//
// Occupancy is a 16-bit RAM mask: set on capture, and re-seeded from disk on
// session load (SEQ_PATTERN_ProbePhrasesOnLoad probes the MBSEQ_PH.V4 base
// headers — captured slots carry num_tracks>0, gaps carry an EMPTY marker laid
// down at capture time). Recall refuses an un-captured phrase before touching SD.
// The phrase DATA itself persists faithfully in the file regardless of the mask.
/////////////////////////////////////////////////////////////////////////////

// 1 if phrase n has been captured this session, 0 otherwise.
s32 SEQ_PATTERN_PhrasePresent(u8 n)
{
  if( n >= SEQ_FILE_B_NUM_PHRASES )
    return 0;
  return (phrase_present_mask & (1 << n)) ? 1 : 0;
}

// session-scoped occupancy as a 16-bit mask (bit n = phrase n captured this
// session) — for the Stage B PHRASE-view navigation-map LEDs (one read instead
// of 16 PhrasePresent calls in the LED path).
u16 SEQ_PATTERN_PhrasePresentMask(void)
{
  return phrase_present_mask;
}

// last phrase recalled this session (-1 = none) — for the Stage B "current" LED.
s32 SEQ_PATTERN_PhraseLastRecalled(void)
{
  return last_recalled_phrase;
}

// 1 if the live organism has been deliberately edited since the last phrase
// recall/capture (any group), 0 otherwise — drives the PHRASE-view drift LED.
// Excludes the generator's ambient auto-mutate (see phrase_drift / the gate in
// SEQ_PATTERN_DirtySetTrack); deliberate ROLL/Snap/ForceMutate/Engage, hands-on
// par/trg/CC edits, and group switches all count.
s32 SEQ_PATTERN_PhraseDrifted(void)
{
  return phrase_drift ? 1 : 0;
}

// The raw per-group drift mask (bit n = group n deliberately edited since the
// last recall/capture). The whole-mask PhraseDrifted above drives the LED; this
// per-group view lets the HIL pin a SPECIFIC group clean (e.g. a slot-capture
// into group 1 must not flag group 1 — the §9 drift-leak regression).
u8 SEQ_PATTERN_PhraseDriftMask(void)
{
  return phrase_drift;
}

// Pointer to phrase n's in-RAM name buffer (20 chars space-padded + NUL) — for
// the keypad editor (edit in place) and the UI to display. A blank (all-spaces)
// name means "un-named": the UI shows the slot number instead. NULL if n invalid.
char *SEQ_PATTERN_PhraseName(u8 n)
{
  if( n >= SEQ_FILE_B_NUM_PHRASES )
    return NULL;
  return seq_phrase_name[n];
}

// Persist phrase n's current RAM name into its base (group-0) record on disk
// (rename-without-recapture). Refuses an un-captured slot (no record to write
// into). Returns >= 0 on success, < 0 on error / refuse.
s32 SEQ_PATTERN_PhraseNameCommit(u8 n)
{
  if( n >= SEQ_FILE_B_NUM_PHRASES )
    return -1;
  if( !(phrase_present_mask & (1 << n)) )
    return SEQ_FILE_B_ERR_NO_FILE; // nothing captured here yet

  MUTEX_SDCARD_TAKE;
  s32 status = SEQ_FILE_B_PhraseWriteName(seq_file_session_name, n, seq_phrase_name[n]);
  MUTEX_SDCARD_GIVE;
  return status;
}

// clear session-scoped phrase occupancy (called on harness reset; session load
// uses SEQ_PATTERN_ProbePhrasesOnLoad below to re-seed from disk instead).
void SEQ_PATTERN_PhraseResetState(void)
{
  phrase_present_mask = 0;
  last_recalled_phrase = -1;
  phrase_drift = 0;
#if SEQ_PHRASE_MORPH
  phrase_morph_target = 0xff; // disarmed
  phrase_morph_pos = 0;
  phrase_morph_dirty = 0;
#endif
  {
    u8 n;
    for(n=0; n<SEQ_FILE_B_NUM_PHRASES; ++n)
      phrase_name_blank(seq_phrase_name[n]);
  }
}

// CROSS-SESSION PROBE (called on session load): re-seed the occupancy mask from
// the session's MBSEQ_PH.V4 on disk, so a reloaded set lights up its captured
// phrases and they recall again. The phrase DATA always persisted faithfully;
// this restores the knowledge of WHICH slots are real (lost when the mask was
// RAM-only). `last_recalled_phrase` stays -1 — the "current" phrase is genuinely
// session-scoped, so we don't light a red current-row LED for a phrase nobody
// recalled this session. Falls back to all-empty on any probe error.
void SEQ_PATTERN_ProbePhrasesOnLoad(void)
{
  // the whole session just changed under us -> any armed morph holds the OLD
  // session's A/B/group; release it so a stray nudge can't clobber the new set.
  // (The single chokepoint every SEQ_FILE_LoadAllFiles path runs.)
  SEQ_PATTERN_PhraseMorphCancel();

  last_recalled_phrase = -1;
  // freshly loaded session: nothing recalled yet, so nothing has drifted. (Runs
  // AFTER SEQ_FILE_B_LoadAllBanks in SEQ_FILE_LoadAllFiles, so it also wipes any
  // drift the per-group load's CC-replay raised.)
  phrase_drift = 0;

  // pre-blank all names; the probe fills only the occupied slots' names, so a
  // never-captured (or unreached) slot stays blank => UI shows the number.
  {
    u8 n;
    for(n=0; n<SEQ_FILE_B_NUM_PHRASES; ++n)
      phrase_name_blank(seq_phrase_name[n]);
  }

  MUTEX_SDCARD_TAKE;
  s32 mask = SEQ_FILE_B_PhraseOccupancyProbe(seq_file_session_name, seq_phrase_name);
  MUTEX_SDCARD_GIVE;

  phrase_present_mask = (mask >= 0) ? (u16)mask : 0;

  if( seq_pattern_log_load_time )
    DEBUG_MSG("[SEQ_PATTERN] PHRASE PROBE mask 0x%04x", phrase_present_mask);
}

// CAPTURE: snapshot the live organism into phrase n's four group-records.
s32 SEQ_PATTERN_PhraseCapture(u8 n)
{
  if( n >= SEQ_FILE_B_NUM_PHRASES )
    return -1;

  s32 status = SEQ_PATTERN_SnapshotWrite(SEQ_FILE_B_PHRASE_BANK, 4 * n);

  if( status >= 0 ) {
    // Naming is opt-in now (2026-06-22): skip the extra name-write here — SnapshotWrite
    // (via PatternWrite) already stamped group-0's working-slot name into the base
    // record, so the slot shows that (or its number); a deliberate rename can rewrite
    // it later. Saves one SD write per capture.
    phrase_present_mask |= (1 << n);
    last_recalled_phrase = n; // you just committed here -> this is "where you are"
    phrase_drift = 0;         // this IS now the committed reference — no drift since
  }

  if( seq_pattern_log_load_time )
    DEBUG_MSG("[SEQ_PATTERN:%d] PHRASE CAPTURE %d status %d", SEQ_BPM_TickGet(), n, status);

  return status;
}

// RECALL: restore the live organism from phrase n (refuses an un-captured phrase).
s32 SEQ_PATTERN_PhraseRecall(u8 n)
{
  if( n >= SEQ_FILE_B_NUM_PHRASES )
    return -1;

  // refuse before any SD op if this phrase was never captured this session
  if( !(phrase_present_mask & (1 << n)) )
    return SEQ_FILE_B_ERR_NO_FILE;

  // Recall feel (design: how a phrase switch lands). STOPPED -> immediate hard
  // restore (land=0, unchanged). PLAYING -> never cut notes (kills the click);
  // SEAMLESS also skips the bar-realign so the groove continues in phase, while
  // QUANTIZE (default) keeps the bar-aligned restart (clean downbeat). REVERT
  // is the undo and stays immediate (land=0) regardless.
  u8 land = 0;
  if( SEQ_BPM_IsRunning() ) {
    land = SEQ_SNAPSHOT_NO_CANCEL;
    if( seq_core_options.RECALL_SEAMLESS )
      land |= SEQ_SNAPSHOT_NO_RESYNC;
    else if( SEQ_CORE_SwitchQuantize16ths() > 0 )
      land |= SEQ_SNAPSHOT_GRID_REPHASE; // QUANTIZE + a grid set -> land the recall on the grid, not the next bar
  }

  s32 status = SEQ_PATTERN_SnapshotRead(SEQ_FILE_B_PHRASE_BANK, 4 * n, 1, land);

  if( status >= 0 ) {
    last_recalled_phrase = n;
    // Cleared LAST, after SnapshotRead's CC-replay has finished tripping it: the
    // organism now IS phrase n, so we're on the waypoint (no drift). A later
    // edit/group-switch/ROLL re-sets it; ambient generator wandering does not.
    phrase_drift = 0;
  }

  if( seq_pattern_log_load_time )
    DEBUG_MSG("[SEQ_PATTERN:%d] PHRASE RECALL %d status %d", SEQ_BPM_TickGet(), n, status);

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// POSTURE-MORPH (Loop A+B) — per-group posture interpolation. See design §10.
//
// Loop A: live = A + pos/MAX*(B-A) per ext CC 0x80..0x9f.
// Loop B: same lerp for main CC whitelist (groove_value, transpose_semi);
//         reversible discrete snap for groove_style, transpose_oct (B at MAX,
//         A below MAX); per-step velocity lerp; reversible per-step gate
//         crossfade via frozen thresholds (non-drum only).
//
// EVERY dimension is reversible: A is snapshotted at arm time and re-derived
// each apply, so pulling pos back to 0 returns to A exactly.  B is read from
// disk once at arm time.  The apply is per-measure; only the focused group's 4
// tracks are touched.
/////////////////////////////////////////////////////////////////////////////

#if SEQ_PHRASE_MORPH
// main CC whitelist: lerped CCs first, then snap-at-MAX CCs
#define MORPH_CC_GROOVE_VALUE   SEQ_CC_GROOVE_VALUE   // 0x52 lerp
#define MORPH_CC_TRANSPOSE_SEMI SEQ_CC_TRANSPOSE_SEMI // 0x50 lerp
#define MORPH_CC_GROOVE_STYLE   SEQ_CC_GROOVE_STYLE   // 0x53 snap
#define MORPH_CC_TRANSPOSE_OCT  SEQ_CC_TRANSPOSE_OCT  // 0x51 snap

static void phrase_morph_apply(void)
{
  u8 slot;
  // proximity to the morph MIDPOINT: 0 at either end, MAX/2 at the center. Drives the
  // notes/gates "flip then unflip" tent (by-ear 2026-06-22): a differing step shows B
  // when its frozen random thresh (uniform 0..MAX-1) is < proximity, so ~50% of the
  // differing steps are on B at the center and ALL re-cohere to A at both ends (pos 0
  // and pos MAX). The lerped dims (vel / semitone / groove-amt / ext-CCs) still ride
  // phrase_morph_pos straight A->B; only the discrete note/gate swaps use the tent.
  s32 morph_prox;
  {
    s32 d = (s32)phrase_morph_pos - PHRASE_MORPH_MAX/2;
    if( d < 0 ) d = -d;
    morph_prox = PHRASE_MORPH_MAX/2 - d;
  }
  for(slot=0; slot<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++slot) {
    u8 track = phrase_morph_group * SEQ_CORE_NUM_TRACKS_PER_GROUP + slot;
    u8 changed = 0;
    u8 i;

    // ---- Loop A: ext CCs 0x80..0x9f ----------------------------------------
    for(i=0; i<SEQ_FILE_B_TRK_EXT_CC_COUNT; ++i) {
      s32 a = phrase_morph_a[slot][i];
      s32 b = phrase_morph_b[slot][i];
      s32 lerped = a + (b - a) * (s32)phrase_morph_pos / PHRASE_MORPH_MAX;
      if( lerped < 0 ) lerped = 0;
      if( lerped > 127 ) lerped = 127;
      // Only write if changed; SEQ_CC_Set>=0 guard skips unimplemented CCs.
      if( (u8)lerped != (u8)SEQ_CC_Get(track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i) &&
          SEQ_CC_Set(track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i, (u8)lerped) >= 0 )
        changed = 1;
    }

    // ---- Loop B: main CC whitelist ------------------------------------------
    {
      // lerped CCs: groove_value, transpose_semi
      static const u8 lerp_ccs[] = { MORPH_CC_GROOVE_VALUE, MORPH_CC_TRANSPOSE_SEMI };
      u8 ci;
      for(ci=0; ci<2; ++ci) {
	u8 cc = lerp_ccs[ci];
	s32 a = phrase_morph_main_a[slot][cc];
	s32 b = phrase_morph_main_b[slot][cc];
	s32 lerped = a + (b - a) * (s32)phrase_morph_pos / PHRASE_MORPH_MAX;
	if( lerped < 0 ) lerped = 0;
	if( lerped > 127 ) lerped = 127;
	if( (u8)lerped != (u8)SEQ_CC_Get(track, cc) &&
	    SEQ_CC_Set(track, cc, (u8)lerped) >= 0 )
	  changed = 1;
      }
      // snap CCs: groove_style, transpose_oct (discrete — B at/above the MIDPOINT,
      // A below it; changed 2026-06-22 from full-throw to >= MAX/2 per by-ear request,
      // so the bottom half of the throw is A and the top half is B). Reversible:
      // pulling the dial back below the midpoint restores the arm-time A. (A lerp
      // would yield musically meaningless intermediate enum/octave values, hence the snap.)
      {
	static const u8 snap_ccs[] = { MORPH_CC_GROOVE_STYLE, MORPH_CC_TRANSPOSE_OCT };
	for(ci=0; ci<2; ++ci) {
	  u8 cc = snap_ccs[ci];
	  u8 want = (phrase_morph_pos >= PHRASE_MORPH_MAX/2)  // midpoint flip (was full-throw), by-ear 2026-06-22
	            ? phrase_morph_main_b[slot][cc] : phrase_morph_main_a[slot][cc];
	  if( want != (u8)SEQ_CC_Get(track, cc) &&
	      SEQ_CC_Set(track, cc, want) >= 0 )
	    changed = 1;
	}
      }
    }

    // ---- Loop B: per-step velocity lerp (non-drum, has_steps) ---------------
    // p_size is the LIVE par step count (the index stride), so vl*p_size+step
    // matches SEQ_PAR_Get's instrument-0 layout; the idx guard is belt-and-braces
    // against a geometry mismatch (phrase captured at a different track length).
    if( phrase_morph_has_steps[slot] &&
        SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE) != SEQ_EVENT_MODE_Drum ) {
      u16 p_size = phrase_morph_p_size[slot];
      u8  vl     = phrase_morph_vel_layer[slot];
      u16 step;
      for(step=0; step<p_size; ++step) {
	u16 idx = (u16)vl * p_size + step;
	if( idx >= SEQ_PAR_MAX_BYTES )
	  break;
	s32 a = phrase_morph_vel_a[slot][step];
	s32 b = phrase_morph_vel_b[slot][step];
	s32 lerped = a + (b - a) * (s32)phrase_morph_pos / PHRASE_MORPH_MAX;
	if( lerped < 0 ) lerped = 0;
	if( lerped > 127 ) lerped = 127;
	if( seq_par_layer_value[track][idx] != (u8)lerped ) {
	  seq_par_layer_value[track][idx] = (u8)lerped;
	  changed = 1;
	}
      }
      if( changed )
        SEQ_PATTERN_DirtySetTrack(track);
    }

    // ---- Loop B: reversible per-step gate crossfade (non-drum, has_steps) ----
    // Each step where A!=B carries a switchover threshold frozen at arm.  TENT
    // (by-ear 2026-06-22): it shows B when thresh < morph_prox (proximity to the
    // midpoint), so differing gates "flip then unflip" — ~50% on B at the center,
    // ALL back to A at both ends.  Deterministic + reversible (pos->0 OR pos->MAX
    // both restore A) + stable at a held pos (no per-measure creep).  A==B steps
    // never move.
    if( phrase_morph_has_steps[slot] &&
        SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE) != SEQ_EVENT_MODE_Drum ) {
      u16 p_size = phrase_morph_p_size[slot];
      u16 step;
      for(step=0; step<p_size && (step/8) < 32; ++step) {
	u8 a_gate = (phrase_morph_gate_a[slot][step/8] >> (step%8)) & 1;
	u8 b_gate = (phrase_morph_gate_b[slot][step/8] >> (step%8)) & 1;
	if( a_gate != b_gate ) {
	  u8 want = (phrase_morph_gate_thresh[slot][step] < morph_prox) ? b_gate : a_gate;
	  u8 cur  = (seq_trg_layer_value[track][step/8] >> (step%8)) & 1;
	  if( want != cur ) {
	    if( want )
	      seq_trg_layer_value[track][step/8] |=  (1 << (step%8));
	    else
	      seq_trg_layer_value[track][step/8] &= ~(1 << (step%8));
	    SEQ_PATTERN_DirtySetTrack(track);
	    changed = 1;
	  }
	}
      }
    }

    // ---- Loop B: discrete per-step NOTE swap (non-drum, note layer present) --
    // Shares the gate's frozen per-step threshold + the TENT (morph_prox): a step
    // keeps A's pitch unless thresh < proximity-to-midpoint, then B's pitch — a
    // discrete swap (no off-scale interpolation) that "flips then unflips" (~50% on
    // B at the center, all back to A at both ends), reversible, stable.
    if( phrase_morph_has_steps[slot] &&
        phrase_morph_note_layer[slot] != 0xff &&
        SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE) != SEQ_EVENT_MODE_Drum ) {
      u16 p_size = phrase_morph_p_size[slot];
      u8  nl     = phrase_morph_note_layer[slot];
      u16 step;
      for(step=0; step<p_size; ++step) {
	u16 idx = (u16)nl * p_size + step;
	if( idx >= SEQ_PAR_MAX_BYTES )
	  break;
	u8 want = (phrase_morph_gate_thresh[slot][step] < morph_prox)
	          ? phrase_morph_note_b[slot][step] : phrase_morph_note_a[slot][step];
	if( seq_par_layer_value[track][idx] != want ) {
	  seq_par_layer_value[track][idx] = want;
	  changed = 1;
	}
      }
      if( changed )
        SEQ_PATTERN_DirtySetTrack(track);
    }

    // forced full quiet render: zero touched_ms (kill sweep -> whole-buffer
    // refresh), arm dirty, render now.  The same idiom as SEQ_PATTERN_SnapshotRead.
    if( changed ) {
      seq_render_touched_ms[track] = 0;
      seq_render_dirty[track] = 1;
      SEQ_CORE_RenderTrack(track);
    }
  }
  phrase_morph_dirty = 0;
}

// ARM target B = phrase n, focused group latched = ui_selected_group. Snapshots
// A from live and reads B's group-slice from disk (once).  Refuses if the ext-CC
// block (Loop A gate) is missing.  Loop B data (main CCs + vel + gate) is
// best-effort: failures disable step morph for that slot without refusing the arm.
// pos resets to 0 (pass-through) — arming alone changes nothing audible.
s32 SEQ_PATTERN_PhraseMorphArm(u8 n)
{
  if( n >= SEQ_FILE_B_NUM_PHRASES )
    return -1;

  // refuse before any SD op if this phrase was never captured this session
  if( !(phrase_present_mask & (1 << n)) )
    return SEQ_FILE_B_ERR_NO_FILE;

  u8 group = ui_selected_group;
  if( group >= SEQ_CORE_NUM_GROUPS )
    group = 0;

  // DISARM for the duration of the SD reads: phrase_morph_b is overwritten in
  // place across 4 blocking reads, and the per-measure PhraseMorphTick runs in a
  // higher-priority task. Leaving the morph armed here would let a boundary mid-
  // read lerp toward a half-overwritten B (a measure of garbage CCs). target ==
  // 0xff makes the tick a clean no-op until the buffers are fully populated and
  // re-armed below. A read failure simply leaves it disarmed (the refuse path).
  phrase_morph_target = 0xff;
  phrase_morph_dirty = 0;

  // --- Loop A: read B's ext CCs (mandatory — refuses the whole arm on failure) ---
  MUTEX_SDCARD_TAKE;
  s32 status = SEQ_FILE_B_Open(seq_file_session_name, SEQ_FILE_B_PHRASE_BANK);
  if( status >= 0 ) {
    u8 slot;
    for(slot=0; slot<SEQ_CORE_NUM_TRACKS_PER_GROUP && status >= 0; ++slot)
      status = SEQ_FILE_B_PhraseReadCCs(SEQ_FILE_B_PHRASE_BANK,
                                        SEQ_CORE_NUM_GROUPS * n + group, slot,
                                        phrase_morph_b[slot]);
  }
  MUTEX_SDCARD_GIVE;
  if( status < 0 )
    return status; // refuse — do NOT arm, no live change

  // --- Loop B: read B's main CCs + velocity + gate (best-effort; no arm refusal) ---
  // Determine velocity layer index from the LIVE track geometry before the SD read.
  // Phrases are captured from the same organism so their geometry should match.
  {
    u8 slot;
    for(slot=0; slot<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++slot) {
      u8 track = group * SEQ_CORE_NUM_TRACKS_PER_GROUP + slot;
      // find note + velocity par layers (first layer of each type)
      u8 vl = 0xff, nl = 0xff;
      {
	s32 nlayers = SEQ_PAR_NumLayersGet(track);
	u8 li;
	for(li=0; li<(u8)nlayers; ++li) {
	  seq_par_layer_type_t t = SEQ_PAR_AssignmentGet(track, li);
	  if( t == SEQ_PAR_Type_Velocity && vl == 0xff ) vl = li;
	  if( t == SEQ_PAR_Type_Note     && nl == 0xff ) nl = li;
	}
      }
      phrase_morph_vel_layer[slot]  = vl;
      phrase_morph_note_layer[slot] = nl;
    }
  }
  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_Open(seq_file_session_name, SEQ_FILE_B_PHRASE_BANK);
  if( status >= 0 ) {
    u8 slot;
    for(slot=0; slot<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++slot) {
      u8 track = group * SEQ_CORE_NUM_TRACKS_PER_GROUP + slot;
      u8 vl = phrase_morph_vel_layer[slot];
      u8 nl = phrase_morph_note_layer[slot];
      s32 rc = SEQ_FILE_B_PhraseReadTrackData(
	SEQ_FILE_B_PHRASE_BANK,
	SEQ_CORE_NUM_GROUPS * n + group, slot,
	phrase_morph_main_b[slot],
	(vl < 0xff) ? vl : 0, phrase_morph_vel_b[slot],  (vl < 0xff) ? 256 : 0,
	(nl < 0xff) ? nl : 0, phrase_morph_note_b[slot], (nl < 0xff) ? 256 : 0,
	phrase_morph_gate_b[slot], 32,
	NULL, NULL);
      // Stride by the LIVE track geometry (SEQ_PAR_NumStepsGet), NOT the phrase's
      // on-disk p_size: the index vl*p_size+step must match the live
      // seq_par_layer_value layout, and live num_p_steps*num_p_layers <=
      // SEQ_PAR_MAX_BYTES by construction so the write can't overrun.  (vel_b is
      // located on disk by the reader using the phrase's own geometry.)
      if( rc >= 0 && vl < 0xff ) {
	s32 live_steps = SEQ_PAR_NumStepsGet(track);
	if( live_steps < 0 ) live_steps = 0;
	if( live_steps > 256 ) live_steps = 256;
	phrase_morph_p_size[slot]    = (u16)live_steps;
	phrase_morph_has_steps[slot] = (live_steps > 0) ? 1 : 0;
      } else {
	phrase_morph_has_steps[slot] = 0;
      }
    }
  } else {
    u8 slot;
    for(slot=0; slot<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++slot)
      phrase_morph_has_steps[slot] = 0;
  }
  MUTEX_SDCARD_GIVE;

  // --- Snapshot A = the focused group's live posture ---
  {
    u8 slot;
    for(slot=0; slot<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++slot) {
      u8 track = group * SEQ_CORE_NUM_TRACKS_PER_GROUP + slot;
      u8 i;
      // Loop A: ext CCs 0x80..0x9f
      for(i=0; i<SEQ_FILE_B_TRK_EXT_CC_COUNT; ++i)
        phrase_morph_a[slot][i] = (u8)SEQ_CC_Get(track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i);
      // Loop B: main CC[0..127] (all 128 — only whitelisted ones used at apply)
      for(i=0; i<128; ++i)
        phrase_morph_main_a[slot][i] = (u8)SEQ_CC_Get(track, i);
      // Loop B: snapshot velocity steps + gate bits from the live source, and
      // freeze a per-step random gate threshold (the reversible gate crossfade).
      if( phrase_morph_has_steps[slot] ) {
	u16 p_size = phrase_morph_p_size[slot];
	u8  vl     = phrase_morph_vel_layer[slot];
	u16 step;
	for(step=0; step<p_size; ++step) {
	  u16 idx = (u16)vl * p_size + step;
	  phrase_morph_vel_a[slot][step] =
	    (idx < SEQ_PAR_MAX_BYTES) ? seq_par_layer_value[track][idx] : 64;
	}
	// note A (only if the track has a note layer)
	if( phrase_morph_note_layer[slot] < 0xff ) {
	  u8 nl = phrase_morph_note_layer[slot];
	  for(step=0; step<p_size; ++step) {
	    u16 idx = (u16)nl * p_size + step;
	    phrase_morph_note_a[slot][step] =
	      (idx < SEQ_PAR_MAX_BYTES) ? seq_par_layer_value[track][idx] : 0;
	  }
	}
	// gate A = the live trg layer-0 bytes (non-drum: par steps == trg steps)
	{
	  u16 nbytes = (p_size + 7) / 8;
	  u16 bi;
	  if( nbytes > 32 ) nbytes = 32;
	  for(bi=0; bi<nbytes; ++bi)
	    phrase_morph_gate_a[slot][bi] = seq_trg_layer_value[track][bi];
	}
	// freeze the per-step switchover thresholds ONCE at arm so the gate
	// crossfade is deterministic / reversible / stable thereafter.
	for(step=0; step<p_size; ++step)
	  phrase_morph_gate_thresh[slot][step] = (u8)SEQ_RANDOM_Gen_Range(0, PHRASE_MORPH_MAX - 1);
      }
    }
  }

  phrase_morph_target = n;
  phrase_morph_group = group;
  phrase_morph_pos = 0;
  phrase_morph_dirty = 0; // pos 0 == live A already -> nothing to apply

  if( seq_pattern_log_load_time )
    DEBUG_MSG("[SEQ_PATTERN:%d] PHRASE MORPH ARM %d group %d", SEQ_BPM_TickGet(), n, group);

  return 0;
}

// SET morph position 0..PHRASE_MORPH_MAX. Defers the apply to the next measure
// boundary while running (per-measure granularity); applies inline when stopped
// (mirrors RESOLVE's snap-now-when-stopped, so auditioning works). No-op if the
// value is unchanged (avoids a wasted per-measure re-render while held).
s32 SEQ_PATTERN_PhraseMorphSet(u8 v)
{
  if( phrase_morph_target == 0xff )
    return -1; // disarmed

  if( v > PHRASE_MORPH_MAX )
    v = PHRASE_MORPH_MAX;

  if( v == phrase_morph_pos )
    return 0; // unchanged

  phrase_morph_pos = v;
  phrase_morph_dirty = 1;

  if( !SEQ_BPM_IsRunning() )
    phrase_morph_apply(); // snap now when stopped (clears dirty)

  return 0;
}

// PER-MEASURE driver — called from the ref_step==0 boundary. Applies a pending
// position change (lerp + re-render the focused group) once per measure.
s32 SEQ_PATTERN_PhraseMorphTick(void)
{
  if( phrase_morph_target == 0xff || !phrase_morph_dirty )
    return 0;

  phrase_morph_apply();
  return 1;
}

// DISARM. Does NOT revert live CCs — the morph leaves you wherever you stopped
// (FEARLESS leave-as-live; UNDO is the safety net). The next switch/recall will
// writeback whatever is live.
void SEQ_PATTERN_PhraseMorphCancel(void)
{
  phrase_morph_target = 0xff;
  phrase_morph_pos = 0;
  phrase_morph_dirty = 0;
}

// Release an armed morph when its focused group's live posture was replaced out-
// of-band (pattern switch / track pull): the arm-time A is now stale, so pos 0
// would no longer be the live state and a further nudge would lerp off the old A.
// Recall/revert/session-load replace ALL groups -> they call Cancel directly.
void SEQ_PATTERN_PhraseMorphInvalidateGroup(u8 group)
{
  if( phrase_morph_target != 0xff && group == phrase_morph_group )
    SEQ_PATTERN_PhraseMorphCancel();
}

// armed target phrase n, or -1 if disarmed.
s32 SEQ_PATTERN_PhraseMorphTarget(void)
{
  return (phrase_morph_target == 0xff) ? -1 : (s32)phrase_morph_target;
}

// current morph position 0..PHRASE_MORPH_MAX.
u8 SEQ_PATTERN_PhraseMorphValue(void)
{
  return phrase_morph_pos;
}

#else // !SEQ_PHRASE_MORPH

// Phrase-posture morph compiled out (make PHRASE_MORPH=0) — stubs so the call sites
// (seq_ui.c gestures, seq_core.c tick/invalidate, seq_testctrl.c 0x4f) link with no
// #if, while the ~7.7 KB of arm/target buffers above are reclaimed. Target() < 0 makes
// every UI morph intercept (GP position bar, datawheel fine-throw, LED thermometer)
// fall through; Arm() refuses so SELECT+tap on a waypoint just reports it can't morph.
s32  SEQ_PATTERN_PhraseMorphArm(u8 n)              { (void)n; return -1; }
s32  SEQ_PATTERN_PhraseMorphSet(u8 v)              { (void)v; return -1; }
s32  SEQ_PATTERN_PhraseMorphTick(void)             { return 0; }
void SEQ_PATTERN_PhraseMorphCancel(void)           { }
void SEQ_PATTERN_PhraseMorphInvalidateGroup(u8 g)  { (void)g; }
s32  SEQ_PATTERN_PhraseMorphTarget(void)           { return -1; }
u8   SEQ_PATTERN_PhraseMorphValue(void)            { return 0; }

#endif // SEQ_PHRASE_MORPH


/////////////////////////////////////////////////////////////////////////////
// Requests a pattern change
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_Change(u8 group, seq_pattern_t pattern, u8 force_immediate_change)
{
  if( group >= SEQ_CORE_NUM_GROUPS )
    return -1; // invalid group

  // change immediately if sequencer not running
  if( force_immediate_change || !SEQ_BPM_IsRunning() || SEQ_BPM_TickGet() == 0 || ui_seq_pause ) {
    // store requested pattern
    portENTER_CRITICAL();
    pattern.REQ = 0; // request not required - we load the pattern immediately
    seq_pattern_req[group] = pattern;
    portEXIT_CRITICAL();

#if LED_PERFORMANCE_MEASURING
    MIOS32_BOARD_LED_Set(0x00000001, 1);
#endif
    SEQ_PATTERN_WritebackIfDirty(group);
    SEQ_PATTERN_Load(group, pattern);
#if LED_PERFORMANCE_MEASURING
    MIOS32_BOARD_LED_Set(0x00000001, 0);
#endif
  } else {

    // A new request overwrites a pending unserviced one (the long-standing
    // "stall here" TODO). Deliberately retired 2026-06-12: requests are
    // per-group, so the overwrite loses only an intermediate switch TARGET
    // (which never sounded). The writeback decision is NOT lost — it happens
    // at service time in SEQ_PATTERN_Handler against seq_pattern[group],
    // which still names the slot whose live state is in RAM.

    // in song mode it has to be considered, that this function is called multiple times
    // to request pattern changes for all groups

    // else request change
    portENTER_CRITICAL();
    pattern.REQ = 1;
    seq_pattern_req[group] = pattern;
    portEXIT_CRITICAL();

    if( seq_core_options.SYNCHED_PATTERN_CHANGE && !SEQ_SONG_ActiveGet() ) {
      // done in SEQ_CORE_Tick() when last step reached
    } else {
      if( seq_pattern_log_load_time ) {
	DEBUG_MSG("[SEQ_PATTERN:%d] Req G%d %c%d", SEQ_BPM_TickGet(), group+1, 'A'+pattern.group, pattern.num+1);
      }
      // pregenerate bpm ticks
      // (won't be generated again if there is already an ongoing request)
      MUTEX_MIDIOUT_TAKE;
      u16 switch_margin_ms = SEQ_CORE_SwitchMarginMs(); // measured I/O margin (B)
      s32 delay_ticks = SEQ_CORE_AddForwardDelay(switch_margin_ms);
      if( delay_ticks >= 0 ) {
      if( seq_pattern_log_load_time ) {
	DEBUG_MSG("[SEQ_PATTERN:%d] Forward Delay %d ticks based on %d mS margin", SEQ_BPM_TickGet(), delay_ticks, switch_margin_ms);
      }
      }
      MUTEX_MIDIOUT_GIVE;
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This function should be called from a separate task to handle pattern
// change requests
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_Handler(void)
{
  u8 group;
  u8 any_pattern_loaded = 0;

  // Fast path: this runs every 1 mS from the +2 UI task, but only a PENDING switch request
  // needs the SD mutex + critical section below. Checking the (sticky) request flags WITHOUT
  // the mutex here keeps the UI task from blocking on MUTEX_SDCARD on every tick — so the
  // control surface (LEDs/buttons) stays live while a lower-rate SD writer (e.g. a phrase
  // capture) holds the mutex for ~1 s. This handler is the only place that clears REQ, so a
  // request set just after the check is simply serviced on the next tick (never lost).
  {
    u8 any_req = 0;
    for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group)
      if( seq_pattern_req[group].REQ ) { any_req = 1; break; }
    if( !any_req )
      return 0; // nothing pending -> don't touch the SD mutex
  }

#if LED_PERFORMANCE_MEASURING
  MIOS32_BOARD_LED_Set(0x00000001, 1);
#endif

  MUTEX_SDCARD_TAKE; // take SD Card Mutex before entering critical section, because within the section we won't get it anymore -> hangup
  portENTER_CRITICAL();

  if( seq_pattern_log_load_time ) {
    MIOS32_STOPWATCH_Reset(); // note: conflicts with SEQ_STATISTICS_Stopwatch, but can be accepted if executed in critical section
  }

  for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group) {
    if( seq_pattern_req[group].REQ ) {
      seq_pattern_req[group].REQ = 0;

      // FEARLESS SWITCHING: persist the outgoing slot's live state before
      // anything replaces it. Must precede SEQ_PATTERN_Load (which updates
      // seq_pattern[group] to the incoming slot). The forward-delay margin
      // covers the added SD write (seq_core_pattern_switch_margin_ms).
      SEQ_PATTERN_WritebackIfDirty(group);

      if( seq_core_options.PATTERN_MIXER_MAP_COUPLING ) {
	u8 mixer_num = 0;
	u8 track;
				
	if (seq_pattern_req[0].lower) {
	  mixer_num = ((seq_pattern_req[0].group) * 8) + seq_pattern_req[0].num;
	} else {
	  mixer_num = (((seq_pattern_req[0].group) + 8) * 8) + seq_pattern_req[0].num;
	}
				
	// setup our requested pattern mixer map
	SEQ_MIXER_NumSet(mixer_num);
	SEQ_MIXER_Load(mixer_num);
			
	// dump mixer for tracks
	for(track = group * 4; track<((group+1)*4); ++track) {
					
	  // if we got the track bit setup inside our remix_map, them do not send mixer data for that track channel, let it be mixed down
	  if ( ((1 << track) | seq_pattern_remix_map) == seq_pattern_remix_map ) {
	    // do nothing for now...
	  } else {
	    SEQ_MIXER_SendAllByChannel(track);
	  }
	}
      }

      if( seq_pattern_log_load_time ) {
	DEBUG_MSG("[SEQ_PATTERN:%d] Load begin G%d %c%d", SEQ_BPM_TickGet(), group+1, 'A'+seq_pattern_req[group].group, seq_pattern_req[group].num+1);
      }
      SEQ_PATTERN_Load(group, seq_pattern_req[group]);
      if( seq_pattern_log_load_time ) {
	DEBUG_MSG("[SEQ_PATTERN:%d] Load end G%d %c%d", SEQ_BPM_TickGet(), group+1, 'A'+seq_pattern_req[group].group, seq_pattern_req[group].num+1);
      }
      any_pattern_loaded = 1;

      // restart *all* patterns?
      if( seq_core_options.RATOPC ) {
	MIOS32_IRQ_Disable(); // must be atomic
	seq_core_state.reset_trkpos_req |= (0xf << (4*group));
	MIOS32_IRQ_Enable();
      }
    }
  }
  u32 stopwatch_delta = MIOS32_STOPWATCH_ValueGet();
  portEXIT_CRITICAL();
  MUTEX_SDCARD_GIVE;
  
#if LED_PERFORMANCE_MEASURING
  MIOS32_BOARD_LED_Set(0x00000001, 0);
#endif

  if( any_pattern_loaded ) {
    // B: size the switch-quantize forward-delay margin from the REAL I/O time.
    // Keep the worst case seen (ms, u8) so the margin never undershoots a heavy
    // switch; >65 mS overflow pins it high. SEQ_CORE_SwitchMarginMs() adds headroom.
    // (Pattern-change path only for now; phrase recall keeps the fixed margin.)
    u32 measured_ms = (stopwatch_delta == 0xffffffff) ? 65 : (stopwatch_delta / 1000);
    if( measured_ms > 255 ) measured_ms = 255;
    if( (u8)measured_ms > seq_core_pattern_switch_measured_ms )
      seq_core_pattern_switch_measured_ms = (u8)measured_ms;

    if( stopwatch_delta == 0xffffffff ) {
      if( seq_pattern_log_load_time ) {
	DEBUG_MSG("[SEQ_PATTERN:%d] All patterns loaded in more than 65 mS!", SEQ_BPM_TickGet());
      }
    } else {
      if( seq_pattern_log_load_time ) {
	DEBUG_MSG("[SEQ_PATTERN:%d] All patterns loaded within %d.%03d mS", SEQ_BPM_TickGet(), stopwatch_delta / 1000, stopwatch_delta % 1000);
      }
    }
  }
  
  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Load a pattern from SD Card
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_Load(u8 group, seq_pattern_t pattern)
{
  s32 status;

  seq_pattern[group] = pattern;

  MUTEX_SDCARD_TAKE;

  if( (status=SEQ_FILE_B_PatternRead(pattern.bank, pattern.pattern, group, seq_pattern_remix_map)) < 0 )
    SEQ_UI_SDCardErrMsg(2000, status);
	
  seq_pattern_start_time = MIOS32_SYS_TimeGet();

  MUTEX_SDCARD_GIVE;

  // cancel sustain if there are no notes played by the track anymore
  {
    int i;
    u8 track = group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
    for(i=0; i<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++i, ++track)
      SEQ_CORE_CancelSustainedNotes(track);
  }

  // optionally unmute loaded tracks
  if( seq_core_options.UNMUTE_ON_PATTERN_CHANGE ) {
    u16 pattern = 0xf << (4*group);
    portENTER_CRITICAL();
    seq_core_trk_muted &= ~pattern;
    seq_core_trk_synched_mute &= ~pattern;
    seq_core_trk_synched_unmute &= ~pattern;
    portEXIT_CRITICAL();
  }

  // reset latched PB/CC values (because assignments could change)
  if( !seq_core_options.PATTERN_CHANGE_DONT_RESET_LATCHED_PC ) {
    SEQ_LAYER_ResetLatchedValues();
  }

  // send program change & bank selects
  {
    MUTEX_MIDIOUT_TAKE;
    int i;
    u8 track = group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
    for(i=0; i<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++i, ++track)
      SEQ_LAYER_SendPCBankValues(track, 0, 1);
    MUTEX_MIDIOUT_GIVE;
  }

  // slot == live by construction now. This also swallows the false dirty the
  // read itself raised (PatternRead replays CCs through the SEQ_CC_Set
  // chokepoint), so it must stay at the END of the load. The group is now a
  // legitimate writeback source (the loaded gate).
  MIOS32_IRQ_Disable();
  seq_pattern_dirty &= ~(1 << group);
  pattern_loaded |= (1 << group);
  MIOS32_IRQ_Enable();

  // this group's tracks were just replaced from disk -> drop the one-deep auto-undo
  // snapshot (an UNDO would otherwise clobber a freshly-loaded track with pre-load
  // bytes). Global/one-deep, so an unconditional drop is correct here.
  SEQ_GENERATOR_UndoInvalidate();

  // a pattern switch into the morph's focused group makes its arm-time A stale.
  SEQ_PATTERN_PhraseMorphInvalidateGroup(group);

  return status;
}

/////////////////////////////////////////////////////////////////////////////
// Stores a pattern into SD Card
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_Save(u8 group, seq_pattern_t pattern)
{
  s32 status;

  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_PatternWrite(seq_file_session_name, pattern.bank, pattern.pattern, group, 1);
  MUTEX_SDCARD_GIVE;

  // saving INTO the working slot makes slot == live again (covers the
  // writeback path and any aimed save targeting the working slot; an aimed
  // save to a DIFFERENT slot leaves the divergence — and the dirty bit —
  // intact)
  if( status >= 0 && group < SEQ_CORE_NUM_GROUPS &&
      pattern.bank == seq_pattern[group].bank &&
      pattern.pattern == seq_pattern[group].pattern ) {
    MIOS32_IRQ_Disable();
    seq_pattern_dirty &= ~(1 << group);
    MIOS32_IRQ_Enable();
  }

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Returns pattern name of a bank w/o overwriting RAM
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_PeekName(seq_pattern_t pattern, char *pattern_name)
{
  s32 status;

  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_PatternPeekName(pattern.bank, pattern.pattern, 0, pattern_name); // always cached!
  MUTEX_SDCARD_GIVE;

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Fetches all 8 patterns of a group and checks if they are empty (unnamed)
// Returns byte where each allocated pattern is flagged with a 1
// Note: this function call takes ca. 10 mS - it shouldn't be called too often!
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_PeekPatternsOfGroup(seq_pattern_t pattern)
{
  int num;
  char pattern_name[21];
  u8 allocated = 0;

  for(num=0; num<8; ++num) {
    pattern.num = num;
    if( SEQ_PATTERN_PeekName(pattern, pattern_name) >= 0 ) {
      // check if pattern is empty
      if( strcmp(pattern_name, "-----<empty>        ") != 0 ) {
        allocated |= (1 << num);
      }
    }
  }

  return allocated;
}

/////////////////////////////////////////////////////////////////////////////
// Fixes a pattern (load/modify/store)
// Can be used on format changes
// Uses group as temporal "storage"
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_Fix(u8 group, seq_pattern_t pattern)
{
  s32 status;

  MUTEX_SDCARD_TAKE;

  MIOS32_MIDI_SendDebugMessage("Loading bank #%d pattern %d\n", pattern.bank+1, pattern.pattern+1);
  if( (status=SEQ_FILE_B_PatternRead(pattern.bank, pattern.pattern, group, 0)) < 0 ) {
    SEQ_UI_SDCardErrMsg(2000, status);
    MIOS32_MIDI_SendDebugMessage("Read failed with status: %d\n", status);
  } else {
    // insert modification here
    int track_i;
    int track = group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
    for(track_i=0; track_i<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++track_i, ++track) {
      // Usage example (disabled as it isn't required anymore)
      // seq_cc_trk[track].clkdiv.value = 15; // due to changed resultion

#if 0
      seq_cc_trk[track].lfo_waveform = 0;
      seq_cc_trk[track].lfo_amplitude = 128 + 64;
      seq_cc_trk[track].lfo_phase = 0;
      seq_cc_trk[track].lfo_steps = 15;
      seq_cc_trk[track].lfo_steps_rst = 15;
      seq_cc_trk[track].lfo_enable_flags.ALL = 0;
      seq_cc_trk[track].lfo_cc = 0;
      seq_cc_trk[track].lfo_cc_offset = 64;
      seq_cc_trk[track].lfo_cc_ppqn = 6; // 96 ppqn
#endif
    }

    MIOS32_MIDI_SendDebugMessage("Saving bank #%d pattern %d\n", pattern.bank+1, pattern.pattern+1);
    if( (status=SEQ_FILE_B_PatternWrite(seq_file_session_name, pattern.bank, pattern.pattern, group, 1)) < 0 ) {
      SEQ_UI_SDCardErrMsg(2000, status);
      MIOS32_MIDI_SendDebugMessage("Write failed with status: %d\n", status);
    }
  }

  MUTEX_SDCARD_GIVE;

  // Fix tramples the group's live RAM as temp storage (documented terminal-
  // tool behavior) — that divergence must NOT be auto-committed to the
  // group's working slot by a later switch.
  MIOS32_IRQ_Disable();
  seq_pattern_dirty &= ~(1 << group);
  MIOS32_IRQ_Enable();

  // Stage B: the read seeded the fixed record's generators into the pool
  // (correct for the faithful write-back above) — but left engaged, they'd
  // mutate the trampled RAM through SEQ_PAR_Set, re-dirty the group, and a
  // later switch would auto-commit the debris. Same trample rule as the
  // dirty bit.
  {
    u8 t;
    u8 base = group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
    for(t=0; t<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++t)
      SEQ_GENERATOR_TrackClear(base + t);
  }

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Fixes all patterns of all banks
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_PATTERN_FixAll(void)
{
  s32 status = 0;

  int bank;
  for(bank=0; bank<SEQ_FILE_B_NUM_BANKS; ++bank) {
    int pattern_i;
    for(pattern_i=0; pattern_i<SEQ_FILE_B_NumPatterns(bank); ++pattern_i) {
      seq_pattern_t pattern;
      pattern.bank = bank;
      pattern.pattern = pattern_i;
      if( (status=SEQ_PATTERN_Fix(0, pattern)) < 0 )
	return status; // break process
    }
  }

  return status;
}
