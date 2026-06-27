// $Id$
/*
 * Sequencer Core Routines
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
#include <seq_midi_out.h>

#include "tasks.h"

#include "seq_core.h"
#include "seq_song.h"
#include "seq_random.h"
#include "seq_cc.h"
#include "seq_layer.h"
#include "seq_scale.h"
#include "seq_groove.h"
#include "seq_humanize.h"
#include "seq_robotize.h"
#include "seq_morph.h"
#include "seq_lfo.h"
#include "seq_midi_port.h"
#include "seq_midi_in.h"
#include "seq_midi_router.h"
#include "seq_par.h"
#include "seq_trg.h"
#include "seq_pattern.h"
#include "seq_file.h"
#include "seq_file_b.h"
#include "seq_random.h"
#include "seq_record.h"
#include "seq_live.h"
#include "seq_midply.h"
#include "seq_midexp.h"
#include "seq_midimp.h"
#include "seq_cv.h"
#include "seq_statistics.h"
#include "seq_ui.h"
#include "seq_generator.h"



/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

// set this to 1 if performance of clock handler should be measured with a scope
// (LED toggling in APP_Background() has to be disabled!)
// set this to 2 to visualize forward delay during pattern changes
#define LED_PERFORMANCE_MEASURING 0

// same for measuring with the stopwatch
// value is visible in menu (-> press exit button)
// value is visible in INFO->System page (-> press exit button, go to last item)
#define STOPWATCH_PERFORMANCE_MEASURING 1


/////////////////////////////////////////////////////////////////////////////
// Local prototypes
/////////////////////////////////////////////////////////////////////////////

static s32 SEQ_CORE_ResetTrkPos(u8 track, seq_core_trk_t *t, seq_cc_trk_t *tcc);
static s32 SEQ_CORE_NextStep(seq_core_trk_t *t, seq_cc_trk_t *tcc, u8 no_progression, u8 reverse);
static void SEQ_CORE_RobotizeLoopBarTick(seq_core_trk_t *t, seq_cc_trk_t *tcc);


/////////////////////////////////////////////////////////////////////////////
// Global variables
/////////////////////////////////////////////////////////////////////////////

seq_core_options_t seq_core_options;
u8 seq_core_steps_per_measure;
u8 seq_core_steps_per_pattern;

u8 seq_core_pattern_switch_margin_ms;
u8 seq_core_pattern_switch_measured_ms;
// SWITCH-QUANTIZE (Phase 2): a phrase recall loads its content at TAP (interrupts-on,
// off the hot path) then sets this to defer its bar-style re-phase to the next grid
// boundary, landed in the tick via reset_trkpos_req (RATOPC's re-phase mechanism).
volatile u8 seq_core_recall_rephase_req;

u16 seq_core_trk_muted;
u16 seq_core_trk_synched_mute;
u16 seq_core_trk_synched_unmute;
seq_core_slaveclk_mute_t seq_core_slaveclk_mute;
u16 seq_core_trk_soloed;

u8 seq_core_step_update_req;

u8 seq_core_global_scale;
u8 seq_core_global_scale_root;
u8 seq_core_global_scale_root_selection;
u8 seq_core_keyb_scale_root;

// GRAVITY field (Tension Workbench, §2). −64..+63, center 0 = pass-through.
s8 seq_core_tension_gravity;

u8 seq_core_global_transpose_enabled;

u8 seq_core_bpm_preset_num;
float seq_core_bpm_preset_tempo[SEQ_CORE_NUM_BPM_PRESETS];
float seq_core_bpm_preset_ramp[SEQ_CORE_NUM_BPM_PRESETS];

u8 seq_core_din_sync_pulse_ctr;

mios32_midi_port_t seq_core_metronome_port;
u8 seq_core_metronome_chn;
u8 seq_core_metronome_note_m;
u8 seq_core_metronome_note_b;

mios32_midi_port_t seq_core_shadow_out_port;
u8 seq_core_shadow_out_chn;

seq_core_state_t seq_core_state;
seq_core_trk_t seq_core_trk[SEQ_CORE_NUM_TRACKS];

seq_core_loop_mode_t seq_core_glb_loop_mode;
u8 seq_core_glb_loop_offset;
u8 seq_core_glb_loop_steps;

// Phase A render cache: per-track dirty flag. Set by any source-mutating path
// (SEQ_PAR_Set, SEQ_TRG_Set, bulk loads, copy/paste/undo, capture, etc.).
// Cleared by SEQ_CORE_RenderTrack after the identity memcpy. Zero-init OK —
// SEQ_PAR_Init/SEQ_TRG_Init dirty all tracks on startup via SEQ_*_TrackInit.
u8 seq_render_dirty[SEQ_CORE_NUM_TRACKS];

// Phase D.1 — per-track touched timestamp in milliseconds (MIOS32 wall clock,
// see MIOS32_TIMESTAMP_Get). Bumped by SEQ_CORE_RenderTouched on any change
// that affects the processor stack's output; read by SEQ_CORE_RenderSweeping
// to decide sweep-vs-quiet. Zero = pristine (never touched, treat as quiet).
u32 seq_render_touched_ms[SEQ_CORE_NUM_TRACKS];

// Phase D.3 — per-track active half-buffer index. Quiet render writes the
// inactive half, then a single-byte XOR flips this — atomic on Cortex-M, so
// the tick path never reads a half-rendered output. Sweep render writes
// active directly (no flip; tearing during knob motion is acceptable).
u8 seq_render_active_buf[SEQ_CORE_NUM_TRACKS];

// Phase D — sweep/quiet change-detection (play-readiness #5). The force-dirty
// processors (CHORD_MASK / TENSION / live PITCH) depend on inputs that can change
// between two ticks WITHOUT touching the source dirty flag: the held chord on a
// bus (SEQ_MIDI_IN — never dirties), the live transposer note, and the global
// GRAVITY / scale / root. The old code re-rendered every armed track EVERY tick to
// catch them (~95% render duty at 16 gripped GRAVITY tracks → the lowest-priority
// +2 UI task starves, the control surface goes dark). Instead, fold every such
// live input into a per-track u32 signature; a track only re-renders when its
// signature changes (a dial sweep / chord change / transposer move). A static field
// is signature-stable and costs nothing — UI stays alive with all 16 gripped.
// Source edits / generator mutation / capture still dirty through the existing flag
// (SEQ_CORE_RenderTouched on source writes), so the signature covers ONLY the live
// inputs that don't otherwise dirty. Stored = the signature as of the last render
// (see SEQ_CORE_RenderTracks). Zero-init OK: the first render of any armed track is
// driven by the slot-sync dirty, and stores the fresh signature.
static u32 seq_render_live_sig[SEQ_CORE_NUM_TRACKS];

// Phase B render cache: per-track processor stack. Empty in phase B
// (zero-init → every slot has id == SEQ_PROCESSOR_ID_NONE), so the renderer
// iteration is a no-op and output stays identical to source. Phase C wires
// the first real processor (chord_mask) into SEQ_CORE_RenderTrack's dispatch.
// CCM-placed alongside the par/trg output mirrors; the stack itself is read-
// only on the tick path, so no cache-coherency concerns.
#ifndef CCM_SECTION
#define CCM_SECTION
#endif
seq_processor_slot_t CCM_SECTION seq_processor_stack[SEQ_CORE_NUM_TRACKS][SEQ_CORE_NUM_PROCESSOR_SLOTS];


/////////////////////////////////////////////////////////////////////////////
// Retroactive CAPTURE — per-bar generative-frame ring (2026-06-19)
//
// The box is always listening: at each musical-measure boundary it snapshots the
// VISIBLE track's generative FRAME (the deterministic state needed to re-simulate
// that measure forward) into a depth-17 ring (the live in-progress bar + 16
// grabbable completed bars), indexed robotize_measure_ctr % SEQ_CORE_CAP_RING_BARS.
// CAPTURE then rewinds to the window-start frame and re-drives K bars forward WITH
// generator wander on — reproducing the lived span exactly (the per-track-RNG
// keystone made every generative stream deterministic from this frame). The source
// par buffer is NOT ringed: restoring the generator loop[]+seed and re-driving
// regenerates it via write_loop_to_source.
//
// Single track (the visible one), invalidated on track-switch — bounds the
// generator-slot snapshots (the costly part) to one track. The frame is
// SELF-CONTAINED: it carries its OWN robotize seed (so re-sim no longer reads the
// 16-deep robotize_seed_snapshots ring, which only reaches 15 bars back and is
// shared with FREEZE) plus random traversal + the generator slots + the
// step/progression cursor.
// All main SRAM (no CCM_SECTION = .bss in main SRAM, like the robotize ring).
/////////////////////////////////////////////////////////////////////////////
#define SEQ_CORE_CAP_RING_BARS  17   // depth = the live (in-progress) bar + 16 grabbable
                                     // completed bars. Indexed robotize_measure_ctr % BARS
                                     // (17 isn't a power of 2, so the modulo is explicit).
                                     // The frame carries its OWN robotize seed (below), so this
                                     // ring is self-contained and does NOT touch the 16-deep
                                     // robotize_seed_snapshots ring that FREEZE shares.
#define SEQ_CORE_CAP_RING_SLOTS 4    // max generator slots ringed per bar (melodic-first)

typedef struct {
  u32 traverse_state;      // random_traverse_state at this measure boundary
  u32 robotize_seed;       // robotize_seed_state at this boundary — self-contained so the re-sim
                           // doesn't read the 16-deep robotize_seed_snapshots ring (which only
                           // reaches 15 bars back and is shared with FREEZE)
  u8  step;                // t->step at the boundary, captured in the tick PROLOGUE BEFORE
                           // the body's NextStep advance -> the PRE-advance (last) step, NOT
                           // 0 (forward: == tcc->length; random traversal: an RNG value).
                           // Loop boundaries are found by frame-count arithmetic, not this
                           // field (see SEQ_CORE_CaptureRingLoopWindow).
  u8  step_saved;          // replay anchor
  u8  step_replay_ctr;     // progression counters (forward/jmpbck/replay/repeat/skip)
  u8  step_fwd_ctr;
  u8  step_interval_ctr;
  u8  step_repeat_ctr;
  u8  step_skip_ctr;
  u8  gen_count;           // generator slots captured this bar (<= SLOTS)
  seq_generator_t gen[SEQ_CORE_CAP_RING_SLOTS]; // slot state at this measure's ref_step==0
                                                // prologue (BEFORE the body NextStep advance
                                                // and BEFORE the new measure's mutate, which
                                                // fires the following tick). Re-sim restores it
                                                // verbatim and reproduces the same advance+mutate
                                                // cadence, so capture & replay share the convention.
} seq_core_cap_frame_t;

static seq_core_cap_frame_t seq_core_cap_ring[SEQ_CORE_CAP_RING_BARS]; // main SRAM
static u8  seq_core_cap_ring_track    = 0xff; // track the ring records (0xff = invalid)
static u8  seq_core_cap_ring_filled   = 0;    // valid measures recorded (<= BARS)
static u8  seq_core_cap_ring_overflow = 0;    // a measure had > SLOTS gens (capture incomplete)
static u8  seq_core_cap_resim_active  = 0;    // 1 while a CAPTURE re-sim drives the engine
                                              // (suppresses ring writes so re-sim doesn't
                                              //  overwrite the very history it reads)
static u8  seq_core_cap_suppress_journal = 0; // 1 while CaptureSpanToSlotTrack runs its
                                              // internal scratch-track CaptureSpan — that
                                              // dst is fully restored, so its undo arm must
                                              // not clobber the user's one-deep journal

/////////////////////////////////////////////////////////////////////////////
// Retroactive CAPTURE — while-PLAYING live tape (2026-06-20)
//
// When STOPPED the box must re-simulate the unrecorded past (the frame ring +
// SEQ_CORE_CaptureSpanReSim, above). When PLAYING the notes are sounding RIGHT
// NOW, so we just record the emitted stream — strictly MORE faithful (it keeps
// emission coin-flips / live keys / real timing that re-sim can't reproduce) and
// it never borrows the live engine. This is literally "Capture MIDI."
//
// A flat ring of emitted note-ons (absolute SCHEDULED tick) for the recording
// track, tapped passively off the MIDI-out drain (SEQ_MIDI_OUT's tap tee). Bars
// are NOT bucketed at tap time — the measure counter advances at PREFETCH time
// (a few ticks ahead of the drain), so a bar's tail would mis-bucket. Instead
// each event keeps its true musical tick (item->timestamp) and the per-bar
// downbeat tick is stamped at the ref_step==0 hook; a grab maps tick->step from
// those, immune to prefetch skew. The tape rides the SAME bars as the frame ring
// (shares seq_core_cap_ring_track / _filled), so the thermometer + bar bookkeeping
// are unified.
/////////////////////////////////////////////////////////////////////////////
typedef struct {
  u32 tick;   // absolute scheduled bpm-tick (item->timestamp at the drain)
  u16 gate;   // gate length in bpm-ticks (off.tick - on.tick); 0 = no off seen yet -> default
  u8  note;
  u8  vel;
} seq_core_cap_tape_evt_t; // 8 bytes (the old {u32,u8,u8} was already padded to 8 -> gate is free)
#define SEQ_CORE_CAP_TAPE_EVENTS 768  // ~6 KB main SRAM; 16 bars of mono melodic ~256 notes,
                                      // so ~3x headroom. Dense/poly overruns -> grab refuses (-10).
static seq_core_cap_tape_evt_t seq_core_cap_tape[SEQ_CORE_CAP_TAPE_EVENTS]; // main SRAM
static u16 seq_core_cap_tape_head;    // ring write cursor (next slot)
static u16 seq_core_cap_tape_count;   // retained events (saturates at EVENTS once wrapped)
static u32 seq_core_cap_tape_bar_start[SEQ_CORE_CAP_RING_BARS]; // downbeat abs tick per bar slot


// Convert a measured gate length (bpm-ticks) into a stored length-layer par value.
// Playback re-derives the gate as tps*(v+1)/96 (the step scheduler at line ~4162 /
// SEQ_PAR_LengthGet), so invert: v = round(gate*96/tps) - 1, clamped to a single step's
// range [0..95] (95 -> len 96 = glide/tie, the longest a single step expresses). A 0 gate
// (no note-off recorded: still ringing at grab, or it scrolled out) falls back to the
// ~3/4 default both capture paths used before precise gate.
#define SEQ_CORE_CAP_DEFAULT_LEN 71   // ~3/4 gate (v=71 -> len 72 -> 72/96)
static u8 SEQ_CORE_CaptureGateToParLen(u32 gate_ticks, u16 tps)
{
  if( gate_ticks == 0 || tps == 0 ) return SEQ_CORE_CAP_DEFAULT_LEN;
  u32 len = (gate_ticks * 96 + (tps / 2)) / tps;  // nearest len (= v+1)
  if( len < 1 ) len = 1;
  if( len > 96 ) len = 96;
  return (u8)(len - 1);
}

// Materialize a captured note that sounded for `gate` bpm-ticks starting at dst step
// `step0`, reproducing SEQ's MULTI-STEP length encoding. A hand-drawn note longer than one
// step isn't a single high length value (a step's length maxes at Gld = one step + tie) —
// it's a CHAIN: the player maxes a step's length, then carries it on the next step's length,
// and so on. So a captured note within one step keeps its fractional gate (unchanged), but a
// longer note must be written across the steps it covers: gated Gld start, each fully-covered
// step carried at Gld (note repeated, gate OFF so it doesn't re-trigger -> sustains/ties), and
// a final partial step (note, gate OFF, length < Gld) whose sub-step length terminates the
// sustain at the right tick. Without this the lone Gld start step just ties to the NEXT note,
// losing the real duration. (Needs a length layer to carry the tie; without one a >1-step note
// degrades to a single gated step.)
static void SEQ_CORE_CaptureMaterializeNote(u8 dst, u16 step0, u16 dst_steps, u32 gate, u16 tps,
                                            u8 note, u8 vel,
                                            s8 note_layer, s8 vel_layer, s8 len_layer)
{
  if( tps == 0 ) tps = 96;
  u16 full = (u16)(gate / tps);   // fully-covered steps (0 if the note fits within one step)
  u16 rem  = (u16)(gate % tps);   // partial tail ticks into the next step

  // start step (always gated)
  if( note_layer >= 0 ) SEQ_PAR_Set(dst, step0, (u8)note_layer, 0, note);
  if( vel_layer  >= 0 ) SEQ_PAR_Set(dst, step0, (u8)vel_layer,  0, vel);
  if( len_layer  >= 0 )
    SEQ_PAR_Set(dst, step0, (u8)len_layer, 0,
                (full >= 1) ? 95 : SEQ_CORE_CaptureGateToParLen(gate, tps));
  SEQ_TRG_GateSet(dst, step0, 0, 1);
  if( full < 1 || len_layer < 0 )
    return;                       // single-step note, or no length layer to carry the tie

  // carried full steps: Gld + note/vel repeated, gate stays OFF (PrepDst cleared the trg
  // buffer). Carry the velocity too so the captured track matches the source's data — the
  // carried steps are gate-off ties so it doesn't sound, but an edit that gates one later
  // must find the note's velocity there, not 0.
  u16 i;
  for(i=1; i<full; ++i) {
    u16 st = (u16)(step0 + i);
    if( st >= dst_steps ) return;
    if( note_layer >= 0 ) SEQ_PAR_Set(dst, st, (u8)note_layer, 0, note);
    if( vel_layer  >= 0 ) SEQ_PAR_Set(dst, st, (u8)vel_layer,  0, vel);
    SEQ_PAR_Set(dst, st, (u8)len_layer, 0, 95);
  }
  // partial tail: a gate-off note event whose <Gld length ends the sustain mid-step
  if( rem > 0 ) {
    u16 st = (u16)(step0 + full);
    if( st >= dst_steps ) return;
    u8 tail = SEQ_CORE_CaptureGateToParLen(rem, tps);
    if( tail > 94 ) tail = 94;    // keep < 96 (Gld) so it terminates rather than tying onward
    if( note_layer >= 0 ) SEQ_PAR_Set(dst, st, (u8)note_layer, 0, note);
    if( vel_layer  >= 0 ) SEQ_PAR_Set(dst, st, (u8)vel_layer,  0, vel);
    SEQ_PAR_Set(dst, st, (u8)len_layer, 0, tail);
  }
}

// Discard the live tape (run-start / track-switch): the recorded notes no longer
// describe the recording track.
static void SEQ_CORE_CaptureTapeReset(void)
{
  seq_core_cap_tape_head = 0;
  seq_core_cap_tape_count = 0;
  // Clear the bar markers too: a grab only reads bar_start[(ctr-k)%BARS] for k < filled,
  // and filled counts only this-run boundaries, so stale ticks can't be reached today —
  // but zeroing here removes the latent coupling (defense-in-depth, 68 B).
  memset(seq_core_cap_tape_bar_start, 0, sizeof(seq_core_cap_tape_bar_start));
}

// Invalidate the ring (run-start / track-switch): the recorded history no longer
// describes the live engine.
static void SEQ_CORE_CaptureRingReset(void)
{
  seq_core_cap_ring_track = 0xff;
  seq_core_cap_ring_filled = 0;
  seq_core_cap_ring_overflow = 0;
  SEQ_CORE_CaptureTapeReset();
}

// Passive tee from the MIDI-out drain (SEQ_MIDI_OUT_Handler), installed once at init
// and always live. Records the recording track's emitted notes into the live tape while
// PLAYING. Cheap fast-path guards first; ignored during re-sim drives (those use the
// redirect sink, not this tee) and when stopped (nothing to record). Note-ons append to
// the ring; note-offs (running-status, vel 0) back-fill the matching on's gate so a grab
// reproduces the precise articulation, not a fixed default.
static s32 SEQ_CORE_CaptureTapeTap(mios32_midi_port_t port, mios32_midi_package_t p, u32 timestamp)
{
  (void)port;
  if( seq_core_cap_resim_active ) return 0;                 // re-sim uses the redirect sink, not the tee
  if( seq_core_cap_ring_track >= SEQ_CORE_NUM_TRACKS ) return 0; // no recording track
  if( !SEQ_BPM_IsRunning() ) return 0;                      // tape only while playing
  if( p.cable != seq_core_cap_ring_track ) return 0;        // only the recording track (cable carries it)
  if( p.event != 0x9 ) return 0;                            // note events only

  if( p.velocity == 0 ) {
    // Note-off (running-status: every engine note-off is the same package with vel 0,
    // seq_midi_out.c:672 / seq_core.c:2699). Back-fill the gate of the most recent still-
    // open note-on of the same number (LIFO match — correct for re-triggers). The off's
    // scheduled tick is on_tick+gatelength, so gate = timestamp - on.tick. A note whose off
    // never arrives (held past grab / scrolled out of the ring) keeps gate 0 -> default.
    u16 n = seq_core_cap_tape_count;
    u16 idx = seq_core_cap_tape_head;                       // one past the newest; walk back
    u16 j;
    for(j=0; j<n; ++j) {
      idx = (u16)((idx + SEQ_CORE_CAP_TAPE_EVENTS - 1) % SEQ_CORE_CAP_TAPE_EVENTS);
      seq_core_cap_tape_evt_t *e = &seq_core_cap_tape[idx];
      if( e->note == p.evnt1 && e->gate == 0 ) {
        s32 g = (s32)(timestamp - e->tick);
        if( g < 1 ) g = 1;                                  // guard same-tick / reorder
        e->gate = (g > 0xffff) ? 0xffff : (u16)g;
        break;
      }
    }
    return 0;
  }

  // Note-on: append (gate filled in by the matching off above).
  seq_core_cap_tape_evt_t *e = &seq_core_cap_tape[seq_core_cap_tape_head];
  e->tick = timestamp;
  e->gate = 0;
  e->note = p.evnt1;
  e->vel  = p.evnt2;
  seq_core_cap_tape_head = (u16)((seq_core_cap_tape_head + 1) % SEQ_CORE_CAP_TAPE_EVENTS);
  if( seq_core_cap_tape_count < SEQ_CORE_CAP_TAPE_EVENTS )
    ++seq_core_cap_tape_count;
  return 0;
}

// Append the VISIBLE track's generative frame at a measure boundary. Called once per
// global measure boundary (ref_step==0) AFTER robotize_measure_ctr++ and the robotize
// ring write, so it shares that index. This runs in the tick PROLOGUE — before the body
// NextStep advances t->step, and before the new measure's generator mutate (which fires
// the following tick). So the frame holds the pre-advance step + the loop going INTO the
// boundary; the re-sim restores it verbatim and re-drives, reproducing the exact
// advance+mutate cadence (capture & replay share the convention).
static void SEQ_CORE_CaptureRingTick(u32 bpm_tick)
{
  if( seq_core_cap_resim_active ) return; // re-sim drive: don't pollute the ring
  u8 vis = SEQ_UI_VisibleTrackGet();
  if( vis >= SEQ_CORE_NUM_TRACKS ) return;
  if( vis != seq_core_cap_ring_track ) {
    seq_core_cap_ring_track = vis;     // follow the visible track; old history is void
    seq_core_cap_ring_filled = 0;
    seq_core_cap_ring_overflow = 0;
    SEQ_CORE_CaptureTapeReset();       // the recorded notes were the OLD track's
  }
  seq_core_trk_t *t = &seq_core_trk[vis];
  // Stamp this measure's downbeat tick (the true musical tick passed in, NOT a counter,
  // so the while-playing tape maps note ticks -> steps without prefetch skew). Shares the
  // frame ring's per-measure index.
  seq_core_cap_tape_bar_start[t->robotize_measure_ctr % SEQ_CORE_CAP_RING_BARS] = bpm_tick;
  seq_core_cap_frame_t *f = &seq_core_cap_ring[t->robotize_measure_ctr % SEQ_CORE_CAP_RING_BARS];
  f->traverse_state    = t->random_traverse_state;
  f->robotize_seed     = t->robotize_seed_state; // self-contained: re-sim restores from the frame
  f->step              = t->step;
  f->step_saved        = t->step_saved;
  f->step_replay_ctr   = t->step_replay_ctr;
  f->step_fwd_ctr      = t->step_fwd_ctr;
  f->step_interval_ctr = t->step_interval_ctr;
  f->step_repeat_ctr   = t->step_repeat_ctr;
  f->step_skip_ctr     = t->step_skip_ctr;
  f->gen_count = SEQ_GENERATOR_TrackSnapshot(vis, f->gen, SEQ_CORE_CAP_RING_SLOTS);
  if( SEQ_GENERATOR_TrackEngagedCount(vis) > SEQ_CORE_CAP_RING_SLOTS )
    seq_core_cap_ring_overflow = 1; // more gens than the ring holds -> capture refuses
  if( seq_core_cap_ring_filled < SEQ_CORE_CAP_RING_BARS )
    ++seq_core_cap_ring_filled;
}

// Resolve the loop-aligned CAPTURE window for `track` (Approach B, 2026-06-26 —
// lifting the one-global-measure constraint to N whole global measures).
//
// The ring pushes EXACTLY ONE frame per GLOBAL measure (SEQ_CORE_CaptureRingTick at
// ref_step==0, robotize_measure_ctr index), so an N-global-measure track is n = spm/gspm
// frames per loop. robotize_measure_ctr resets to 0 at transport start and the FIRST global
// measure increments it to 1 with the track still at step 0 (FIRST_CLK suppresses that
// tick's NextStep advance), so the track's loop-START frames sit at robotize_measure_ctr
// ≡ 1 (mod n) — NOT ≡ 0 — for a grid-aligned start (the by-ear case; a synch/SPP-offset
// start is the documented phase edge). We detect boundaries by FRAME-COUNT ARITHMETIC, NOT
// by frame->step: the frame is snapshotted in the tick PROLOGUE *before* the body's NextStep
// wraps, so frame->step holds the PRE-advance step (== tcc->length for a forward loop, an
// arbitrary RNG value for random traversal) — it is never reliably 0 at a boundary. (The
// step==0 premise AND a ≡0 phase were both shipped as bugs in the first cut; the arithmetic
// below reduces to the original per-bar FrameBack(k) for n==1, the shipped one-measure path.)
//
//   e = (ctr-1) % n    -> frames back to the most-recent loop-START frame (the window's END
//                         boundary); 0 when ctr is itself a loop start. For n==1, e==0 always.
//   win-START          -> the k-th loop-start back = e + k*n
//   max_loops          -> (filled-1 - e) / n   complete loops of history
//
//   k==0 -> query only: *max_loops = total complete loops available (thermometer cap).
//   k>=1 -> *o_out / *e_out = measure offsets (from ctr) of the window START / END loop
//           boundaries; the window spans k loops. Re-sim restores ring[(ctr-*o)]; the tape
//           maps bar_start[(ctr-*o)] .. bar_start[(ctr-*e)].
// `n` (frames per loop = spm/gspm, >=1) is passed in (the callers already compute spm/gspm).
// Returns 0 on success; -2 = no completed loop boundary / fewer than k loops of history.
static s32 SEQ_CORE_CaptureRingLoopWindow(u8 track, u32 ctr, u8 n, u8 k,
                                          u8 *max_loops, u8 *o_out, u8 *e_out)
{
  if( max_loops ) *max_loops = 0;
  if( track != seq_core_cap_ring_track || n < 1 ) return -2;
  u8 filled = seq_core_cap_ring_filled;
  if( filled < 2 ) return -2;                 // (ensures ctr >= 2, so ctr-1 can't underflow)

  u8 e = (u8)((ctr - 1) % n);                // frames back to the most-recent loop-START
  if( e >= filled ) return -2;               // no completed loop boundary recorded yet
  u8 count = (u8)((filled - 1 - e) / n);     // complete loops behind the end boundary
  if( max_loops ) *max_loops = count;
  if( k == 0 ) return 0;                      // query-only (thermometer)
  if( k > count ) return -2;                  // not enough history for k loops
  if( o_out ) *o_out = (u8)(e + (u32)k * n);  // <= filled-1 since k<=count
  if( e_out ) *e_out = e;
  return 0;
}

// HIL / capture-verb accessors.
u8 SEQ_CORE_CaptureRingDepth(void) { return seq_core_cap_ring_filled; }
u8 SEQ_CORE_CaptureRingTrack(void) { return seq_core_cap_ring_track; }
u8 SEQ_CORE_CaptureRingOverflow(void) { return seq_core_cap_ring_overflow; }

// Effective CAPTURE loop length (steps) for `src`. A SYNCH_TO_MEASURE track is
// force-reset to step 0 at every global-measure boundary (SEQ_CORE_ResetTrkPos in the
// tick body, gated on synch_to_measure_req), so its AUDIBLE loop is exactly ONE global
// measure regardless of tcc->length — route it as a 1-bar loop (spm = gspm, n = 1, the
// most-validated path) instead of refusing it (re-sim) or slicing by its raw length
// (tape's non-aligned branch). Both the per-measure tape markers and the re-sim drive
// (which re-applies the synch reset at tick B) line up exactly with the synch'd loop.
// Any other track loops at its own length. seq_core_steps_per_measure is stored as
// (steps-1). The three capture call sites share this so they can never disagree.
static u16 SEQ_CORE_CaptureLoopSteps(u8 src)
{
  if( src >= SEQ_CORE_NUM_TRACKS ) return 0;
  if( seq_cc_trk[src].clkdiv.SYNCH_TO_MEASURE )
    return (u16)(seq_core_steps_per_measure + 1);   // one global measure
  return (u16)(seq_cc_trk[src].length + 1);          // the track's own loop
}

// Max grabbable K (bars) for `src` right now: the SMALLER of the ring depth and
// what the destination buffers can hold (the dst inherits src's layout, so a heavy
// src — e.g. a 16-instrument drum track — caps the par buffer well below the ring).
// The UI thermometer + "max N bars" use this so the lit GP LEDs == what a grab
// actually accepts (raw ring depth would over-promise). 0 = nothing grabbable.
// Mirrors the steps>256 / par / trg guards in SEQ_CORE_CaptureSpanReSim exactly.
u8 SEQ_CORE_CaptureMaxK(u8 src)
{
  if( src >= SEQ_CORE_NUM_TRACKS || src != seq_core_cap_ring_track ) return 0;

  // spm = steps per LOOP; gspm = global Steps-per-Measure (stored as steps-1). A synch'd
  // track reports gspm here (SEQ_CORE_CaptureLoopSteps) so the thermometer matches the grab.
  u16 spm  = SEQ_CORE_CaptureLoopSteps(src);
  u16 gspm = (u16)(seq_core_steps_per_measure + 1);
  if( spm == 0 ) return 0;
  u8 running = SEQ_BPM_IsRunning();

  // Loop depth in LOOPS, by path:
  u8 max_k = 0;
  if( gspm != 0 && (spm % gspm) == 0 ) {
    // Whole-measure (Approach B): count the complete loop-aligned spans in the frame ring.
    // n = frames per loop. STOPPED re-sim grabs ONE-measure tracks only (n==1); a MULTI-
    // measure stopped grab is refused (drive-phase A2 deferred), so report 0 then. While
    // PLAYING the tape grabs any n. The generative-frame overflow only blocks the STOPPED
    // re-sim (it replays the frame); the tape records emitted notes regardless of gen count.
    if( seq_core_cap_ring_filled < 2 ) return 0;
    u8 n = (u8)(spm / gspm);                              // frames per loop
    if( !running && (n != 1 || seq_core_cap_ring_overflow) ) return 0;
    u32 mctr = seq_core_trk[src].robotize_measure_ctr;
    if( SEQ_CORE_CaptureRingLoopWindow(src, mctr, n, 0, &max_k, NULL, NULL) != 0 || max_k == 0 )
      return 0;
  } else {
    // Approach A non-(whole-measure): only the while-PLAYING tape grabs (stopped re-sim of a
    // non-aligned loop needs the A2 phase kernel). Depth = complete loop PERIODS played
    // since transport start, capped at the ring depth (the tape-density cap is enforced at
    // grab time via -10).
    if( !running ) return 0;
    u16 tps = seq_core_trk[src].step_length;
    if( tps == 0 ) tps = (u16)((seq_cc_trk[src].clkdiv.value + 1) * (seq_cc_trk[src].clkdiv.TRIPLETS ? 4 : 6));
    if( tps == 0 ) tps = 96;
    u32 loops_done = (u32)SEQ_BPM_TickGet() / ((u32)spm * tps);
    if( loops_done == 0 ) return 0;
    max_k = (loops_done > (SEQ_CORE_CAP_RING_BARS - 1)) ? (SEQ_CORE_CAP_RING_BARS - 1) : (u8)loops_done;
  }

  if( max_k > 256 / spm ) max_k = (u8)(256 / spm);         // dst_steps = K*spm <= 256

  u32 par_unit = (u32)spm * SEQ_PAR_NumLayersGet(src) * SEQ_PAR_NumInstrumentsGet(src);
  if( par_unit && max_k > SEQ_PAR_MAX_BYTES / par_unit )
    max_k = (u8)(SEQ_PAR_MAX_BYTES / par_unit);            // dst par buffer

  u32 trg_unit = (u32)SEQ_TRG_NumLayersGet(src) * SEQ_TRG_NumInstrumentsGet(src);
  while( max_k > 0 && (u32)(((u16)max_k * spm + 7) / 8) * trg_unit > SEQ_TRG_MAX_BYTES )
    --max_k;                                               // dst trg buffer (exact ceil)

  return max_k;
}


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static u32 bpm_tick_prefetch_req;
static u32 bpm_tick_prefetched;

// --- emission-task service-gap instrumentation (capture/SD-write freeze probe) ---
// SEQ_CORE_Handler runs once per TASK_MIDI (+4) service (~every 1ms while the engine
// runs). bpm_tick is incremented in the HIGHEST-priority HW-timer ISR
// (SEQ_BPM_Timer_Master), so SEQ_BPM_TickGet() keeps counting even while the emission
// TASK is starved — which makes it USELESS for detecting a clock-task stall. The gap
// (in ISR ticks) between successive SEQ_CORE_Handler entries CAN detect one: a healthy
// clock services every tick or two, but if a lower-priority task hogs the CPU (e.g. a
// long SD write that never yields) bpm_tick races ahead and the gap balloons. We track
// the peak gap since the last reset so a perf verb (CMD_CAPTURE_PERF) can fire a capture
// and read back how long emission was actually starved. See SEQ_CORE_ServiceGapReset /
// SEQ_CORE_ServiceMaxGapGet, and the tracker at the top of SEQ_CORE_Handler.
static u32 seq_core_service_last_tick;  // bpm_tick at the last SEQ_CORE_Handler entry
static u32 seq_core_service_max_gap;    // peak inter-service gap (ISR ticks) since reset

// Same probe, but for the +2 UI task (SEQ_TASK_Period1mS — LED/LCD/button scan). The
// +4 emission task preempts a +3 SD write so the audible clock survives; the +2 UI task
// is BELOW the writer and gets fully starved during a write (the ~640 ms control-surface
// hang). This pair measures that starvation the same way: peak gap between UI-task runs.
static u32 seq_core_ui_service_last_tick; // bpm_tick at the last SEQ_TASK_Period1mS entry
static u32 seq_core_ui_service_max_gap;   // peak inter-service gap (ISR ticks) since reset

static float seq_core_bpm_target;
static float seq_core_bpm_sweep_inc;

// All-16 force-dirty render-cost probe (play-readiness #5). chord_mask / tension /
// live-pitch slots force a FULL per-track re-render every tick — memcpy(SEQ_PAR_MAX_BYTES)
// + memcpy(SEQ_TRG_MAX_BYTES) + a processor sweep over up to num_p_steps — bypassing the
// sweep/quiet fast path. With 16 tracks armed (e.g. GRAVITY GRIP on all) that is the
// all-sweeping worst case the design never measured: if it eats too much of a tick the
// +4 emission task (which runs this very render in its prologue) falls behind and the
// audible clock slips. We time each render with the Cortex-M4 DWT cycle counter, NOT
// MIOS32_STOPWATCH — TIM6 is already owned by SEQ_STATISTICS and brackets this handler,
// so a nested stopwatch Reset/ValueGet would corrupt both readings (see seq_pattern.c).
// This fork's CMSIS core_cm4.h omits the DWT register block, so we map the trace unit's
// architecturally-fixed Cortex-M4 addresses directly (identical on every CM3/CM4):
// DEMCR.TRCENA enables the trace subsystem, DWT_CTRL.CYCCNTENA the free-running cycle
// counter, read with a single load. Enabled once in SEQ_CORE_Init. Accumulators are
// main-RAM statics like the service-gap pair above, read/zeroed via CMD_RENDER_PERF.
// Total is accumulated in microseconds (u32; the per-tick divide is by a compile-time
// constant); the peak cycle counts convert to us only at readout.
#define SEQ_CORE_DWT_CTRL    (*(volatile u32 *)0xE0001000)
#define SEQ_CORE_DWT_CYCCNT  (*(volatile u32 *)0xE0001004)
#define SEQ_CORE_SCB_DEMCR   (*(volatile u32 *)0xE000EDFC)
#define SEQ_CORE_DWT_CTRL_CYCCNTENA  (1u << 0)
#define SEQ_CORE_SCB_DEMCR_TRCENA    (1u << 24)
#define SEQ_CORE_DWT_CYC_PER_US      (MIOS32_SYS_CPU_FREQUENCY / 1000000)  // 168 on STM32F407
static u32 seq_core_render_total_us;      // summed render time (us) across the window
static u32 seq_core_render_max_tick_cyc;  // peak total render of one RenderTracks pass (cycles, all dirty tracks summed)
static u32 seq_core_render_max_track_cyc; // peak single-track render (cycles)
static u32 seq_core_render_tick_count;    // RenderTracks passes that rendered >=1 track since reset
static u8  seq_core_render_last_dirty;    // tracks rendered on the most recent pass (sanity: == 16 at full stress)
static u8  seq_core_render_max_dirty;     // PEAK tracks rendered in one pass over the window — race-free
                                          // "how many were armed and re-rendered together" (== 16 when a
                                          // live-input change dirties all gripped tracks; stays 0 in a
                                          // truly-static window where change-detection renders nothing)
static u32 seq_core_render_reset_ms;      // MIOS32_TIMESTAMP at last reset (window denominator)


/////////////////////////////////////////////////////////////////////////////
// Initialisation
// \param mode if 0: clear all parameters, if 1: don't clear global parameters which are stored in the MBSEQ_GC.V4 file
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Init(u32 mode)
{
  int i;

  // Enable the DWT cycle counter once for the render-cost probe (SEQ_CORE_RenderTracks).
  // Free-running at the core clock, read with a single load — conflict-free vs the TIM6
  // MIOS32_STOPWATCH that SEQ_STATISTICS already brackets this handler with. The enables
  // are idempotent OR-sets; we deliberately do NOT zero CYCCNT — the probe only reads
  // wrap-safe deltas, and clearing a free-running counter here (Init runs on every
  // new-session create) could underflow a render in flight on the +4 task.
  SEQ_CORE_SCB_DEMCR |= SEQ_CORE_SCB_DEMCR_TRCENA;
  SEQ_CORE_DWT_CTRL |= SEQ_CORE_DWT_CTRL_CYCCNTENA;
  seq_core_render_total_us = 0;
  seq_core_render_max_tick_cyc = 0;
  seq_core_render_max_track_cyc = 0;
  seq_core_render_tick_count = 0;
  seq_core_render_last_dirty = 0;
  seq_core_render_max_dirty = 0;
  seq_core_render_reset_ms = (u32)MIOS32_TIMESTAMP_Get();

  seq_core_trk_muted = 0;
  seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
  seq_core_trk_soloed = 0;
  seq_core_options.ALL = 0;
  if( mode == 0 ) {
    seq_core_options.INIT_CC = 0x80; // off
    seq_core_options.INIT_WITH_TRIGGERS = 1;
    seq_core_options.PASTE_CLR_ALL = 1;
    seq_core_options.PATTERN_MIXER_MAP_COUPLING = 0;
    seq_core_options.MIXER_LIVE_SEND = 1;
  }
  seq_core_steps_per_measure = 16-1;
  seq_core_steps_per_pattern = 16-1;
  seq_core_pattern_switch_margin_ms = 100; // FALLBACK only now: SEQ_PATTERN_Handler measures the real switch I/O into seq_core_pattern_switch_measured_ms and the effective margin tracks that (see SEQ_CORE_SwitchMarginMs). Covers FEARLESS writeback (SD write+read in the switch window). Heaviest user is now PHRASE RECALL: it writes back ALL dirty groups + reads 4 patterns inside this one window (vs the per-group switch's 1+1).
  seq_core_pattern_switch_measured_ms = 0; // no measurement yet (set by SEQ_PATTERN_Handler)
  seq_core_options.SWITCH_QUANTIZE_GRID = 0; // default Instant: grid==0 keeps the EXACT old pattern-boundary switching when synched is on (zero behaviour change / no regression). Dial up to quantize; the encoder/testctrl couples grid>0 -> SYNCHED on, grid 0 -> SYNCHED off.
  seq_core_recall_rephase_req = 0;
  seq_core_global_scale = 0;
  seq_core_global_scale_root_selection = 0; // from keyboard
  seq_core_keyb_scale_root = 0; // taken if enabled in OPT menu
  seq_core_global_transpose_enabled = 0;
  seq_core_din_sync_pulse_ctr = 0; // used to generate a 1 mS pulse

  if( mode == 0 ) {
    seq_core_metronome_port = DEFAULT;
    seq_core_metronome_chn = 10;
    seq_core_metronome_note_m = 0x25; // C#1
    seq_core_metronome_note_b = 0x25; // C#1
  }

  if( mode == 0 ) {
    seq_core_shadow_out_port = DEFAULT;
    seq_core_shadow_out_chn = 0; // means: off
  }

  seq_core_bpm_preset_num = 13; // 140.0
  for(i=0; i<SEQ_CORE_NUM_BPM_PRESETS; ++i) {
    seq_core_bpm_preset_tempo[i] = 75.0 + 5.0*i;
    seq_core_bpm_preset_ramp[i] = 0.0;
  }
  seq_core_bpm_sweep_inc = 0.0;

  seq_core_state.ALL = 0;

  seq_core_glb_loop_mode = SEQ_CORE_LOOP_MODE_ALL_TRACKS_VIEW;
  seq_core_glb_loop_offset = 0;
  seq_core_glb_loop_steps = 16-1;

  seq_core_step_update_req = 0;

  // Phase B: reset processor stacks to empty (NONE id, disabled). Explicit
  // write defeats DCE so the bss slot survives even though phase B's
  // renderer iteration is a no-op. Phase C will assign real ids here.
  {
    u8 track, slot;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
      for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
        seq_processor_stack[track][slot].id        = SEQ_PROCESSOR_ID_NONE;
        seq_processor_stack[track][slot].enabled   = 0;
        seq_processor_stack[track][slot].strength  = 0;
        seq_processor_stack[track][slot].bus       = 0;
        seq_processor_stack[track][slot].drum_mask = 0xFFFF;
      }
      seq_render_live_sig[track] = 0;  // change-detection baseline (first render stores fresh)
    }
  }

  // set initial seed of random generator
  SEQ_RANDOM_Gen(0xdeadbabe);

  // reset layers
  SEQ_LAYER_Init(0);

  // reset patterns
  SEQ_PATTERN_Init(0);

  // reset force-to-scale module
  SEQ_SCALE_Init(0);

  // reset groove module
  SEQ_GROOVE_Init(0);

  // reset morph module
  SEQ_MORPH_Init(0);

  // reset humanizer module
  SEQ_HUMANIZE_Init(0);


  // reset robotizer module
  SEQ_ROBOTIZE_Init(0);

  // reset generator pool (phase E)
  SEQ_GENERATOR_Init(0);

  // reset LFO module
  SEQ_LFO_Init(0);

  // reset song module
  SEQ_SONG_Init(0);

  // reset live play module
  SEQ_LIVE_Init(0);

  // reset record module
  SEQ_RECORD_Init(0);

  // init MIDI file player/exporter/importer
  SEQ_MIDPLY_Init(0);
  SEQ_MIDEXP_Init(0);
  SEQ_MIDIMP_Init(0);

  // Retroactive CAPTURE: install the passive output tee that feeds the while-playing
  // live tape. Always live (cheap fast-path guards); the re-sim redirect sink is a
  // separate callback, so the two never conflict.
  SEQ_MIDI_OUT_Callback_MIDI_Tap_Set(SEQ_CORE_CaptureTapeTap);

  // clear registers which are not reset by SEQ_CORE_Reset()
  u8 track;
  seq_core_trk_t *t = &seq_core_trk[0];
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t) {

    // if track name only contains spaces, the UI will print 
    // the track number instead of an empty message
    // this ensures highest flexibility (e.g. any track can 
    // play patterns normaly assigned for other tracks w/o inconsistent messages)
    // -> see SEQ_LCD_PrintTrackName()
    int i;
    for(i=0; i<80; ++i)
      t->name[i] = ' ';
    t->name[80] = 0;

    // clear glide note storage
    for(i=0; i<4; ++i)
      t->glide_notes[i] = 0;

    // don't select sections
    t->play_section = 0;
  }

  // reset sequencer
  SEQ_CORE_Reset(0);

  // reset MIDI player
  SEQ_MIDPLY_Reset();

  // init BPM generator
  SEQ_BPM_Init(0);

  SEQ_BPM_PPQN_Set(384);
  SEQ_CORE_BPM_Update(seq_core_bpm_preset_tempo[seq_core_bpm_preset_num], 0.0);

#if STOPWATCH_PERFORMANCE_MEASURING
  SEQ_STATISTICS_StopwatchInit();
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Render cache — identity copy + processor stack (phases A + B).
//
// SEQ_CORE_RenderDirtySet marks one track's output mirror stale; callers that
// mutate seq_par_layer_value / seq_trg_layer_value invoke this so the next
// tick refreshes the mirror.
//
// SEQ_CORE_RenderTrack copies source → output (identity, phase A), then
// iterates the per-track processor stack (phase B; empty in B, dispatch
// lands in C). The tick-side swap point (tick reads from *_output_value)
// is invariant across phases.
//
// SEQ_CORE_RenderTracks runs at the top of every SEQ_CORE_Tick — the
// "tick-prologue batch" trigger model. Guarantees output is current before
// any tick read.
/////////////////////////////////////////////////////////////////////////////
// Phase D.1 — bump the touched timestamp + dirty flag for one track. Called
// from any site that mutates the processor stack's inputs (slot params,
// processor enable/disable, bus assignment). The touch is the signal that
// switches the renderer into sweep regime for SEQ_RENDER_SWEEP_MS.
void SEQ_CORE_RenderTouched(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  // u32 wraps every ~50 days; the touched-window comparison uses
  // MIOS32_TIMESTAMP_GetDelay() which is wrap-safe via unsigned subtraction.
  // A timestamp of 0 is reserved as "never touched" → if Get() ever returns
  // 0 we'd misclassify a fresh touch as never-touched for one ms — clamp.
  u32 now = (u32)MIOS32_TIMESTAMP_Get();
  seq_render_touched_ms[track] = now ? now : 1;
  seq_render_dirty[track] = 1;
  if( !SEQ_BPM_IsRunning() )
    SEQ_CORE_RenderTrack(track);
}

// Phase D.1 — sweep regime if the track was touched within the last
// SEQ_RENDER_SWEEP_MS ms. Quiet otherwise. Called from the renderer to pick
// between sweep (bounded current+lookahead, D.2) and quiet (whole-buffer)
// paths. MIOS32_TIMESTAMP_GetDelay uses unsigned subtraction so the result
// is correct across the u32 wrap.
u8 SEQ_CORE_RenderSweeping(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return 0;
  u32 touched = seq_render_touched_ms[track];
  if( !touched )
    return 0;
  u32 delay = (u32)MIOS32_TIMESTAMP_GetDelay(touched);
  return (delay < SEQ_RENDER_SWEEP_MS) ? 1 : 0;
}

void SEQ_CORE_RenderDirtySet(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  seq_render_dirty[track] = 1;
  // When transport is stopped no SEQ_CORE_Tick fires, so the prologue batch
  // renderer never runs and UI reads (par/trg via SEQ_*_Get → output mirror)
  // stay stale. Flush synchronously so edits, CLEAR, GP toggles, and file
  // loads are immediately visible on the LCD.
  if( !SEQ_BPM_IsRunning() )
    SEQ_CORE_RenderTrack(track);
}

void SEQ_CORE_RenderDirtySetAll(void)
{
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    seq_render_dirty[track] = 1;
    if( !SEQ_BPM_IsRunning() )
      SEQ_CORE_RenderTrack(track);
  }
}

// Shared deterministic decision hash (defined further down beside TENSION).
// chord_mask sits above it in the file, so forward-declare it here.
static u8 grip_hash(u8 track, u8 instr, u16 step, u8 zone);
// grip_hash zone for CHORD_MASK — distinct from TENSION's zones (0..6, with
// pull collapsing to 0) so the two processors' gates decorrelate when both
// run on the same (track, instr, step).
#define GRIP_ZONE_CHORD_MASK  0x20

// Snap `note` to the nearest semitone whose pitch-class is set in `pc_mask`,
// searching outward from d=0; on a tie (down and up both in mask at the same
// distance) the lower side wins because it is checked first. Returns `note`
// unchanged if no PC is reachable within 6 semitones (cannot happen when
// pc_mask != 0, since every note has some PC ≤6 away).
static u8 chord_mask_snap(u8 note, u16 pc_mask)
{
  int d;
  for(d=0; d<=6; ++d) {
    int down = (int)note - d;
    if( down >= 0 && (pc_mask & (1 << (down % 12))) )
      return (u8)down;
    int up = (int)note + d;
    if( up <= 127 && (pc_mask & (1 << (up % 12))) )
      return (u8)up;
  }
  return note;
}

// chord_mask processor (§8 step 4, §3 spine). Rewrites note-bearing bytes in
// the par buffer pointed at by `par_buf` to snap toward the bus's currently-
// held chord (PC-set). Algorithm: probabilistic gate at p->strength, nearest-
// PC outward search, lower wins on tie. 0-valued bytes are skipped — for
// drums this preserves the "fall back to lay_const" idiom (§9 drum-Note-
// layer); for normal tracks it avoids inventing pitch on empty steps.
// Phase D.2: operates on [step_lo, step_hi). Whole-buffer = (0, num_p_steps).
static void chord_mask_render_range(u8 track, const seq_processor_slot_t *p,
                                    u8 *par_buf, u16 step_lo, u16 step_hi)
{
  if( !p->strength )
    return;
  u16 pc_mask = SEQ_MIDI_IN_BusPCSetGet(p->bus);
  if( !pc_mask )
    return;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  u8 num_p_layers      = SEQ_PAR_NumLayersGet(track);
  u16 num_p_steps      = SEQ_PAR_NumStepsGet(track);
  u8 num_p_instruments = SEQ_PAR_NumInstrumentsGet(track);
  if( step_hi > num_p_steps ) step_hi = num_p_steps;
  if( step_lo >= step_hi ) return;

  if( tcc->event_mode == SEQ_EVENT_MODE_Drum ) {
    s8 nl = tcc->link_par_layer_note;
    if( nl < 0 )
      return;
    u8 drum;
    for(drum=0; drum<num_p_instruments; ++drum) {
      // Phase G/step-7 polish: drum_mask scopes which drums the processor
      // touches. Bit i set = process drum i. Default 0xFFFF = all drums.
      if( !(p->drum_mask & (1u << drum)) )
        continue;
      u8 *base = &par_buf[(u32)drum*num_p_layers*num_p_steps + (u32)nl*num_p_steps];
      u16 step;
      for(step=step_lo; step<step_hi; ++step) {
        u8 note = base[step];
        if( !note )
          continue;
        if( grip_hash(track, drum, step, GRIP_ZONE_CHORD_MASK) < p->strength )
          base[step] = chord_mask_snap(note, pc_mask);
      }
    }
  } else {
    u8 par_layer;
    for(par_layer=0; par_layer<num_p_layers; ++par_layer) {
      if( tcc->lay_const[par_layer] != SEQ_PAR_Type_Note )
        continue;
      u8 *base = &par_buf[(u32)par_layer*num_p_steps];
      u16 step;
      for(step=step_lo; step<step_hi; ++step) {
        u8 note = base[step];
        if( !note )
          continue;
        if( grip_hash(track, par_layer, step, GRIP_ZONE_CHORD_MASK) < p->strength )
          base[step] = chord_mask_snap(note, pc_mask);
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
// Track 2 — pitch-chain migration (plan 2026-06-10). The emission-time pitch
// chain (SEQ_CORE_Transpose → force-to-scale; the note limit follows in
// Stage B) becomes a render-stack processor, so the output mirror holds the
// HEARD pitches: bounce is faithful by construction (no bake), and FTS sits
// upstream of CHORD_MASK/TENSION in slot order.
//
// Fenced at emission (NOT migrated — the legacy_pitch gate in SEQ_CORE_Tick):
//  - Arpeggiator playmode: multi-arp cycles t->arp_pos per emission — runtime
//    state a step-deterministic render cannot reproduce (user call 2026-06-10).
//  - Chord par layers: the par byte is a chord INDEX, not a note — pitch only
//    exists post-expansion, so the legacy emission chain keeps owning it.
//  - Drum event mode: a 0 Note byte is NOT a rest there (it falls back to the
//    per-drum lay_const note and still plays), so mirror-rest silence and the
//    skip-0 idiom are both wrong for drums — transposer-no-key would write 0s
//    and the kit would keep firing on DEFAULT notes instead of falling silent.
//    Fenced until drum pitch semantics get their own design pass (plan §4).
//
// Accepted semantic edges vs the emission chain (plan §2):
//  - A 0 Note byte stays a rest; "transpose note 0 (C-2) and play it" is
//    unrepresentable in the mirror.
//  - Transposer-with-no-key writes rests into the mirror (was velocity=0 at
//    emission) — same silence, now bounce-visible. The stretched-glide release
//    that velocity-0 events used to trigger is reproduced at emission (the
//    no-events branch after GetEvents).
//  - Morph blends already-pitched mirror values instead of pitching the
//    blend. NOT equivalent: a 50% blend of two in-scale notes can pass
//    through an off-scale value (C/D → C#) that legacy emission-FTS folded —
//    small audible edge on morphed FTS tracks, accepted (decide by ear).
//  - PitchBend under transpose: the wire MSB matches legacy; the LSB is now
//    derived from the shifted value (legacy kept the unshifted LSB) —
//    inaudible, byte-level only.
/////////////////////////////////////////////////////////////////////////////

// Per-step scale/root for the render-side FTS snap — mirrors
// SEQ_CORE_FTS_GetScaleAndRoot, but reads the Scale/Root par layers from the
// render buffer instead of SEQ_PAR_Get's output mirror (same values — no
// processor mutates non-Note layers). `instr_base` is the render par buffer
// (drum tracks are fenced — see section header — so addressing is layer-major).
static void pitch_step_scale_root(const seq_cc_trk_t *tcc, const u8 *instr_base,
                                  u16 num_steps, u16 step, u8 *scale, u8 *root)
{
  if( tcc->link_par_layer_scale >= 0 ) {
    u8 s = instr_base[(u32)tcc->link_par_layer_scale * num_steps + step];
    *scale = s ? (u8)(s - 1) : seq_core_global_scale;
  } else {
    *scale = seq_core_global_scale;
  }

  u8 root_selection = seq_core_global_scale_root_selection;
  u8 fallback_root = (root_selection == 0) ? seq_core_keyb_scale_root
                                           : (u8)(root_selection - 1);
  if( tcc->link_par_layer_root >= 0 ) {
    u8 r = instr_base[(u32)tcc->link_par_layer_root * num_steps + step] % 13;
    *root = r ? (u8)(r - 1) : fallback_root;
  } else {
    *root = fallback_root;
  }
}

// PITCH processor (Track 2). Reproduces SEQ_CORE_Transpose's Normal/Transpose/
// ChordMask-playmode semantics + the emission force-to-scale snap, writing the
// result into the render buffer:
//  - Note layers: static oct/semi (±8, ≥8-wrap signed encoding) + the live
//    transposer offset, TrimNote to 0..127, then the FTS snap (per-step
//    Scale/Root layers respected) when FORCE_SCALE is set.
//  - CC-ish layers (CC/PitchBend) in CC event mode: value shift only —
//    SEQ_CORE_Transpose's is_cc path. The global transpose never moves CC
//    values (its !is_cc gate), only Transpose playmode does.
// p->bus carries tcc->busasg.bus (the transposer bus — NOT the chord-context
// bus the CHORD_MASK/TENSION slots use). p->strength is not a dial here; the
// chain's CCs are the controls and 0-transpose+FTS-off disarms the slot.
static void pitch_render_range(u8 track, const seq_processor_slot_t *p,
                               u8 *par_buf, u16 step_lo, u16 step_hi)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  // Fenced cases (see section header) — the sync never arms these; belt and braces.
  if( tcc->playmode == SEQ_CORE_TRKMODE_Arpeggiator
      || tcc->event_mode == SEQ_EVENT_MODE_Drum )
    return;

  int inc_oct  = tcc->transpose_oct;   if( inc_oct  >= 8 ) inc_oct  -= 16;
  int inc_semi = tcc->transpose_semi;  if( inc_semi >= 8 ) inc_semi -= 16;
  int inc_static = 12*inc_oct + inc_semi;

  // Live transposer offset. Transpose playmode moves notes AND CC values; the
  // global transpose moves notes only. No key held → notes fall silent (rests
  // in the mirror), and in Transpose playmode CC values drop to 0 (emission
  // parity: velocity=0 aliases the CC value byte in the MIDI package).
  u8 transposer = (tcc->playmode == SEQ_CORE_TRKMODE_Transpose);
  u8 global_tr  = (!transposer && seq_core_global_transpose_enabled);
  u8 silence = 0;
  int tr_offset = 0;
  if( transposer || global_tr ) {
    int tr_note = SEQ_MIDI_IN_TransposerNoteGet(p->bus, tcc->trkmode_flags.HOLD,
                                                tcc->trkmode_flags.FIRST_NOTE);
    if( tr_note < 0 )
      silence = 1;
    else
      tr_offset = tr_note - 0x3c; // C-3 is the base note
  }
  int inc_note = inc_static + tr_offset;
  int inc_cc   = transposer ? inc_note : inc_static;

  u8 force_scale = tcc->trkmode_flags.FORCE_SCALE ? 1 : 0;

  u8 num_p_layers      = SEQ_PAR_NumLayersGet(track);
  u16 num_p_steps      = SEQ_PAR_NumStepsGet(track);
  if( step_hi > num_p_steps ) step_hi = num_p_steps;
  if( step_lo >= step_hi ) return;

  // CC + PitchBend carry the is_cc shift in CC event mode. ProgramChange and
  // Aftertouch are deliberately EXCLUDED although legacy "transposed" them:
  // seq_layer.c builds their wire bytes from evnt1, while the legacy shift
  // wrote evnt2 (a don't-care for PC/AT) — so the legacy shift never reached
  // the wire, and shifting the stored value here would invent audible program
  // changes / pressure jumps that never existed.
  u8 cc_mode = (tcc->event_mode == SEQ_EVENT_MODE_CC);
  u8 par_layer;
  for(par_layer=0; par_layer<num_p_layers; ++par_layer) {
    seq_par_layer_type_t lt = (seq_par_layer_type_t)tcc->lay_const[par_layer];
    u8 is_note = (lt == SEQ_PAR_Type_Note);
    u8 is_ccish = cc_mode && (lt == SEQ_PAR_Type_CC ||
                              lt == SEQ_PAR_Type_PitchBend);
    if( !is_note && !is_ccish )
      continue;
    u8 *base = &par_buf[(u32)par_layer*num_p_steps];
    u16 step;
    for(step=step_lo; step<step_hi; ++step) {
      if( is_note ) {
        u8 note = base[step];
        if( !note )
          continue; // rest
        if( silence ) { base[step] = 0; continue; }
        int n = (int)note + inc_note;
        n = SEQ_CORE_TrimNote(n, 0, 127);
        if( force_scale ) {
          u8 scale, root;
          pitch_step_scale_root(tcc, par_buf, num_p_steps, step, &scale, &root);
          n = SEQ_SCALE_NoteValueGet((u8)n, scale, root);
        }
        base[step] = (u8)n;
      } else {
        // is_cc path: value 0 shifts too — no rest semantics for CC values.
        if( transposer && silence ) { base[step] = 0; continue; }
        int n = (int)base[step] + inc_cc;
        n = SEQ_CORE_TrimNote(n, 0, 127);
        base[step] = (u8)n;
      }
    }
  }
}

// LIMIT processor (Track 2 Stage B). The final range fold — slot 3, AFTER
// TENSION, so a pushed note still lands inside the window (emission parity:
// the limit was "the last Fx in the chain"). Mirrors SEQ_CORE_Limit exactly:
// active when either bound is set, unset upper = 127, swapped bounds
// normalize, and SEQ_CORE_TrimNote OCTAVE-FOLDS (pitch class preserved)
// rather than clamping. The per-step no_fx TRG layer escapes the fold
// (emission parity), read from the trg buffer being built so a just-edited
// no_fx bit and this render stay consistent. The nth-trigger bar-variant of
// no_fx cannot exist in a bar-independent render — accepted edge (plan §4);
// nth still suppresses the emission-side FX (humanize/LFO/echo).
static void limit_render_range(u8 track, const seq_processor_slot_t *p,
                               u8 *par_buf, const u8 *trg_buf,
                               u16 step_lo, u16 step_hi)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  // Fenced cases (see the PITCH section header) — sync never arms these.
  if( tcc->playmode == SEQ_CORE_TRKMODE_Arpeggiator
      || tcc->event_mode == SEQ_EVENT_MODE_Drum )
    return;

  u8 lower = tcc->limit_lower;
  u8 upper = tcc->limit_upper;
  if( !lower && !upper )
    return;
  if( !upper )
    upper = 127;
  if( lower > upper ) { u8 tmp = upper; upper = lower; lower = tmp; }

  u8 num_p_layers = SEQ_PAR_NumLayersGet(track);
  u16 num_p_steps = SEQ_PAR_NumStepsGet(track);
  if( step_hi > num_p_steps ) step_hi = num_p_steps;
  if( step_lo >= step_hi ) return;

  // no_fx trg assignment: 0 = none, else layer index + 1 (SEQ_TRG_NoFxGet).
  u8 nofx_asg = tcc->trg_assignments.no_fx;
  u16 num_t_step8 = (u16)(SEQ_TRG_NumStepsGet(track) / 8);
  u8  num_t_layers = SEQ_TRG_NumLayersGet(track);  // bound the no_fx layer: OOB ⇒ no escape

  u8 par_layer;
  for(par_layer=0; par_layer<num_p_layers; ++par_layer) {
    if( (seq_par_layer_type_t)tcc->lay_const[par_layer] != SEQ_PAR_Type_Note )
      continue;
    u8 *base = &par_buf[(u32)par_layer*num_p_steps];
    u16 step;
    for(step=step_lo; step<step_hi; ++step) {
      u8 note = base[step];
      if( !note )
        continue; // rest
      // no_fx escape: bound the assignment to the track's real trg layers (SEQ_TRG_Get
      // parity). An OOB layer would index the stale [used,MAX) tail of the rendered trg
      // buffer (the render refreshes only the used region), so treat it as "no escape",
      // not garbage. The layer bound keeps ix < used <= MAX.
      if( nofx_asg && num_t_step8 && (u8)(nofx_asg - 1) < num_t_layers ) {
        u32 ix = (u32)(nofx_asg - 1) * num_t_step8 + (step >> 3);
        if( trg_buf[ix] & (1 << (step & 7)) )
          continue; // per-step no-fx escapes the fold (emission parity)
      }
      base[step] = (u8)SEQ_CORE_TrimNote(note, lower, upper);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
// Tension Workbench — the GRAVITY field (design doc §5; plan 2026-06-09).
//
// A ranked field over the 12 pitch classes with one bipolar dial (GRAVITY,
// −64..+63, center 0 = pass-through) moving notes along a stability gradient:
// pull (CCW) toward the chord root / scale, push (CW) toward structured
// dissonance. Reuses chord_mask_snap as the snap primitive (it is general over
// any 12-bit mask); the new code is the band-mask builder + a deterministic
// grip gate. GRAVITY is global; GRIP (how strongly a track is held) is per-
// track, mirrored into the slot's strength via SEQ_CORE_TensionSlotSync.
/////////////////////////////////////////////////////////////////////////////

// Slot homes (Track 2 re-home). Slot index = stack order: PITCH planes/tidies
// first, CHORD_MASK snaps over it (the chord wins over the scale), TENSION
// pushes/pulls, LIMIT folds the result back into range last — so a gravity
// push is no longer re-corrected by the FTS snap (the Track-1 "FTS off on
// gripped tracks" POC rule dissolves) but still respects the range window.
#define SEQ_CORE_PITCH_SLOT     0
#define SEQ_CORE_CHORDMASK_SLOT 1
#define SEQ_CORE_TENSION_SLOT   2
#define SEQ_CORE_LIMIT_SLOT     3

// 12-bit pitch-class barrel rotate (mod 12, masked to 0x0FFF — a raw u16 shift
// would leak bits across PC 11→12). Up = toward higher PC.
static inline u16 tension_rot12_up(u16 m, u8 n) {
  n %= 12; return (u16)(((m << n) | (m >> (12 - n))) & 0x0FFF);
}
static inline u16 tension_rot12_dn(u16 m, u8 n) {
  n %= 12; return (u16)(((m >> n) | (m << (12 - n))) & 0x0FFF);
}

// Deterministic per-note decision source (§2.3): a pure function of
// (track, instrument, step, zone) — same knob position ⇒ same gripped set on
// every render, so states are returnable and bounce is faithful with no bake.
// Integer key-mix + one xorshift32 step (the seq_random.c GenXorshift body);
// returns 0..126 to match the legacy SEQ_RANDOM_Gen_Range(0,126) bucketing.
// SHARED by TENSION (grip) and CHORD_MASK — the per-track-RNG keystone moved
// chord_mask off the global RNG onto this same hash so its gripped subset is
// stable per (track,step) instead of re-rolled (and flickering) every render,
// and so a captured span re-renders byte-identically (no global-RNG state).
// The `zone` axis decorrelates the two processors' gates on a shared
// (track,instr,step): TENSION uses 0..6 (pull collapses to 0), CHORD_MASK uses
// GRIP_ZONE_CHORD_MASK below.
static u8 grip_hash(u8 track, u8 instr, u16 step, u8 zone) {
  u32 h = (u32)track * 2654435761u
        + (u32)instr * 40503u
        + (u32)step  * 2246822519u
        + (u32)zone  * 3266489917u
        + 0x9e3779b9u;
  h ^= h << 13; h ^= h >> 17; h ^= h << 5;  // xorshift32 mix
  return (u8)(h % 127);
}

// §2.1 stability ladder + §2.2 zones. Builds the target PC band for the zone
// implied by `gravity`. Pull-side bands NEST (L0 ⊂ ≤L1 ⊂ ≤L2 ⊂ ≤L3) so the CCW
// sweep is monotone. Push-side bands are targets (in-band notes stay put — the
// snap returns them unchanged at d=0). Degrades gracefully with no chord held:
// L0/L1/L3 come from the global root + scale, so the field works solo.
u16 SEQ_CORE_TensionBandMask(s8 gravity, u8 bus, u8 *zone)
{
  // Resolve scale + root exactly as force-to-scale does from the globals.
  u8 scale = seq_core_global_scale;
  u8 root  = (seq_core_global_scale_root_selection == 0)
             ? seq_core_keyb_scale_root
             : (u8)(seq_core_global_scale_root_selection - 1);
  if( root >= 12 ) root = 0;

  // L0: chord root = lowest held note's PC (bass proxy), else the global root.
  s32 low = SEQ_MIDI_IN_BusLowestNoteGet(bus);
  u8 root_pc = (low >= 0) ? (u8)((u8)low % 12) : root;

  u16 L0  = (u16)(1u << root_pc);
  u16 L1  = (u16)(L0 | (1u << ((root_pc + 7) % 12)));   // + perfect fifth
  u16 L2c = SEQ_MIDI_IN_BusPCSetGet(bus);               // chord tones (0 = none)

  // Scale membership: PCs where the FTS snap is a fixed point (the table is a
  // snap map, not a bitmask — test at a fixed mid octave to dodge 0/127 clamp).
  u16 scaleMask = 0;
  {
    u8 pc;
    for(pc=0; pc<12; ++pc) {
      u8 n = 60 + pc;
      if( SEQ_SCALE_NoteValueGet(n, scale, root) == n )
        scaleMask |= (u16)(1u << pc);
    }
  }

  u16 leL2 = (u16)(L1 | L2c);
  u16 leL3 = (u16)(leL2 | scaleMask);

  if( gravity == 0 ) { *zone = 0; return 0; }           // detent → pass-through

  u16 band;
  u8  z;
  if( gravity < 0 ) {
    // Pull side — nested, monotone toward the root.
    if( gravity >= -24 )      { z = 3; band = leL3; }                       // SCALE
    else if( gravity >= -48 ) { z = 2; band = leL2; }                       // CHORD
    else                      { z = 1; band = (gravity <= -57) ? L0 : L1; } // DRONE
  } else {
    // Push side — tense targets.
    if( gravity <= 24 ) {                                                   // LEAN
      z = 4;
      u16 b = (u16)(leL3 & ~leL2);   // in-scale, non-chord (sus/add color)
      band = b ? b : leL3;
    } else if( gravity <= 48 ) {                                            // RUB
      z = 5;
      u16 anchor = L2c ? L2c : L1;
      u16 adj = (u16)(tension_rot12_up(anchor, 1) | tension_rot12_dn(anchor, 1));
      u16 L4  = (u16)(~leL3 & 0x0FFF);   // chromatic remainder
      u16 b   = (u16)(L4 & adj);         // chromatic neighbours of chord tones
      band = b ? b : (L4 ? L4 : leL3);
    } else {                                                               // SLIP
      z = 6;
      band = tension_rot12_up(L2c ? leL2 : L1, 1);  // chord planed +1 semitone
    }
  }
  *zone = z;
  return band;
}

// TENSION processor (§2.3). Parallels chord_mask_render_range, but swaps the
// live SEQ_RANDOM gate for the deterministic grip hash and feeds chord_mask_snap
// the GRAVITY band instead of the raw chord PC-set. p->strength carries the
// per-track GRIP; GRAVITY/scale are read from globals. Skips 0-valued bytes
// (drum lay_const fallback idiom). 0 GRIP or detent ⇒ untouched pass-through.
static void tension_render_range(u8 track, const seq_processor_slot_t *p,
                                 u8 *par_buf, u16 step_lo, u16 step_hi)
{
  if( !p->strength )                  // GRIP 0 → this track not held by the field
    return;
  s8 gravity = seq_core_tension_gravity;
  if( !gravity )                      // detent → byte-identical pass-through (§2.3)
    return;

  u8 zone;
  u16 band = SEQ_CORE_TensionBandMask(gravity, p->bus, &zone);
  if( !band )                         // nothing legal to snap to → pass-through
    return;

  // Threshold: |gravity| (1..64) scaled by GRIP (0..127), /64 → 0..127. At full
  // pull + full grip every note grips; light grip / shallow gravity → few.
  u8  abs_g = (gravity < 0) ? (u8)(-(s16)gravity) : (u8)gravity;
  u32 thr = ((u32)abs_g * (u32)p->strength) >> 6;
  if( thr > 127 ) thr = 127;
  if( !thr )
    return;

  // Grip-hash zone key. PULL collapses ALL into one class (0) so the gripped set
  // only GROWS as |gravity| rises (thr ↑, band nests ⊆) — pulling harder can
  // only move a voice further down the ladder, never pop it back out (§2.2
  // monotone pull / §8.1 "collapse, not dropout"). PUSH keeps the per-zone id so
  // LEAN/RUB/SLIP each select a different tense set (§2.3 variety across zones).
  u8 hash_zone = (gravity < 0) ? 0 : zone;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  u8 num_p_layers      = SEQ_PAR_NumLayersGet(track);
  u16 num_p_steps      = SEQ_PAR_NumStepsGet(track);
  u8 num_p_instruments = SEQ_PAR_NumInstrumentsGet(track);
  if( step_hi > num_p_steps ) step_hi = num_p_steps;
  if( step_lo >= step_hi ) return;

  if( tcc->event_mode == SEQ_EVENT_MODE_Drum ) {
    s8 nl = tcc->link_par_layer_note;
    if( nl < 0 )
      return;
    u8 drum;
    for(drum=0; drum<num_p_instruments; ++drum) {
      if( !(p->drum_mask & (1u << drum)) )
        continue;
      u8 *base = &par_buf[(u32)drum*num_p_layers*num_p_steps + (u32)nl*num_p_steps];
      u16 step;
      for(step=step_lo; step<step_hi; ++step) {
        u8 note = base[step];
        if( !note )
          continue;
        if( grip_hash(track, drum, step, hash_zone) < thr )
          base[step] = chord_mask_snap(note, band);
      }
    }
  } else {
    u8 par_layer;
    for(par_layer=0; par_layer<num_p_layers; ++par_layer) {
      if( tcc->lay_const[par_layer] != SEQ_PAR_Type_Note )
        continue;
      u8 *base = &par_buf[(u32)par_layer*num_p_steps];
      u16 step;
      for(step=step_lo; step<step_hi; ++step) {
        u8 note = base[step];
        if( !note )
          continue;
        if( grip_hash(track, par_layer, step, hash_zone) < thr )
          base[step] = chord_mask_snap(note, band);
      }
    }
  }
}

// Phase D.2 — sweep-window render. Recopies source → ACTIVE output buffer
// for a bounded [step_lo, step_hi) slice across every par/trg layer/drum,
// then runs the processor stack on the same slice. Writes directly to the
// active half-buffer (tearing during knob motion is acceptable; the tick
// only reads the slice we just wrote). Dirty stays set so the next tick re-
// evaluates: still sweeping → another slice render; quiet now → full catch-up
// quiet render fires and clears dirty.
static void sweep_window_render(u8 track)
{
  u16 num_p_steps = (u16)SEQ_PAR_NumStepsGet(track);
  if( !num_p_steps )
    return;
  u16 step_lo = (u16)(seq_core_trk[track].step % num_p_steps);
  u16 step_hi = step_lo + SEQ_RENDER_SWEEP_LOOKAHEAD;
  if( step_hi > num_p_steps ) step_hi = num_p_steps;
  u16 step_count = step_hi - step_lo;
  if( !step_count )
    return;

  u8 *par_buf = SEQ_PAR_OutputActive(track);
  u8 *trg_buf = SEQ_TRG_OutputActive(track);

  // Par window copy across every (instrument, layer).
  u8 num_p_layers = (u8)SEQ_PAR_NumLayersGet(track);
  u8 num_p_instr  = (u8)SEQ_PAR_NumInstrumentsGet(track);
  u8 instr, layer;
  for(instr=0; instr<num_p_instr; ++instr) {
    for(layer=0; layer<num_p_layers; ++layer) {
      u32 base = (u32)instr * num_p_layers * num_p_steps + (u32)layer * num_p_steps;
      memcpy(&par_buf[base + step_lo],
             &seq_par_layer_value[track][base + step_lo],
             step_count);
    }
  }

  // Trg window copy — step_lo/step_hi project onto step8 byte addressing.
  u16 num_t_step8 = (u16)(SEQ_TRG_NumStepsGet(track) / 8);
  if( num_t_step8 ) {
    u16 step8_lo = step_lo / 8;
    u16 step8_hi = (step_hi + 7) / 8;
    if( step8_hi > num_t_step8 ) step8_hi = num_t_step8;
    u16 step8_count = (step8_hi > step8_lo) ? (step8_hi - step8_lo) : 0;
    if( step8_count ) {
      u8 num_t_layers = (u8)SEQ_TRG_NumLayersGet(track);
      u8 num_t_instr  = (u8)SEQ_TRG_NumInstrumentsGet(track);
      for(instr=0; instr<num_t_instr; ++instr) {
        for(layer=0; layer<num_t_layers; ++layer) {
          u32 base = (u32)instr * num_t_layers * num_t_step8 + (u32)layer * num_t_step8;
          memcpy(&trg_buf[base + step8_lo],
                 &seq_trg_layer_value[track][base + step8_lo],
                 step8_count);
        }
      }
    }
  }

  // Processor stack on the window slice.
  u8 slot;
  for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
    const seq_processor_slot_t *p = &seq_processor_stack[track][slot];
    if( p->id == SEQ_PROCESSOR_ID_NONE || !p->enabled )
      continue;
    switch( p->id ) {
      case SEQ_PROCESSOR_ID_PITCH:
        pitch_render_range(track, p, par_buf, step_lo, step_hi);
        break;
      case SEQ_PROCESSOR_ID_CHORD_MASK:
        chord_mask_render_range(track, p, par_buf, step_lo, step_hi);
        break;
      case SEQ_PROCESSOR_ID_TENSION:
        tension_render_range(track, p, par_buf, step_lo, step_hi);
        break;
      case SEQ_PROCESSOR_ID_LIMIT:
        limit_render_range(track, p, par_buf, trg_buf, step_lo, step_hi);
        break;
      default:
        break;
    }
  }
}

// Bytes a track actually occupies in its par / trg layer buffer (instr*layers*steps). The
// per-tick quiet render and the capture primitive copy only this region, not the whole MAX
// buffer — a 16-step 1-layer track uses ~16 of 1024 par bytes. Clamped to MAX (the layout
// invariant keeps used <= MAX; the clamp guarantees we never copy past the buffer).
static u32 par_used_bytes(u8 track)
{
  u32 used = (u32)SEQ_PAR_NumInstrumentsGet(track) * SEQ_PAR_NumLayersGet(track)
           * SEQ_PAR_NumStepsGet(track);
  return (used > SEQ_PAR_MAX_BYTES) ? SEQ_PAR_MAX_BYTES : used;
}

static u32 trg_used_bytes(u8 track)
{
  u32 used = (u32)SEQ_TRG_NumInstrumentsGet(track) * SEQ_TRG_NumLayersGet(track)
           * (((u32)SEQ_TRG_NumStepsGet(track) + 7) / 8);
  return (used > SEQ_TRG_MAX_BYTES) ? SEQ_TRG_MAX_BYTES : used;
}

void SEQ_CORE_RenderTrack(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  if( !seq_render_dirty[track] )
    return;

  if( SEQ_CORE_RenderSweeping(track) ) {
    // Sweep regime — partial slice into active half, no flip, keep dirty so
    // the next tick re-runs the sweep until the touched timestamp expires.
    sweep_window_render(track);
    return;
  }

  // Quiet regime — full render into the inactive half, then atomic flip. Copy only the
  // bytes this track actually uses (instr*layers*steps), NOT the whole MAX buffer: a
  // 16-step 1-layer track uses ~16 of 1024 par bytes, yet this copy runs EVERY tick for
  // force-dirty (gravity/chord_mask/live-pitch) tracks — the full-MAX copy was the bulk
  // of the all-16 render cost that pegs the CPU and starves the lower-priority +2 UI task
  // (UI goes dark under a heavy GRAVITY field). Reads (SEQ_PAR_Get / SEQ_TRG_Get) never
  // index past the used region, so the stale tail left in the inactive buffer is never
  // observed; the processor passes below also stay within [0, num_p_steps). Clamp to MAX
  // defensively (the layout invariant keeps used <= MAX, but never copy past the buffer).
  u16 num_p_steps = (u16)SEQ_PAR_NumStepsGet(track);
  u8 *par_buf = SEQ_PAR_OutputInactive(track);
  u8 *trg_buf = SEQ_TRG_OutputInactive(track);
  memcpy(par_buf, seq_par_layer_value[track], par_used_bytes(track));
  memcpy(trg_buf, seq_trg_layer_value[track], trg_used_bytes(track));

  // Iterate processor stack on the inactive half. Empty/disabled slots
  // short-circuit; ordering within a track is slot index.
  u8 slot;
  for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
    const seq_processor_slot_t *p = &seq_processor_stack[track][slot];
    if( p->id == SEQ_PROCESSOR_ID_NONE || !p->enabled )
      continue;
    switch( p->id ) {
      case SEQ_PROCESSOR_ID_PITCH:
        pitch_render_range(track, p, par_buf, 0, num_p_steps);
        break;
      case SEQ_PROCESSOR_ID_CHORD_MASK:
        chord_mask_render_range(track, p, par_buf, 0, num_p_steps);
        break;
      case SEQ_PROCESSOR_ID_TENSION:
        tension_render_range(track, p, par_buf, 0, num_p_steps);
        break;
      case SEQ_PROCESSOR_ID_LIMIT:
        limit_render_range(track, p, par_buf, trg_buf, 0, num_p_steps);
        break;
      default:
        break;
    }
  }

  // Single-byte XOR — atomic on Cortex-M, so the tick path never sees a half-
  // rendered output. After this point SEQ_*_OutputActive() returns the just-
  // populated buffer.
  seq_render_active_buf[track] ^= 1;
  seq_render_dirty[track] = 0;
}

// Phase C bridge: the TRKMODE_ChordMask playmode + CHORDMASK_* CCs remain the
// persistent storage (v2 pattern format unchanged) and the user-facing controls
// (§9 known-musical shipping UX). This helper keeps slot 0 mirrored from tcc
// whenever the relevant fields change — so the renderer reads slot params,
// while the UI/CC layer keeps writing tcc as before. When chord_mask is the
// only processor a track carries, slot 0 is the conventional home; the proper
// allocator arrives with phase E.
//
// Phase G/step-7 polish: bus + drum_mask are per-processor (independent of
// tcc->busasg.bus). Sourcing the bus from tcc->chordmask_bus untangles the
// processor's chord-listening from the track's own bus assignment — a track
// can play on bus 0 while its chord_mask reads chord context from bus 1.
void SEQ_CORE_ChordMaskSlotSync(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_processor_slot_t *slot = &seq_processor_stack[track][SEQ_CORE_CHORDMASK_SLOT];

  if( tcc->playmode == SEQ_CORE_TRKMODE_ChordMask ) {
    slot->id        = SEQ_PROCESSOR_ID_CHORD_MASK;
    slot->enabled   = 1;
    slot->strength  = tcc->chordmask_strength;
    slot->bus       = tcc->chordmask_bus;
    slot->drum_mask = ((u16)tcc->chordmask_drum_h << 8) | (u16)tcc->chordmask_drum_l;
    SEQ_CORE_RenderTouched(track);
  } else if( slot->id == SEQ_PROCESSOR_ID_CHORD_MASK ) {
    slot->id        = SEQ_PROCESSOR_ID_NONE;
    slot->enabled   = 0;
    slot->strength  = 0;
    slot->bus       = 0;
    slot->drum_mask = 0xFFFF;
    SEQ_CORE_RenderTouched(track);
  }
}

// Tension Workbench (§3): the TENSION slot mirrors tcc->tension_grip (GRIP) into
// slot->strength, and shares the chord-context bus + drum scope with chord_mask
// (tcc->chordmask_bus / chordmask_drum_*). Enabled iff GRIP > 0. GRAVITY/SHADE
// are globals read at render time, so they need no per-track mirror — only GRIP
// gates whether a track is held by the field. Called from SEQ_CC_Set on a GRIP
// write, and on a chordmask_bus/drum change (so the shared context stays synced).
void SEQ_CORE_TensionSlotSync(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_processor_slot_t *slot = &seq_processor_stack[track][SEQ_CORE_TENSION_SLOT];

  if( tcc->tension_grip ) {
    slot->id        = SEQ_PROCESSOR_ID_TENSION;
    slot->enabled   = 1;
    slot->strength  = tcc->tension_grip;
    slot->bus       = tcc->chordmask_bus;
    slot->drum_mask = ((u16)tcc->chordmask_drum_h << 8) | (u16)tcc->chordmask_drum_l;
    SEQ_CORE_RenderTouched(track);
  } else if( slot->id == SEQ_PROCESSOR_ID_TENSION ) {
    slot->id        = SEQ_PROCESSOR_ID_NONE;
    slot->enabled   = 0;
    slot->strength  = 0;
    slot->bus       = 0;
    slot->drum_mask = 0xFFFF;
    SEQ_CORE_RenderTouched(track);
  }
}

// Track 2 bridge (the chord-mask phase-C pattern): tcc stays the persistent
// truth + the user-facing surface (TRKMODE/transpose/FTS pages and their CCs);
// the PITCH slot mirrors it. Armed only when the chain is non-neutral — an
// armed slot costs a render whenever dirty, and the live-varying cases
// (Transpose playmode / global transpose) force one every tick via
// SEQ_CORE_RenderTracks. Arpeggiator playmode never arms (fenced at emission).
void SEQ_CORE_PitchSlotSync(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_processor_slot_t *slot = &seq_processor_stack[track][SEQ_CORE_PITCH_SLOT];

  // NOTE: seq_core_global_transpose_enabled has no runtime writer on this fork
  // (init-0 only) — if one ever appears, it must re-sync ALL tracks, since a
  // neutral track won't arm on the toggle otherwise.
  u8 active = 0;
  if( tcc->playmode != SEQ_CORE_TRKMODE_Arpeggiator
      && tcc->event_mode != SEQ_EVENT_MODE_Drum ) {  // fenced cases (section header)
    active = (tcc->playmode == SEQ_CORE_TRKMODE_Transpose)
          || tcc->transpose_oct || tcc->transpose_semi
          || tcc->trkmode_flags.FORCE_SCALE
          || seq_core_global_transpose_enabled;
  }

  if( active ) {
    slot->id        = SEQ_PROCESSOR_ID_PITCH;
    slot->enabled   = 1;
    slot->strength  = 127;             // not a dial — the chain's CCs are the controls
    slot->bus       = tcc->busasg.bus; // transposer bus (NOT the chord context)
    slot->drum_mask = 0xFFFF;          // legacy chain was whole-track
    SEQ_CORE_RenderTouched(track);
  } else if( slot->id == SEQ_PROCESSOR_ID_PITCH ) {
    slot->id        = SEQ_PROCESSOR_ID_NONE;
    slot->enabled   = 0;
    slot->strength  = 0;
    slot->bus       = 0;
    slot->drum_mask = 0xFFFF;
    SEQ_CORE_RenderTouched(track);
  }
}

// Track 2 Stage B bridge: the LIMIT slot mirrors tcc->limit_lower/upper. Same
// shape as the PITCH sync: armed iff non-neutral, arp/drum fenced, tcc stays
// the persistent truth + the FX_LIMIT page's surface. Purely source-state
// (no live input), so it renders on events only — never implicit-dirty.
void SEQ_CORE_LimitSlotSync(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_processor_slot_t *slot = &seq_processor_stack[track][SEQ_CORE_LIMIT_SLOT];

  u8 active = 0;
  if( tcc->playmode != SEQ_CORE_TRKMODE_Arpeggiator
      && tcc->event_mode != SEQ_EVENT_MODE_Drum ) {
    active = (tcc->limit_lower || tcc->limit_upper);
  }

  if( active ) {
    slot->id        = SEQ_PROCESSOR_ID_LIMIT;
    slot->enabled   = 1;
    slot->strength  = 127;    // not a dial — the limit CCs are the controls
    slot->bus       = 0;
    slot->drum_mask = 0xFFFF;
    SEQ_CORE_RenderTouched(track);
  } else if( slot->id == SEQ_PROCESSOR_ID_LIMIT ) {
    slot->id        = SEQ_PROCESSOR_ID_NONE;
    slot->enabled   = 0;
    slot->strength  = 0;
    slot->bus       = 0;
    slot->drum_mask = 0xFFFF;
    SEQ_CORE_RenderTouched(track);
  }
}

// PITCH is implicit-dirty (per tick) only when its inputs are live — the
// transposer bus / global transpose can change between any two ticks without a
// CC write. Static transpose+FTS render on events instead (CC writes touch via
// the sync, par edits via SEQ_PAR_Set's auto-dirty, global scale/root changes
// via RenderDirtySetAll at the write sites) — re-rendering every armed track
// per tick would make every FTS track a full per-tick render for no live input.
static u8 pitch_slot_live(u8 track)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  return (tcc->playmode == SEQ_CORE_TRKMODE_Transpose) || seq_core_global_transpose_enabled;
}

// Set the global GRAVITY dial (clamped −64..+63) and re-render the tracks the
// field holds. Touches them (sweep regime) so a live knob sweep stays smooth
// while playing; when stopped, RenderTouched renders synchronously so the LCD/
// audition reflect it. The cockpit GRAVITY encoder calls this. (The testctrl
// CMD_TENSION_SET uses RenderDirtySetAll instead — it must make every track
// reflect the value for offline pinning regardless of which tracks are gripped.)
void SEQ_CORE_TensionGravitySet(s8 gravity)
{
  if( gravity < -64 ) gravity = -64;
  if( gravity >  63 ) gravity =  63;
  seq_core_tension_gravity = gravity;

  u8 track, slot;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
      const seq_processor_slot_t *p = &seq_processor_stack[track][slot];
      if( p->id == SEQ_PROCESSOR_ID_TENSION && p->enabled ) {
        SEQ_CORE_RenderTouched(track);
        break;
      }
    }
  }
}

// RESOLVE (§3): a bar-quantized ramp of GRAVITY to the detent, landing exactly
// on the next musical-measure downbeat — tension resolved into the One. The
// glide runs in the per-tick prologue (SEQ_CORE_TensionResolveTick); a hard pin
// at the ref_step==0 boundary (SEQ_CORE_Tick) guarantees the exact 0 landing.
// Resolution timing is most of what separates an instrument from an effect
// (Meyer/Huron), so the landing is quantized, not instantaneous. When the
// transport is stopped there is no measure to ramp into, so RESOLVE snaps now.
static u8  tension_resolve_active;
static s32 tension_resolve_q8;       // GRAVITY × 256 during the glide (fixed point)
static s32 tension_resolve_step_q8;  // per-tick magnitude toward 0 (0 = compute on first tick)

void SEQ_CORE_TensionResolveCancel(void)
{
  tension_resolve_active = 0;
}

void SEQ_CORE_TensionResolve(void)
{
  if( seq_core_tension_gravity == 0 ) {     // already home
    tension_resolve_active = 0;
    return;
  }
  if( !SEQ_BPM_IsRunning() ) {              // no bar to ramp over → resolve now
    tension_resolve_active = 0;
    SEQ_CORE_TensionGravitySet(0);
    return;
  }
  tension_resolve_q8      = (s32)seq_core_tension_gravity << 8;
  tension_resolve_step_q8 = 0;              // sized on the first ramp tick
  tension_resolve_active  = 1;
}

// Per-tick prologue glide. Sizes the step on the first tick from the ticks
// remaining to the next downbeat, then walks GRAVITY toward 0 each tick. The
// boundary pin in SEQ_CORE_Tick finalizes the exact landing + clears active.
void SEQ_CORE_TensionResolveTick(u32 bpm_tick)
{
  if( !tension_resolve_active )
    return;

  if( tension_resolve_step_q8 == 0 ) {
    // 384 ppqn → 96 ticks per 16th-note. seq_core_steps_per_measure is (steps−1),
    // ref_step is the current 0-based step in the measure; the next downbeat is
    // (steps_per_measure+1 − ref_step) sixteenths out, less our place in this one.
    u32 sixteenth   = bpm_tick % 96;
    u32 steps_left  = (u32)seq_core_steps_per_measure + 1 - seq_core_state.ref_step;
    s32 ticks_to_dn = (s32)(steps_left * 96) - (s32)sixteenth;
    if( ticks_to_dn < 1 ) ticks_to_dn = 1;
    s32 mag = (tension_resolve_q8 < 0) ? -tension_resolve_q8 : tension_resolve_q8;
    // round up so the glide fully reaches 0 by the downbeat (then holds, clamped)
    // rather than undershooting and leaving a residual for the boundary pin.
    tension_resolve_step_q8 = (mag + ticks_to_dn - 1) / ticks_to_dn;
    if( tension_resolve_step_q8 < 1 ) tension_resolve_step_q8 = 1;
  }

  if( tension_resolve_q8 > 0 ) {
    tension_resolve_q8 -= tension_resolve_step_q8;
    if( tension_resolve_q8 < 0 ) tension_resolve_q8 = 0;
  } else {
    tension_resolve_q8 += tension_resolve_step_q8;
    if( tension_resolve_q8 > 0 ) tension_resolve_q8 = 0;
  }

  s8 g = (s8)(tension_resolve_q8 >> 8);
  if( g != seq_core_tension_gravity )
    SEQ_CORE_TensionGravitySet(g);  // set + touch field tracks (does not cancel resolve)
}

// Called from the ref_step==0 measure boundary: pin GRAVITY to exactly 0 and
// end the ramp. Returns 1 if a resolve landed (so the tick can re-render).
u8 SEQ_CORE_TensionResolveBoundary(void)
{
  if( !tension_resolve_active )
    return 0;
  tension_resolve_active = 0;
  tension_resolve_q8 = 0;
  if( seq_core_tension_gravity != 0 )
    SEQ_CORE_TensionGravitySet(0);
  return 1;
}

/////////////////////////////////////////////////////////////////////////////
// Fork: capture engine — the shared primitive behind every "bounce"/capture
// verb. Snapshots a track's rendered output (the lossless, exact post-
// processor par/trg, CC layers included) into caller-provided buffers.
//
// Forcing a full *quiet* render first is what makes capture sweep-safe: a
// recent dial touch leaves the renderer in sweep regime, where only a ~4-step
// slice of OutputActive is fresh; resetting touched_ms + dirty and re-rendering
// guarantees a whole-buffer catch-up. The touched_ms reset is benign — the
// next dial touch re-arms sweep.
//
// Runs in task context; does no MIDI/SD I/O, so it needs no mutex of its own
// (the render's active-buffer flip is a single-byte atomic — the tick never
// sees a half-rendered buffer). par_dst must be >= SEQ_PAR_MAX_BYTES, trg_dst
// >= SEQ_TRG_MAX_BYTES.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_CaptureTrackOutput(u8 track, u8 *par_dst, u8 *trg_dst)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( par_dst == NULL || trg_dst == NULL ) return -1;

  // Full quiet render → OutputActive current across the track's used region.
  seq_render_touched_ms[track] = 0;
  seq_render_dirty[track] = 1;
  SEQ_CORE_RenderTrack(track);

  // The render refreshes only [0, used); the [used, MAX) tail is unused by any track read
  // (reads are bounded to the layout). Snapshot the fresh rendered region from the output
  // mirror, then fill the tail from the SOURCE so a whole-buffer snapshot is byte-identical
  // to the historical full-output copy (back when the render mirrored the whole buffer).
  // When par_dst IS the track's own source (in-place ProcessorBounce) the tail is already
  // correct, so the guarded copy skips it (and dodges a self-overlapping memcpy).
  u32 pu = par_used_bytes(track);
  u32 tu = trg_used_bytes(track);
  memcpy(par_dst, SEQ_PAR_OutputActive(track), pu);
  memcpy(trg_dst, SEQ_TRG_OutputActive(track), tu);
  if( pu < SEQ_PAR_MAX_BYTES && par_dst != seq_par_layer_value[track] )
    memcpy(par_dst + pu, &seq_par_layer_value[track][pu], SEQ_PAR_MAX_BYTES - pu);
  if( tu < SEQ_TRG_MAX_BYTES && trg_dst != seq_trg_layer_value[track] )
    memcpy(trg_dst + tu, &seq_trg_layer_value[track][tu], SEQ_TRG_MAX_BYTES - tu);
  return 0;
}


// In-place processor freeze: commit the post-processor output into the source
// par/trg layer and clear every enabled processor slot, so the freshly written
// source *is* the bounced material and subsequent renders are identity
// (output ≡ source). Returns 1 if a bounce occurred, 0 if nothing was enabled.
//
// The output→source copy goes through SEQ_CORE_CaptureTrackOutput, which forces
// a full quiet render first — without it, a recent dial touch (sweep regime)
// would freeze a buffer where only a 4-step slice reflects the live dial value.
//
// The chord_mask processor is doubly-bound: slot 0 mirrors tcc->playmode +
// tcc->chordmask_strength via SEQ_CORE_ChordMaskSlotSync. Clearing the slot
// alone would let the next SlotSync re-enable it, so we also reset
// playmode → Normal and chordmask_strength → 0.
s32 SEQ_CORE_ProcessorBounce(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0;

  // Anything enabled to bounce? If not, leave source untouched and report 0.
  u8 any_enabled = 0;
  u8 slot;
  for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
    const seq_processor_slot_t *p = &seq_processor_stack[track][slot];
    if( p->id != SEQ_PROCESSOR_ID_NONE && p->enabled ) {
      any_enabled = 1;
      break;
    }
  }
  if( !any_enabled )
    return 0;

  // Commit output → source (full-buffer, sweep-safe via the primitive).
  SEQ_CORE_CaptureTrackOutput(track, seq_par_layer_value[track], seq_trg_layer_value[track]);

  // Clear every slot.
  for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
    seq_processor_stack[track][slot].id        = SEQ_PROCESSOR_ID_NONE;
    seq_processor_stack[track][slot].enabled   = 0;
    seq_processor_stack[track][slot].strength  = 0;
    seq_processor_stack[track][slot].bus       = 0;
    seq_processor_stack[track][slot].drum_mask = 0xFFFF;
  }

  // Untangle the chord_mask tcc mirror so the next SlotSync doesn't re-arm.
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->playmode == SEQ_CORE_TRKMODE_ChordMask )
    tcc->playmode = SEQ_CORE_TRKMODE_Normal;
  tcc->chordmask_strength = 0;
  tcc->chordmask_bus      = 0;
  tcc->chordmask_drum_l   = 0xFF;
  tcc->chordmask_drum_h   = 0xFF;

  // Track 2: PITCH, TENSION and LIMIT are doubly-bound the same way — the
  // bounced source already holds their output, so a re-armed slot would
  // re-apply it (double-transpose / re-snap / re-grip; the limit re-fold is
  // idempotent but the posture should clear like the others — matches
  // SEQ_CC_ResetGenerativeForBounce on the capture path).
  if( tcc->playmode == SEQ_CORE_TRKMODE_Transpose )
    tcc->playmode = SEQ_CORE_TRKMODE_Normal;
  tcc->transpose_oct  = 0;
  tcc->transpose_semi = 0;
  tcc->trkmode_flags.FORCE_SCALE = 0;
  tcc->tension_grip = 0;
  tcc->limit_lower = 0;
  tcc->limit_upper = 0;

  // Re-render so the output mirror equals the new source (identity copy).
  SEQ_CORE_RenderTouched(track);
  seq_render_dirty[track] = 1;
  return 1;
}


/////////////////////////////////////////////////////////////////////////////
// (Track 2, 2026-06-10: SEQ_CORE_BakeForceScale lived here. The pitch chain
//  — transpose, force-to-scale, note limit — renders into the output mirror
//  now, so every capture verb copies the HEARD pitch by construction and the
//  per-effect bake program is over. Arp playmode is fenced at emission; its
//  capture was never pitch-faithful, with or without the bake.)
/////////////////////////////////////////////////////////////////////////////


// Static snapshot buffers for the capture-to-slot verb: preserve the source
// track's RAM across the slot write without an SD round-trip (a non-zero
// remix_map silently skips the source track on reload, and play_section lives
// outside the pattern file). One track's worth; the verb runs in task context
// and is not re-entrant.
static seq_cc_trk_t capture_cc_snapshot;
static u8           capture_par_snapshot[SEQ_PAR_MAX_BYTES];
static u8           capture_trg_snapshot[SEQ_TRG_MAX_BYTES];
static char         capture_name_snapshot[20];

/////////////////////////////////////////////////////////////////////////////
// Fork: capture a track's computed output into a destination pattern slot.
//
// Lossless replacement for the removed emission-tape bounce. The source track's
// OutputActive (exact par/trg, CC layers included, precise lengths — none of
// the tape's NoteOff-heuristic / CC-drop / grid-snap loss) is written into the
// (bank, pattern) slot. The source track's live RAM is snapshot and restored
// byte-identical, so capture never disturbs what is playing.
//
// A pattern slot stores a whole group of tracks, so the other tracks in
// src_group are written in their current live state; only src_track is the
// frozen capture. The destination's generative CC is reset
// (SEQ_CC_ResetGenerativeForBounce) so the frozen output isn't re-modulated on
// playback, and the name gets a "BNC" prefix.
//
// MUST run in task context (takes MUTEX_SDCARD around the write).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_CaptureToSlot(u8 src_track, u8 dst_group, u8 dst_bank, u8 dst_pattern)
{
  if( src_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_group >= SEQ_CORE_NUM_GROUPS ) return -1;

  u8 src_group = src_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
  s32 status;

  // 1. Snapshot source RAM (restored byte-identical at the end).
  u8 play_section_snapshot = seq_core_trk[src_track].play_section;
  memcpy(&capture_cc_snapshot, &seq_cc_trk[src_track], sizeof(seq_cc_trk_t));
  memcpy(capture_par_snapshot, seq_par_layer_value[src_track], SEQ_PAR_MAX_BYTES);
  memcpy(capture_trg_snapshot, seq_trg_layer_value[src_track], SEQ_TRG_MAX_BYTES);
  memcpy(capture_name_snapshot, seq_pattern_name[src_group], 20);

  // 2. Overwrite the source layers with the track's computed output
  //    (forced quiet render inside the primitive → sweep-safe, lossless).
  SEQ_CORE_CaptureTrackOutput(src_track, seq_par_layer_value[src_track], seq_trg_layer_value[src_track]);

  // (Track 2 closed the bake program: the pitch chain renders into the mirror,
  //  so the capture above already holds the heard pitches — SEQ_CORE_BakeForceScale
  //  is gone. Bounce correctness is a property of the architecture now.)

  // 3. Sanitize generative CC so the frozen slot plays back as tape, not
  //    re-modulated material.
  SEQ_CC_ResetGenerativeForBounce(src_track);

  // 4. BNC name tag so bounced slots are visually distinct in the picker.
  seq_pattern_name[src_group][0] = 'B';
  seq_pattern_name[src_group][1] = 'N';
  seq_pattern_name[src_group][2] = 'C';

  // 5. Write the mutated RAM (still labelled src_group) to the dst slot.
  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_PatternWrite(seq_file_session_name, dst_bank, dst_pattern, src_group, 1);
  MUTEX_SDCARD_GIVE;

  // 6. Restore source RAM. Always run, even on write failure — the in-RAM
  //    state has to return to its pre-capture value either way.
  memcpy(&seq_cc_trk[src_track], &capture_cc_snapshot, sizeof(seq_cc_trk_t));
  memcpy(seq_par_layer_value[src_track], capture_par_snapshot, SEQ_PAR_MAX_BYTES);
  memcpy(seq_trg_layer_value[src_track], capture_trg_snapshot, SEQ_TRG_MAX_BYTES);
  memcpy(seq_pattern_name[src_group], capture_name_snapshot, 20);
  seq_core_trk[src_track].play_section = play_section_snapshot;
  SEQ_CORE_RenderDirtySet(src_track);
  SEQ_CC_LinkUpdate(src_track);

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Fork: capture a track's computed output onto another track in the current
// pattern (RAM only; does not touch SD).
//
// Track geometry (par/trg layer + step + instrument counts) lives in
// seq_par/seq_trg, NOT in seq_cc_trk, so a raw layer copy across tracks of
// differing geometry would read back as garbage. We mirror COPY/PASTE TRACK
// (seq_ui_util.c PASTE_CLEAR_MODE_TRACK): set the dst event-mode, repartition
// via SEQ_*_TrackInit with the source geometry, copy the lower-48 CCs + drum
// par-layer assignments (so layer *types* match) — only then is the raw output
// copy valid. The dst's generative CC is then reset so it plays the frozen
// line without re-modulation.
//
// Arms the unified UNDO net internally (SEQ_CORE_JournalArm(dst) below) — a
// caller must NOT snapshot dst itself or it double-arms. (Note: the live UI
// CAPTURE gesture is SEQ_CORE_CaptureSpan, which arms separately; this verb is
// reached only by the testctrl HIL path today.) Per the no-smart-default rule,
// dst is NOT gated on "empty" — the caller's pick is deliberate. Runs in task
// context; RAM only, no mutex.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_CaptureToTrack(u8 src_track, u8 dst_track)
{
  if( src_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( src_track == dst_track ) return -2; // in-place is the freeze verb

  // Arm the unified undo net: capture-to-track overwrites dst's live state, so
  // snapshot it first (this verb had no undo before the 2026-06-23 net).
  SEQ_CORE_JournalArm(dst_track);

  // 1. Inherit src config + geometry onto dst so the layer copy is valid.
  u16 par_steps  = (u16)SEQ_PAR_NumStepsGet(src_track);
  u8  par_layers = (u8)SEQ_PAR_NumLayersGet(src_track);
  u8  num_instr  = (u8)SEQ_PAR_NumInstrumentsGet(src_track);
  u16 trg_steps  = (u16)SEQ_TRG_NumStepsGet(src_track);
  u8  trg_layers = (u8)SEQ_TRG_NumLayersGet(src_track);

  SEQ_CC_Set(dst_track, SEQ_CC_MIDI_EVENT_MODE, SEQ_CC_Get(src_track, SEQ_CC_MIDI_EVENT_MODE));
  SEQ_CC_LinkUpdate(dst_track);
  SEQ_PAR_TrackInit(dst_track, par_steps, par_layers, num_instr);
  SEQ_TRG_TrackInit(dst_track, trg_steps, trg_layers, num_instr);

  // Faithful full-config inherit (mirrors PASTE_CLR_ALL in seq_ui_util.c): copy
  // the source's whole 0x00..0x7f CC space so length/clock/groove/trigger
  // assignments/routing travel with the frozen notes — not just the lay_const +
  // drum par-asg (which left the copy at dst's defaults: wrong length/clock,
  // wrong gates, no groove). ResetGenerativeForBounce below strips the generation
  // axis; the deterministic shaping (groove/length/clkdiv/structural trg-asg) stays.
  {
    int i;
    for(i=0; i<128; ++i)
      SEQ_CC_Set(dst_track, i, SEQ_CC_Get(src_track, i));
    SEQ_CC_LinkUpdate(dst_track);
  }

  // 2. Lossless output → dst source (forced quiet render → sweep-safe). Valid
  //    raw copy because dst geometry now equals src geometry.
  SEQ_CORE_CaptureTrackOutput(src_track, seq_par_layer_value[dst_track], seq_trg_layer_value[dst_track]);

  // (Track 2: the mirror already held the snapped/planed/limited pitch — no bake.)

  // 3. Sanitize generative CC on dst so the frozen line isn't re-modulated.
  SEQ_CC_ResetGenerativeForBounce(dst_track);

  // 4. Force a full dst render so SEQ_PAR_Get(dst) reads the captured bytes
  //    across the whole buffer immediately.
  seq_render_touched_ms[dst_track] = 0;
  seq_render_dirty[dst_track] = 1;
  SEQ_CORE_RenderTrack(dst_track);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Retroactive CAPTURE — re-simulate the last K bars of the VISIBLE track into a
// static dst track (2026-06-19). The keystone made every generative stream
// deterministic from a per-bar frame; this rewinds the ring K bars and re-drives
// the engine forward WITH wander on, recording the EMITTED stream (the real
// "Capture MIDI" — traversal order + robotize live in the emitted notes, NOT in
// the rendered mirror) and quantizing it into dst across K bars.
//
// STOPPED transport (the re-sim drives the one engine exclusively — no +4 tick race,
// no live freeze, no BPM gap; the while-playing tape is the running case). Materialize
// quantizes the emitted note-ons (pitch/rhythm/velocity/traversal/wander) and now also
// the precise gate: the sink pairs each note-off with its open note-on and writes the
// measured length. Notes still ringing past the window keep the default gate.
// Melodic/normal tracks (drum note-0 fence deferred).
/////////////////////////////////////////////////////////////////////////////

// Full live-state snapshot so the re-sim borrow is non-destructive.
typedef struct {
  seq_core_trk_t   trk[SEQ_CORE_NUM_TRACKS]; // step/traversal/robotize/counters (all tracks)
  seq_core_state_t state;                    // ref_step + FIRST_CLK/FREEZE/...
  u32              bpm_tick;
  u32              prefetched;               // bpm_tick_prefetched (the drive advances it)
  u32              prefetch_req;             // bpm_tick_prefetch_req
  u32              rng[SEQ_RANDOM_STATE_WORDS]; // global jsw MT + cache
  u8               pattern_dirty;
  u8               last_seen[SEQ_CORE_NUM_TRACKS]; // generator wrap detector
  seq_generator_t  gen[SEQ_CORE_CAP_RING_SLOTS];   // src track's generator slots
  u8               gen_count;
  u8               par_src[SEQ_PAR_MAX_BYTES];  // src source par (wander rewrites it)
  u8               trg_src[SEQ_TRG_MAX_BYTES];  // src source trg
} seq_core_cap_snapshot_t;

static seq_core_cap_snapshot_t seq_core_cap_snap; // main SRAM (~4.8 KB)

// Materialize-sink params (the re-sim's MIDI-out hook quantizes into dst).
static u8  capspan_src, capspan_dst;
static u16 capspan_tps;          // ticks per step
static u16 capspan_dst_steps;    // K * (length+1)
static u32 capspan_cur_tick;     // current synthetic drive bpm-tick
static u32 capspan_base;         // synthetic bpm-tick at the window start
static s8  capspan_note_layer, capspan_vel_layer, capspan_len_layer;
static u8  capspan_default_len;

// Open-note tracking for precise gate (re-sim path): the drive drains a note-on (vel>0)
// then its off (vel==0) at on_tick+gatelength. Remember each open note's dst step + on
// tick; when its off drains, gate = off_tick - on_tick -> the step's length. Small bounded
// array (a melodic line holds few at once); overflow or a missed off -> default gate. The
// invariant is "at most one open entry per dst step": a new on at a step evicts the prior
// occupant there, matching the sink's last-write-wins quantize.
#define CAPSPAN_OPEN_MAX 24
static struct { u8 note; u16 step; u32 on_tick; } capspan_open[CAPSPAN_OPEN_MAX];
static u8 capspan_open_count;
static u8 capspan_in_flush;      // 1 during the post-drive flush: those offs are past-window, skip gate

// MIDI-out hooks (run the scheduler against synthetic time + a quantizing sink).
static s32 SEQ_CORE_CapSpanSink(mios32_midi_port_t port, mios32_midi_package_t package)
{
  if( package.evnt0 >= 0xf8 ) return 0;          // ignore realtime (clock etc.)
  if( package.cable != capspan_src ) return 0;   // only the captured track
  if( package.event != NoteOn ) return 0;        // note events only

  if( package.evnt2 == 0 ) {                      // running-status note-off
    if( capspan_in_flush || capspan_len_layer < 0 ) return 0; // flush offs are past-window
    u8 ki;                                        // newest-first match -> back-fill its step's length
    for(ki=capspan_open_count; ki>0; --ki) {
      if( capspan_open[ki-1].note == package.evnt1 ) {
        u32 gate = capspan_cur_tick - capspan_open[ki-1].on_tick;
        SEQ_PAR_Set(capspan_dst, capspan_open[ki-1].step, (u8)capspan_len_layer, 0,
                    SEQ_CORE_CaptureGateToParLen(gate, capspan_tps));
        u8 m;                                     // remove entry, preserve LIFO order of the rest
        for(m=ki-1; m+1<capspan_open_count; ++m)
          capspan_open[m] = capspan_open[m+1];
        --capspan_open_count;
        break;
      }
    }
    return 0;
  }

  if( capspan_cur_tick < capspan_base ) return 0;
  u16 step = (u16)((capspan_cur_tick - capspan_base) / capspan_tps);
  if( step >= capspan_dst_steps ) return 0;      // outside the K-bar window
  if( capspan_note_layer >= 0 )
    SEQ_PAR_Set(capspan_dst, step, (u8)capspan_note_layer, 0, package.evnt1);
  if( capspan_vel_layer >= 0 )
    SEQ_PAR_Set(capspan_dst, step, (u8)capspan_vel_layer, 0, package.evnt2);
  if( capspan_len_layer >= 0 ) {
    SEQ_PAR_Set(capspan_dst, step, (u8)capspan_len_layer, 0, capspan_default_len); // default until off
    // keep one open entry per step: evict a prior note still open at this step (superseded
    // by last-write-wins), then record this on so its off back-fills the precise length.
    u8 w = 0, r;
    for(r=0; r<capspan_open_count; ++r)
      if( capspan_open[r].step != step ) capspan_open[w++] = capspan_open[r];
    capspan_open_count = w;
    if( capspan_open_count < CAPSPAN_OPEN_MAX ) {
      capspan_open[capspan_open_count].note    = package.evnt1;
      capspan_open[capspan_open_count].step    = step;
      capspan_open[capspan_open_count].on_tick = capspan_cur_tick;
      ++capspan_open_count;
    }
  }
  SEQ_TRG_GateSet(capspan_dst, step, 0, 1);
  return 0;
}
static s32 SEQ_CORE_CapSpanBpmRunning(void) { return 1; }
static u32 SEQ_CORE_CapSpanBpmTick(void) { return capspan_cur_tick; }
static s32 SEQ_CORE_CapSpanBpmSet(float bpm) { (void)bpm; return 0; }

static void SEQ_CORE_CaptureSpanSnapshot(u8 src)
{
  memcpy(seq_core_cap_snap.trk, seq_core_trk, sizeof(seq_core_trk));
  seq_core_cap_snap.state = seq_core_state;
  seq_core_cap_snap.bpm_tick = SEQ_BPM_TickGet();
  seq_core_cap_snap.prefetched = bpm_tick_prefetched;
  seq_core_cap_snap.prefetch_req = bpm_tick_prefetch_req;
  SEQ_RANDOM_StateGet(seq_core_cap_snap.rng);
  seq_core_cap_snap.pattern_dirty = seq_pattern_dirty;
  u8 t;
  for(t=0; t<SEQ_CORE_NUM_TRACKS; ++t)
    seq_core_cap_snap.last_seen[t] = SEQ_GENERATOR_LastSeenStepGet(t);
  seq_core_cap_snap.gen_count = SEQ_GENERATOR_TrackSnapshot(src, seq_core_cap_snap.gen, SEQ_CORE_CAP_RING_SLOTS);
  memcpy(seq_core_cap_snap.par_src, seq_par_layer_value[src], SEQ_PAR_MAX_BYTES);
  memcpy(seq_core_cap_snap.trg_src, seq_trg_layer_value[src], SEQ_TRG_MAX_BYTES);
}

static void SEQ_CORE_CaptureSpanRestore(u8 src)
{
  memcpy(seq_core_trk, seq_core_cap_snap.trk, sizeof(seq_core_trk));
  seq_core_state = seq_core_cap_snap.state;
  SEQ_BPM_TickSet(seq_core_cap_snap.bpm_tick);
  bpm_tick_prefetched = seq_core_cap_snap.prefetched;
  bpm_tick_prefetch_req = seq_core_cap_snap.prefetch_req;
  SEQ_RANDOM_StateSet(seq_core_cap_snap.rng);
  seq_pattern_dirty = seq_core_cap_snap.pattern_dirty; // clear src's spurious wander-dirty
  u8 t;
  for(t=0; t<SEQ_CORE_NUM_TRACKS; ++t)
    SEQ_GENERATOR_LastSeenStepSet(t, seq_core_cap_snap.last_seen[t]);
  SEQ_GENERATOR_TrackRestore(src, seq_core_cap_snap.gen, seq_core_cap_snap.gen_count);
  memcpy(seq_par_layer_value[src], seq_core_cap_snap.par_src, SEQ_PAR_MAX_BYTES);
  memcpy(seq_trg_layer_value[src], seq_core_cap_snap.trg_src, SEQ_TRG_MAX_BYTES);
  SEQ_GENERATOR_ForceRewrite(src); // re-derive src source from the restored loops
  // The drive left src's OUTPUT MIRROR holding the end-of-window line. Re-render so
  // the restored SOURCE is copied back into the mirror (stopped-transport reads the
  // mirror); otherwise live play / SEQ_PAR_Get(src) returns the wandered bytes.
  seq_render_touched_ms[src] = 0;
  seq_render_dirty[src] = 1;
  SEQ_CORE_RenderTrack(src);
}

// Shared destination preparation for BOTH CAPTURE paths (stopped re-sim + while-playing
// tape): inherit src's full config/geometry, give dst the K-bar length, strip the
// generative axis AND groove (note positions are already baked into the captured
// stream, so an inherited groove would double-apply), start dst all-rest. Caller holds
// MUTEX_MIDIOUT. Keep the two callers' prep identical by routing both through here.
static void SEQ_CORE_CaptureSpanPrepDst(u8 src, u8 dst, u16 dst_steps,
                                        u8 par_layers, u8 par_instr,
                                        u8 trg_layers, u8 trg_instr, u16 tps)
{
  SEQ_CC_Set(dst, SEQ_CC_MIDI_EVENT_MODE, SEQ_CC_Get(src, SEQ_CC_MIDI_EVENT_MODE));
  SEQ_CC_LinkUpdate(dst);
  SEQ_PAR_TrackInit(dst, dst_steps, par_layers, par_instr);
  SEQ_TRG_TrackInit(dst, dst_steps, trg_layers, trg_instr);
  { int i; for(i=0; i<128; ++i) SEQ_CC_Set(dst, i, SEQ_CC_Get(src, i)); SEQ_CC_LinkUpdate(dst); }
  SEQ_CC_ResetGenerativeForBounce(dst);                // forward playback, strip gen axis
  SEQ_CC_Set(dst, SEQ_CC_GROOVE_VALUE, 0);
  SEQ_CC_Set(dst, SEQ_CC_GROOVE_STYLE, 0);
  SEQ_CC_Set(dst, SEQ_CC_LENGTH, dst_steps - 1);       // K-bar length

  // Pin the dst clock divider to the CAPTURE GRID: the notes were quantized to a
  // `tps`-tick step spacing, so the frozen copy must PLAY at that spacing. The full-CC
  // inherit above copied src's clkdiv.value, but a self-bus / `Ctrl` routing on the
  // clock divider can leave that config at a TRANSIENT value (e.g. 0 -> divide-by-1)
  // that disagrees with the actual step spacing, so the copy played at the wrong speed.
  // step_length = (value+1)*(TRIPLETS?4:6); for an UNmodulated src this re-derives the
  // SAME value it inherited (tps == src step_length), so the normal bounce is unchanged.
  {
    u8 unit = seq_cc_trk[dst].clkdiv.TRIPLETS ? 4 : 6;
    if( unit && tps >= unit ) {
      u16 div = (u16)(tps / unit) - 1;
      if( div > 255 ) div = 255;
      SEQ_CC_Set(dst, SEQ_CC_CLK_DIVIDER, (u8)div);
    }
  }
  SEQ_CC_LinkUpdate(dst);
  memset(seq_par_layer_value[dst], 0, SEQ_PAR_MAX_BYTES); // start as all-rest
  memset(seq_trg_layer_value[dst], 0, SEQ_TRG_MAX_BYTES);
}

// Re-simulate the last `k` bars of `src` (the ring's track) into `dst`.
// Returns 0 on success; negative status on refusal (see CMD_CAPTURE_SPAN).
s32 SEQ_CORE_CaptureSpanReSim(u8 src, u8 dst, u8 k)
{
  if( src >= SEQ_CORE_NUM_TRACKS || dst >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( src == dst ) return -2;
  if( SEQ_BPM_IsRunning() ) return -3;                 // first cut: stopped only
  if( src != SEQ_CORE_CaptureRingTrack() ) return -4;  // ring isn't recording src
  if( SEQ_CORE_CaptureRingOverflow() ) return -5;      // more gens than ring holds

  seq_cc_trk_t *tcc = &seq_cc_trk[src];
  // Arp playmode early-returns in the render-stack processors (A8 fence) — its emitted
  // stream can't be faithfully quantized back; refuse it. Drum event_mode is by-ear
  // out-of-scope (note-0=kit-preset on playback, per-instrument link layers) but the
  // machinery (determinism / non-destructive / quantize of instrument-0 notes) is sound,
  // so it is allowed and validated by the HIL pitch-gen-on-drum setup; melodic is the
  // by-ear target.
  if( tcc->playmode == SEQ_CORE_TRKMODE_Arpeggiator ) return -11;

  // STOPPED re-sim is ONE global measure only (2026-06-26). The drive phase-aligns to the
  // global measure (B = gspm*96), so it faithfully reproduces a one-measure loop — but a
  // MULTI-measure loop (n = spm/gspm >= 2) is driven at the wrong phase (a hardware trace
  // showed a sub-measure rotation), and a NON-(whole-measure) loop is unaligned outright.
  // BOTH route to "play to grab": the while-PLAYING tape (SEQ_CORE_CaptureSpanTape) records
  // the emitted stream and is traversal- AND phase-correct for ANY length (n=1/n>=2/odd),
  // verified note-for-note. The multi-measure/non-aligned STOPPED re-sim drive-phase is the
  // deferred A2 kernel. seq_core_steps_per_measure is stored as (steps-1).
  // A SYNCH_TO_MEASURE track resolves to gspm (1-bar loop) via SEQ_CORE_CaptureLoopSteps,
  // so it passes the gate below and re-sims through the n==1 path; the drive re-applies the
  // synch reset at tick B (it runs the real tick logic), reproducing the audible measure.
  u16 spm  = SEQ_CORE_CaptureLoopSteps(src);
  u16 gspm = (u16)(seq_core_steps_per_measure + 1);
  if( spm != gspm ) return -8;                          // not ONE measure: play to grab (A2 pending)
  u16 dst_steps = (u16)k * spm;
  if( dst_steps == 0 || dst_steps > 256 ) return -7;    // exceeds max track steps

  // Window-START = the loop boundary k complete loops back (skipping any in-progress
  // partial loop). n = frames per loop = spm/gspm (gate above guarantees spm%gspm==0).
  // Frozen ctr (STOPPED) -> no race; the helper reduces to FrameBack(k) for n==1.
  u8 n = (u8)(spm / gspm);
  u32 mctr = seq_core_trk[src].robotize_measure_ctr;
  u8 win_o;
  if( SEQ_CORE_CaptureRingLoopWindow(src, mctr, n, k, NULL, &win_o, NULL) != 0 )
    return -6;                                          // not enough aligned history
  const seq_core_cap_frame_t *frame = &seq_core_cap_ring[(mctr - win_o) % SEQ_CORE_CAP_RING_BARS];

  u16 tps = seq_core_trk[src].step_length;
  if( tps == 0 ) tps = (u16)((tcc->clkdiv.value + 1) * (tcc->clkdiv.TRIPLETS ? 4 : 6));
  if( tps == 0 ) tps = 96;

  // par and trg carry INDEPENDENT instrument counts (a drum track is par 1-layer ×
  // 16-instr but trg 8-layer × 1-instr) — use each axis's own count for its TrackInit
  // and overflow guard.
  u8  par_layers = (u8)SEQ_PAR_NumLayersGet(src);
  u8  par_instr  = (u8)SEQ_PAR_NumInstrumentsGet(src);
  u8  trg_layers = (u8)SEQ_TRG_NumLayersGet(src);
  u8  trg_instr  = (u8)SEQ_TRG_NumInstrumentsGet(src);
  if( (u32)dst_steps * par_layers * par_instr > SEQ_PAR_MAX_BYTES ) return -9;        // dst par overflow
  if( (u32)((dst_steps + 7) / 8) * trg_layers * trg_instr > SEQ_TRG_MAX_BYTES ) return -12; // dst trg overflow

  MUTEX_MIDIOUT_TAKE;

  // (a) snapshot live state for a non-destructive borrow
  SEQ_CORE_CaptureSpanSnapshot(src);

  // Arm the unified undo net for the DST track BEFORE PrepDst overwrites it — this
  // is the live performance-surface CAPTURE gesture (UTILITY-hold grab), the most
  // destructive live verb. Placed after every refusal return so a refused capture
  // can't arm a bogus snapshot. (The Snapshot above borrows SRC; this snapshots DST.)
  SEQ_CORE_JournalArm(dst);

  // (b) prepare dst: inherit src config/geometry, K-bar length, start empty (shared
  //     with the while-playing tape path so both stay identical)
  SEQ_CORE_CaptureSpanPrepDst(src, dst, dst_steps, par_layers, par_instr, trg_layers, trg_instr, tps);

  // sink params (read dst links AFTER the CC inherit above)
  seq_cc_trk_t *dtcc = &seq_cc_trk[dst];
  capspan_src = src; capspan_dst = dst;
  capspan_tps = tps; capspan_dst_steps = dst_steps;
  capspan_note_layer = dtcc->link_par_layer_note;
  capspan_vel_layer  = dtcc->link_par_layer_velocity;
  capspan_len_layer  = dtcc->link_par_layer_length;
  capspan_default_len = SEQ_CORE_CAP_DEFAULT_LEN;       // fallback for notes whose off is past-window
  capspan_open_count = 0;                               // precise-gate open-note tracking starts empty
  capspan_in_flush = 0;

  // (c) rewind src to the window-start frame. The frame is the state at that measure's
  // ref_step==0 prologue (BEFORE the body's NextStep advance), so we restore it verbatim
  // and drive WITHOUT FIRST_CLK: the first driven tick does a real NextStep (advance /
  // random draw) exactly like LIVE did at that boundary, and last_seen==frame->step lets
  // the generator auto-mutate fire on the natural tick (reproducing the wander).
  // *** The exact first-step phase is the #1 hardware-validation item (HIL + by-ear). ***
  seq_core_trk_t *t = &seq_core_trk[src];
  t->random_traverse_state = frame->traverse_state;
  t->robotize_seed_state   = frame->robotize_seed; // self-contained in the frame (was the 16-deep
                                                   // robotize_seed_snapshots ring, which couldn't
                                                   // reach 16 bars back and is shared with FREEZE)
  t->step           = frame->step;
  t->step_saved     = frame->step_saved;
  t->step_replay_ctr   = frame->step_replay_ctr;
  t->step_fwd_ctr      = frame->step_fwd_ctr;
  t->step_interval_ctr = frame->step_interval_ctr;
  t->step_repeat_ctr   = frame->step_repeat_ctr;
  t->step_skip_ctr     = frame->step_skip_ctr;
  SEQ_GENERATOR_TrackRestore(src, frame->gen, frame->gen_count);
  SEQ_GENERATOR_ForceRewrite(src); // push the window-start loop into the source par buffer
  // wrap-detector = each track's current step: src matches the frame moment (mutate fires
  // naturally), non-src tracks won't spuriously mutate. The resim guard below also stops
  // round-0 loopback tracks from wandering.
  { u8 tt; for(tt=0; tt<SEQ_CORE_NUM_TRACKS; ++tt) SEQ_GENERATOR_LastSeenStepSet(tt, seq_core_trk[tt].step); }
  SEQ_GENERATOR_ReSimOnlyTrackSet(src);

  // phase-align the drive to a global-measure boundary: B = one measure of 16th-ticks,
  // so bpm_tick%96==0 (the ref_step/robotize clock) AND the step clock coincide as LIVE.
  u32 B = (u32)gspm * 96;
  seq_core_state.FIRST_CLK = 0;
  seq_core_state.FORCE_REF_STEP_RESET = 0;
  seq_core_state.reset_trkpos_req = 0;
  seq_core_state.ref_step = seq_core_steps_per_measure; // ++ -> wraps to 0 at tick B
  t->state.FIRST_CLK = 0;
  t->timestamp_next_step = B;
  t->timestamp_next_step_ref = B;
  bpm_tick_prefetched = B - 1; // belt-and-braces: the prefetch gate lives in SEQ_CORE_Handler,
                               // not the direct SEQ_CORE_Tick drive path, so this is a no-op for
                               // the drive — but it's snapshotted/restored so the live engine is
                               // left consistent regardless.

  // (d) install hooks, drive exactly K bars, materialize via the sink
  seq_core_cap_resim_active = 1;
  capspan_base = B;
  SEQ_MIDI_OUT_Callback_MIDI_SendPackage_Set(SEQ_CORE_CapSpanSink);
  SEQ_MIDI_OUT_Callback_BPM_IsRunning_Set(SEQ_CORE_CapSpanBpmRunning);
  SEQ_MIDI_OUT_Callback_BPM_TickGet_Set(SEQ_CORE_CapSpanBpmTick);
  SEQ_MIDI_OUT_Callback_BPM_Set_Set(SEQ_CORE_CapSpanBpmSet);

  u32 drive_ticks = (u32)dst_steps * tps;
  u32 i;
  for(i=0; i<drive_ticks; ++i) {
    u32 bt = B + i;
    capspan_cur_tick = bt;
    SEQ_CORE_Tick(bt, (s8)src, 0);  // export_track=src, mute=0 -> emit to the sink
    SEQ_MIDI_OUT_Handler();         // drain pool -> SEQ_CORE_CapSpanSink
  }

  // (e) flush residual queued items (note-offs scheduled past the window) through the
  //     still-installed sink so they don't leak into live output, then uninstall hooks.
  //     capspan_in_flush makes the sink skip gate back-fill here: a glide/sustained note's
  //     off is deferred (scheduled at 0xffffffff and only rescheduled when the tie breaks),
  //     so a note still sustaining at the window end drains its off in this flush at a stale
  //     tick — handled below as a glide, not via this gate math.
  capspan_in_flush = 1;
  SEQ_MIDI_OUT_FlushQueue();
  SEQ_MIDI_OUT_Callback_MIDI_SendPackage_Set(NULL);
  SEQ_MIDI_OUT_Callback_BPM_IsRunning_Set(NULL);
  SEQ_MIDI_OUT_Callback_BPM_TickGet_Set(NULL);
  SEQ_MIDI_OUT_Callback_BPM_Set_Set(NULL);
  seq_core_cap_resim_active = 0;
  SEQ_GENERATOR_ReSimOnlyTrackSet(0xff);

  // Notes still OPEN after the drive never saw an in-window note-off: they sustained PAST
  // the captured window — a glide/tie into the next (uncaptured) note, or a sustain-mode
  // tail. A note only stays open if its off was deferred past window end (its gatelength
  // exceeded the room left), i.e. it genuinely ties; a normal note's off drains in the
  // loop. Mark them as glide (95 -> len 96, the longest a step expresses) so the tie is
  // preserved instead of collapsing to the default gate.
  if( capspan_len_layer >= 0 ) {
    u8 oi;
    for(oi=0; oi<capspan_open_count; ++oi)
      SEQ_PAR_Set(capspan_dst, capspan_open[oi].step, (u8)capspan_len_layer, 0, 95);
  }

  // (f) restore the live engine (byte-identical) and mark only dst dirty
  SEQ_CORE_CaptureSpanRestore(src);
  SEQ_PATTERN_DirtySetTrack(dst); // the capture IS a deliberate change to dst

  MUTEX_MIDIOUT_GIVE;

  // (g) force a dst render so SEQ_PAR_Get(dst) reads the written bytes immediately
  seq_render_touched_ms[dst] = 0;
  seq_render_dirty[dst] = 1;
  SEQ_CORE_RenderTrack(dst);
  return 0;
}


// Grab the last `k` bars of `src` (the ring's track) into `dst` from the WHILE-PLAYING
// live tape — the recording of what actually sounded (not a re-simulation). No engine
// borrow: we just quantize the taped note-ons in the window to dst's step grid. Same
// refusal codes as the re-sim path (negative; see CMD_CAPTURE_SPAN), plus -10 = the
// span scrolled out of the tape ring (too many notes buffered). Runs under
// MUTEX_MIDIOUT so the tape read + dst write are serialized against the live engine.
s32 SEQ_CORE_CaptureSpanTape(u8 src, u8 dst, u8 k)
{
  if( src >= SEQ_CORE_NUM_TRACKS || dst >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( src == dst ) return -2;
  if( !SEQ_BPM_IsRunning() ) return -3;                // tape path is the running case
  if( src != SEQ_CORE_CaptureRingTrack() ) return -4;  // ring isn't recording src

  seq_cc_trk_t *tcc = &seq_cc_trk[src];
  if( tcc->playmode == SEQ_CORE_TRKMODE_Arpeggiator ) return -11; // unquantizable emitted stream (A8 fence)

  // Approach A (2026-06-26): ANY track length while PLAYING. spm = steps per LOOP
  // (tcc->length+1). The tape records the EMITTED stream, so the grab window is just a
  // tick-period slice of the track's own loop period — traversal-agnostic (works for
  // self-modulated direction / random / ping-pong / sub-measure / odd, not only whole
  // measures). A whole-measure track still uses the Approach-B per-measure markers below;
  // a non-aligned one slices by P = spm*tps ticks. (Stopped re-sim keeps the whole-measure
  // gate — A2 lifts it.) seq_core_steps_per_measure is stored as (steps-1).
  // A SYNCH_TO_MEASURE track resolves to gspm via SEQ_CORE_CaptureLoopSteps, so it takes
  // the whole-measure (n==1) marker branch below — the per-global-measure tape markers are
  // exactly the synch'd track's loop boundaries (the reset re-aligns it every bar).
  u16 spm  = SEQ_CORE_CaptureLoopSteps(src);
  u16 gspm = (u16)(seq_core_steps_per_measure + 1);
  if( spm == 0 ) return -7;                             // degenerate length
  u16 dst_steps = (u16)k * spm;
  if( dst_steps == 0 || dst_steps > 256 ) return -7;    // exceeds max track steps
  if( k < 1 ) return -6;

  u16 tps = seq_core_trk[src].step_length;
  if( tps == 0 ) tps = (u16)((tcc->clkdiv.value + 1) * (tcc->clkdiv.TRIPLETS ? 4 : 6));
  if( tps == 0 ) tps = 96;

  // par/trg carry INDEPENDENT instrument counts — guard each axis with its own.
  u8  par_layers = (u8)SEQ_PAR_NumLayersGet(src);
  u8  par_instr  = (u8)SEQ_PAR_NumInstrumentsGet(src);
  u8  trg_layers = (u8)SEQ_TRG_NumLayersGet(src);
  u8  trg_instr  = (u8)SEQ_TRG_NumInstrumentsGet(src);
  if( (u32)dst_steps * par_layers * par_instr > SEQ_PAR_MAX_BYTES ) return -9;
  if( (u32)((dst_steps + 7) / 8) * trg_layers * trg_instr > SEQ_TRG_MAX_BYTES ) return -12;

  MUTEX_MIDIOUT_TAKE;

  // Window [win_start, win_end) in absolute ticks = the last k COMPLETE loops.
  u32 win_start, win_end;
  if( gspm != 0 && (spm % gspm) == 0 ) {
    // Whole-measure track (Approach B): the per-measure bar markers line up loop-by-loop.
    // n = frames per loop = spm/gspm. Resolve the loop-aligned offsets against a single ctr
    // read (the engine advances ctr while PLAYING). Reduces to {win_o=k, win_e=0} for n==1
    // -> identical to the original window.
    u8 n = (u8)(spm / gspm);
    u32 ctr = seq_core_trk[src].robotize_measure_ctr;
    u8 win_o, win_e;
    if( SEQ_CORE_CaptureRingLoopWindow(src, ctr, n, k, NULL, &win_o, &win_e) != 0 ) {
      MUTEX_MIDIOUT_GIVE; return -6;                     // not enough aligned history
    }
    win_start = seq_core_cap_tape_bar_start[(ctr - win_o) % SEQ_CORE_CAP_RING_BARS];
    win_end   = seq_core_cap_tape_bar_start[(ctr - win_e) % SEQ_CORE_CAP_RING_BARS];
  } else {
    // Approach A — non-(whole-measure) track: no per-global-measure marker lines up, so
    // slice by the track's own loop PERIOD in ticks (P = spm*tps). The track advances one
    // step per tps ticks regardless of how its direction/self-bus wanders, so a tick slice
    // faithfully holds "what played in the last k loops" for ANY traversal. Loop boundaries
    // sit at multiples of P from transport start (first cut: assumes the track started
    // phase-aligned at tick 0; a mid-run synch/restart offset is the documented edge,
    // deferred). win_end = the in-progress loop's start (keep only COMPLETED loops);
    // win_start = k loops before it.
    if( k > (SEQ_CORE_CAP_RING_BARS - 1) ) { MUTEX_MIDIOUT_GIVE; return -6; } // match the thermometer cap (MaxK)
    u32 P = (u32)spm * tps;
    u32 now = (u32)SEQ_BPM_TickGet();
    u32 loops_done = now / P;                            // completed loops since tick 0
    if( (u32)k > loops_done ) { MUTEX_MIDIOUT_GIVE; return -6; } // not enough played yet
    win_end   = loops_done * P;
    win_start = win_end - (u32)k * P;
  }

  // Eviction guard: when the tape ring is full, the head slot holds the OLDEST retained
  // event. If even that is newer than the window start, the span's early notes scrolled
  // out -> the capture would be incomplete. Refuse (dense/poly overran the tape).
  if( seq_core_cap_tape_count >= SEQ_CORE_CAP_TAPE_EVENTS ) {
    u32 oldest_tick = seq_core_cap_tape[seq_core_cap_tape_head].tick;
    if( (s32)(oldest_tick - win_start) > 0 ) { MUTEX_MIDIOUT_GIVE; return -10; }
  }

  // Arm the unified undo net for DST before PrepDst overwrites it — after the
  // post-mutex eviction refusal (-10) above so a refused tape capture doesn't arm.
  SEQ_CORE_JournalArm(dst);

  // prepare dst (shared with the re-sim path)
  SEQ_CORE_CaptureSpanPrepDst(src, dst, dst_steps, par_layers, par_instr, trg_layers, trg_instr, tps);
  seq_cc_trk_t *dtcc = &seq_cc_trk[dst];
  s8 note_layer = dtcc->link_par_layer_note;
  s8 vel_layer  = dtcc->link_par_layer_velocity;
  s8 len_layer  = dtcc->link_par_layer_length;

  // Quantize the window's note-ons -> steps. Iterate oldest..newest; collisions on a
  // step are last-write-wins (melodic-mono target, same as the re-sim sink). Each on
  // carries its precise gate (off.tick-on.tick, back-filled at tap time); a note still
  // ringing at grab keeps gate 0 -> SEQ_CORE_CaptureGateToParLen returns the default. A
  // note spanning more than one step is written as a multi-step length chain (SEQ's
  // hand-drawn long-note encoding) by SEQ_CORE_CaptureMaterializeNote — a single Gld start
  // step would otherwise lose the duration (it would just tie to the next note).
  // EDGE (off the by-ear proof span): the taped tick has groove/step/port delay baked in,
  // so a note swung EARLY past win_start (negative groove) is dropped as "before window",
  // and one pushed LATE past win_end is dropped as "live bar". The clean melodic target
  // has no such offset; precise window-seam handling stays deferred.
  u16 n = seq_core_cap_tape_count;
  u16 idx = (u16)((seq_core_cap_tape_head + SEQ_CORE_CAP_TAPE_EVENTS - n) % SEQ_CORE_CAP_TAPE_EVENTS);
  u16 i;
  for(i=0; i<n; ++i) {
    seq_core_cap_tape_evt_t *e = &seq_core_cap_tape[idx];
    idx = (u16)((idx + 1) % SEQ_CORE_CAP_TAPE_EVENTS);
    if( (s32)(e->tick - win_start) < 0 ) continue;     // before the window
    if( (s32)(e->tick - win_end) >= 0 ) continue;      // in the live bar (not completed)
    u16 step = (u16)((e->tick - win_start) / tps);
    if( step >= dst_steps ) continue;
    SEQ_CORE_CaptureMaterializeNote(dst, step, dst_steps, e->gate, (u16)tps,
                                    e->note, e->vel, note_layer, vel_layer, len_layer);
  }

  SEQ_PATTERN_DirtySetTrack(dst); // the capture IS a deliberate change to dst

  // Render dst now so SEQ_PAR_Get(dst) reads the written bytes immediately. Done UNDER
  // the mutex (unlike re-sim's post-give render) because the live engine renders tracks
  // under this same mutex — serializing avoids racing the engine's own render of dst.
  seq_render_touched_ms[dst] = 0;
  seq_render_dirty[dst] = 1;
  SEQ_CORE_RenderTrack(dst);

  MUTEX_MIDIOUT_GIVE;
  return 0;
}


// Dispatcher: while PLAYING grab the live tape (the recorded performance); while STOPPED
// re-simulate the generative frame (regenerate the unrecorded past). The gesture + the
// testctrl verb call this so the same "grab last K bars" surface works in both states.
s32 SEQ_CORE_CaptureSpan(u8 src, u8 dst, u8 k)
{
  if( SEQ_BPM_IsRunning() )
    return SEQ_CORE_CaptureSpanTape(src, dst, k);
  return SEQ_CORE_CaptureSpanReSim(src, dst, k);
}


// Static snapshot of the destination group (4 tracks) for the capture-to-slot-
// track verb: preserve the dst group's live RAM across the load-modify-save so
// the operation doesn't disturb what's currently loaded/playing there. Plus a
// snapshot of the source's FULL CC config (captured before the load, in case the
// source shares the dst group). The whole 0x00..0x7f CC space, so the frozen copy
// faithfully inherits length/clock/groove/trigger-assignments/routing — not just
// the lay_const + drum par-asg the old lower-48 inherit copied.
static seq_cc_trk_t slottrk_cc_snap[SEQ_CORE_NUM_TRACKS_PER_GROUP];
static u8           slottrk_par_snap[SEQ_CORE_NUM_TRACKS_PER_GROUP][SEQ_PAR_MAX_BYTES];
static u8           slottrk_trg_snap[SEQ_CORE_NUM_TRACKS_PER_GROUP][SEQ_TRG_MAX_BYTES];
static char         slottrk_name_snap[20];
static u8           slottrk_play_section_snap[SEQ_CORE_NUM_TRACKS_PER_GROUP];
static u8           slottrk_src_cc[128];
// Stage B: live generators of the dst group, snapped around the staged
// load-modify-save — the slot read seeds the SLOT's generators into the pool
// (so the write-back round-trips them faithfully), and this restore puts the
// LIVE organisms back afterwards. Persist-cap per track, like the file format.
static seq_generator_t slottrk_gen_snap[SEQ_CORE_NUM_TRACKS_PER_GROUP][SEQ_GENERATOR_PERSIST_SLOTS];
static u8              slottrk_gen_count[SEQ_CORE_NUM_TRACKS_PER_GROUP];

/////////////////////////////////////////////////////////////////////////////
// Fork: capture src_track's computed output into dst_track of slot (bank,
// pattern), PERSISTED to SD, preserving the slot's other tracks.
//
// MBSEQ pattern slots store a whole 4-track group, so to place one track into a
// stored slot we: capture the source first (before any SD load, in case the
// source shares the dst group); snapshot the dst group's live RAM; read the
// target slot into the dst group (remix_map=0 so all 4 tracks load); replace
// dst_track with the captured output (src geometry/CC inherited, generative CC
// reset); write the slot back; restore the dst group's live RAM. seq_pattern[]
// is never touched, so the dst group's "current pattern" tracking is unchanged.
//
// MUST run in task context (two MUTEX_SDCARD ops). While the slot is staged
// (read..write), a running dst group briefly plays the target slot — a small,
// deliberate glitch. Returns the PatternWrite status (>=0 ok), or the negative
// PatternRead status if the load failed (dst group left restored).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_CaptureToSlotTrack(u8 src_track, u8 dst_track, u8 dst_bank, u8 dst_pattern)
{
  if( src_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -1;

  u8 dst_group = dst_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
  u8 dst_base  = dst_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
  s32 status;
  int t, i;

  // 1. Capture source output + config BEFORE touching the dst group (step 3's
  //    load overwrites the dst group's RAM, which may include the source).
  SEQ_CORE_CaptureTrackOutput(src_track, capture_par_snapshot, capture_trg_snapshot);
  u16 src_par_steps  = (u16)SEQ_PAR_NumStepsGet(src_track);
  u8  src_par_layers = (u8)SEQ_PAR_NumLayersGet(src_track);
  u8  src_num_instr  = (u8)SEQ_PAR_NumInstrumentsGet(src_track);
  u16 src_trg_steps  = (u16)SEQ_TRG_NumStepsGet(src_track);
  u8  src_trg_layers = (u8)SEQ_TRG_NumLayersGet(src_track);
  // Snapshot the source's FULL CC config (0x00..0x7f). The frozen copy must
  // reproduce what was heard — length (0x4d), clock divider (0x4c), groove
  // (0x52/0x53) and the trigger-layer assignments (0x60..0x68) all live above the
  // old lower-48 inherit, so a partial copy left the copy running at the dst
  // slot's defaults ("too fast" wrong length/clock, wrong gates, no groove).
  for(i=0; i<128; ++i)
    slottrk_src_cc[i] = (u8)SEQ_CC_Get(src_track, i);

  // 2. Snapshot the dst group's live RAM (4 tracks) so we can restore it.
  for(t=0; t<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++t) {
    memcpy(&slottrk_cc_snap[t], &seq_cc_trk[dst_base+t], sizeof(seq_cc_trk_t));
    memcpy(slottrk_par_snap[t], seq_par_layer_value[dst_base+t], SEQ_PAR_MAX_BYTES);
    memcpy(slottrk_trg_snap[t], seq_trg_layer_value[dst_base+t], SEQ_TRG_MAX_BYTES);
    slottrk_play_section_snap[t] = seq_core_trk[dst_base+t].play_section;
    // Stage B: gen state is track content — step 3's read replaces the pool
    // slots with the SLOT's generators (correct for the step-5 write-back);
    // snap the live ones so step 6 can put them back.
    slottrk_gen_count[t] = SEQ_GENERATOR_TrackSnapshot(dst_base+t, slottrk_gen_snap[t], SEQ_GENERATOR_PERSIST_SLOTS);
  }
  memcpy(slottrk_name_snap, seq_pattern_name[dst_group], 20);
  // ...including the pattern-dirty bit: steps 3/4 replay CCs through the
  // SEQ_CC_Set chokepoint, but the trample is fully restored below — a clean
  // group must not come out flagged for auto-writeback.
  u8 dirty_snap = seq_pattern_dirty & (1 << dst_group);

  // 3. Read the target slot into the dst group (full load, remix_map=0).
  MUTEX_SDCARD_TAKE;
  status = SEQ_FILE_B_PatternRead(dst_bank, dst_pattern, dst_group, 0);
  MUTEX_SDCARD_GIVE;

  if( status >= 0 ) {
    // 4. Inherit src config/geometry onto dst_track, write the captured output,
    //    sanitize generative CC (incl. the gate-assignment-preserving fix).
    SEQ_CC_Set(dst_track, SEQ_CC_MIDI_EVENT_MODE, slottrk_src_cc[SEQ_CC_MIDI_EVENT_MODE]);
    SEQ_CC_LinkUpdate(dst_track);
    SEQ_PAR_TrackInit(dst_track, src_par_steps, src_par_layers, src_num_instr);
    SEQ_TRG_TrackInit(dst_track, src_trg_steps, src_trg_layers, src_num_instr);
    // Faithful full-config inherit (mirrors PASTE_CLR_ALL in seq_ui_util.c): copy
    // the source's whole 0x00..0x7f CC space so length/clock/groove/trigger
    // assignments/routing travel with the frozen notes. SEQ_CC_ResetGenerativeForBounce
    // below strips the generation axis (mode/direction/transpose/robotize/echo/lfo/
    // random) while keeping the deterministic shaping (groove, length, clkdiv,
    // structural trg-asg). EVENT_MODE re-set in the loop is a harmless no-op
    // (already set above; the setter only re-links, never re-partitions).
    for(i=0; i<128; ++i)
      SEQ_CC_Set(dst_track, i, slottrk_src_cc[i]);
    SEQ_CC_LinkUpdate(dst_track);
    memcpy(seq_par_layer_value[dst_track], capture_par_snapshot, SEQ_PAR_MAX_BYTES);
    memcpy(seq_trg_layer_value[dst_track], capture_trg_snapshot, SEQ_TRG_MAX_BYTES);
    // (Track 2: the mirror already held the snapped/planed/limited pitch — no bake.)
    SEQ_CC_ResetGenerativeForBounce(dst_track);
    // The captured copy is a FREEZE: generator-less by definition (same intent
    // as the generative-CC reset above). Clears the slot-read-seeded gens for
    // this section so the step-5 write persists none for it.
    SEQ_GENERATOR_TrackClear(dst_track);

    // 5. Write the dst group (now carrying the captured track) back to the slot.
    MUTEX_SDCARD_TAKE;
    status = SEQ_FILE_B_PatternWrite(seq_file_session_name, dst_bank, dst_pattern, dst_group, 1);
    MUTEX_SDCARD_GIVE;
  }

  // 6. Restore the dst group's live RAM (always — even on a read/write error).
  for(t=0; t<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++t) {
    memcpy(&seq_cc_trk[dst_base+t], &slottrk_cc_snap[t], sizeof(seq_cc_trk_t));
    memcpy(seq_par_layer_value[dst_base+t], slottrk_par_snap[t], SEQ_PAR_MAX_BYTES);
    memcpy(seq_trg_layer_value[dst_base+t], slottrk_trg_snap[t], SEQ_TRG_MAX_BYTES);
    seq_core_trk[dst_base+t].play_section = slottrk_play_section_snap[t];
    SEQ_CC_LinkUpdate(dst_base+t);
    // Stage B: put the LIVE organisms back (clears the slot's gens that rode
    // the pool through the staged window).
    SEQ_GENERATOR_TrackRestore(dst_base+t, slottrk_gen_snap[t], slottrk_gen_count[t]);
    // Track 2: steps 3/4 above moved the processor slots (PatternRead replays
    // CCs through SEQ_CC_Set; the CC inherit too) but this restore is a raw
    // memcpy — re-sync the slot bridges so a live Transpose/FTS/LIMIT/GRIP
    // track doesn't come back with a disarmed slot under a non-neutral tcc
    // (silently raw playback until the next CC touch).
    SEQ_CORE_ChordMaskSlotSync(dst_base+t);
    SEQ_CORE_TensionSlotSync(dst_base+t);
    SEQ_CORE_PitchSlotSync(dst_base+t);
    SEQ_CORE_LimitSlotSync(dst_base+t);
    SEQ_CORE_RenderDirtySet(dst_base+t);
  }
  memcpy(seq_pattern_name[dst_group], slottrk_name_snap, 20);

  MIOS32_IRQ_Disable();
  seq_pattern_dirty = (seq_pattern_dirty & ~(1 << dst_group)) | dirty_snap;
  MIOS32_IRQ_Enable();

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Fork: capture the live RECORDER (the retroactive ring/tape — what actually
// SOUNDED over the last k loops, incl. live keys / emission coin-flips / wander)
// of `src` into `dst_track` of slot (dst_bank, dst_pattern), PERSISTED to SD,
// preserving the slot's other 3 tracks. The while-PLAYING companion to
// SEQ_CORE_CaptureToSlotTrack (which freezes the static render mirror): the
// PATTERN-hold gesture dispatches here when the transport runs so "capture to
// another pattern" keeps the PERFORMANCE, not a single rendered frame. With
// dst_track == src this is exactly "same track, another pattern".
//
// SEQ_CORE_CaptureSpan materializes into a real track, so we borrow a SCRATCH
// track in the dst group (!= src, != dst_track), snapshot the WHOLE dst group
// FIRST (so both the scratch borrow AND the slot's staged load are undone by one
// restore), span-capture the recorder into scratch, then run the same load-
// modify-save as CaptureToSlotTrack sourcing the captured scratch. seq_pattern[]
// is never touched. MUST run in task context (CaptureSpan takes MUTEX_MIDIOUT,
// the slot R/W takes MUTEX_SDCARD — sequential, not nested).
//
// Returns the PatternWrite status (>=0 ok); a negative CaptureSpan refusal (e.g.
// -3 wrong transport, -4 ring not recording src, -8 multi-bar stopped) or the
// PatternRead status on failure (dst group left restored).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_CaptureSpanToSlotTrack(u8 src, u8 dst_track, u8 dst_bank, u8 dst_pattern, u8 k)
{
  if( src >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -1;

  u8 dst_group = dst_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
  u8 dst_base  = dst_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
  s32 status;
  int t, i;

  // Borrow a scratch track in the dst group: the span capture needs a target != src,
  // and the slot writer overwrites dst_track. With 4 tracks/group and 2 excluded there
  // is always one free.
  u8 scratch = 0xff;
  for(t=0; t<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++t) {
    u8 cand = (u8)(dst_base + t);
    if( cand != src && cand != dst_track ) { scratch = cand; break; }
  }
  if( scratch == 0xff ) return -1;

  // 1. Snapshot the dst group's live RAM (4 tracks) BEFORE the span capture, so the
  //    scratch borrow AND the slot's staged load both unwind in step 7's restore.
  for(t=0; t<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++t) {
    memcpy(&slottrk_cc_snap[t], &seq_cc_trk[dst_base+t], sizeof(seq_cc_trk_t));
    memcpy(slottrk_par_snap[t], seq_par_layer_value[dst_base+t], SEQ_PAR_MAX_BYTES);
    memcpy(slottrk_trg_snap[t], seq_trg_layer_value[dst_base+t], SEQ_TRG_MAX_BYTES);
    slottrk_play_section_snap[t] = seq_core_trk[dst_base+t].play_section;
    slottrk_gen_count[t] = SEQ_GENERATOR_TrackSnapshot(dst_base+t, slottrk_gen_snap[t], SEQ_GENERATOR_PERSIST_SLOTS);
  }
  memcpy(slottrk_name_snap, seq_pattern_name[dst_group], 20);
  u8 dirty_snap = seq_pattern_dirty & (1 << dst_group);

  // 2. Materialize the recorder (tape while playing / re-sim stopped) into scratch.
  //    Non-destructive to src; writes scratch (in the snapshotted group). Suppress the
  //    internal undo arm — scratch is restored, so it must not eat the user's one-deep.
  seq_core_cap_suppress_journal = 1;
  status = SEQ_CORE_CaptureSpan(src, scratch, k);
  seq_core_cap_suppress_journal = 0;

  if( status >= 0 ) {
    // 3. scratch now holds the captured loop (static, ResetGen'd, length=k*spm, clock
    //    pinned to the grid). Read it as the slot-source material + geometry.
    memcpy(capture_par_snapshot, seq_par_layer_value[scratch], SEQ_PAR_MAX_BYTES);
    memcpy(capture_trg_snapshot, seq_trg_layer_value[scratch], SEQ_TRG_MAX_BYTES);
    for(i=0; i<128; ++i)
      slottrk_src_cc[i] = (u8)SEQ_CC_Get(scratch, i);
    u16 cap_par_steps  = (u16)SEQ_PAR_NumStepsGet(scratch);
    u8  cap_par_layers = (u8)SEQ_PAR_NumLayersGet(scratch);
    u8  cap_num_instr  = (u8)SEQ_PAR_NumInstrumentsGet(scratch);
    u16 cap_trg_steps  = (u16)SEQ_TRG_NumStepsGet(scratch);
    u8  cap_trg_layers = (u8)SEQ_TRG_NumLayersGet(scratch);

    // 4. Read the target slot into the dst group (full load, remix_map=0).
    MUTEX_SDCARD_TAKE;
    status = SEQ_FILE_B_PatternRead(dst_bank, dst_pattern, dst_group, 0);
    MUTEX_SDCARD_GIVE;

    if( status >= 0 ) {
      // 5. Inherit the captured geometry/CC onto the slot's dst_track + write the notes
      //    (same shape as CaptureToSlotTrack step 4).
      SEQ_CC_Set(dst_track, SEQ_CC_MIDI_EVENT_MODE, slottrk_src_cc[SEQ_CC_MIDI_EVENT_MODE]);
      SEQ_CC_LinkUpdate(dst_track);
      SEQ_PAR_TrackInit(dst_track, cap_par_steps, cap_par_layers, cap_num_instr);
      SEQ_TRG_TrackInit(dst_track, cap_trg_steps, cap_trg_layers, cap_num_instr);
      for(i=0; i<128; ++i)
        SEQ_CC_Set(dst_track, i, slottrk_src_cc[i]);
      SEQ_CC_LinkUpdate(dst_track);
      memcpy(seq_par_layer_value[dst_track], capture_par_snapshot, SEQ_PAR_MAX_BYTES);
      memcpy(seq_trg_layer_value[dst_track], capture_trg_snapshot, SEQ_TRG_MAX_BYTES);
      SEQ_CC_ResetGenerativeForBounce(dst_track);
      SEQ_GENERATOR_TrackClear(dst_track);

      // 6. Write the dst group (now carrying the captured track) back to the slot.
      MUTEX_SDCARD_TAKE;
      status = SEQ_FILE_B_PatternWrite(seq_file_session_name, dst_bank, dst_pattern, dst_group, 1);
      MUTEX_SDCARD_GIVE;
    }
  }

  // 7. Restore the dst group's live RAM (always — undoes the scratch borrow AND any slot
  //    load; mirrors CaptureToSlotTrack's restore incl. the slot-bridge re-sync).
  for(t=0; t<SEQ_CORE_NUM_TRACKS_PER_GROUP; ++t) {
    memcpy(&seq_cc_trk[dst_base+t], &slottrk_cc_snap[t], sizeof(seq_cc_trk_t));
    memcpy(seq_par_layer_value[dst_base+t], slottrk_par_snap[t], SEQ_PAR_MAX_BYTES);
    memcpy(seq_trg_layer_value[dst_base+t], slottrk_trg_snap[t], SEQ_TRG_MAX_BYTES);
    seq_core_trk[dst_base+t].play_section = slottrk_play_section_snap[t];
    SEQ_CC_LinkUpdate(dst_base+t);
    SEQ_GENERATOR_TrackRestore(dst_base+t, slottrk_gen_snap[t], slottrk_gen_count[t]);
    SEQ_CORE_ChordMaskSlotSync(dst_base+t);
    SEQ_CORE_TensionSlotSync(dst_base+t);
    SEQ_CORE_PitchSlotSync(dst_base+t);
    SEQ_CORE_LimitSlotSync(dst_base+t);
    SEQ_CORE_RenderDirtySet(dst_base+t);
  }
  memcpy(seq_pattern_name[dst_group], slottrk_name_snap, 20);

  MIOS32_IRQ_Disable();
  seq_pattern_dirty = (seq_pattern_dirty & ~(1 << dst_group)) | dirty_snap;
  MIOS32_IRQ_Enable();

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Fork: one-deep track undo — the RECOMBINE keystone (design doc §9
// 2026-06-11). The snapshot buffer shape for one track's full persisted state
// (geometry, name, CC image 0x00..0x9f, robotize anchors, par/trg sources,
// play_section, generators). Used by the unified action journal below as both
// the `before` and `after` snapshots.
/////////////////////////////////////////////////////////////////////////////

// full persisted CC image: base 0x00..0x7f + ext block range 0x80..0x9f
#define TRACK_UNDO_CC_COUNT 0xa0

typedef struct {
  u8 valid;
  u8 track;
  u8 play_section;  // runtime state, never in the pattern file
  u16 par_steps;
  u8  par_layers;
  u8  par_instruments;
  u16 trg_steps;
  u8  trg_layers;
  u8  trg_instruments;
  char name[81];
  u8 cc[TRACK_UNDO_CC_COUNT];
  u8 anchors[sizeof(((seq_cc_trk_t *)0)->robotize_bar_anchors)]; // 64
  u8 par[SEQ_PAR_MAX_BYTES];
  u8 trg[SEQ_TRG_MAX_BYTES];
  // Stage B: the victim's generators — without these, undoing a pull leaves
  // the PULLED organism alive rewriting the restored notes at the next wrap.
  u8 gen_count;
  seq_generator_t gen[SEQ_GENERATOR_PERSIST_SLOTS];
} seq_core_track_undo_t;   // ~2.4 KB

// Unified action journal (design §10(a2) / play-readiness safety net,
// 2026-06-23): ONE global one-deep store behind every deliberate track-grain
// gesture — pull (RECOMBINE), utility copy/paste/clear, generator ENGAGE,
// capture-to-track. Replaces the three bespoke one-deeps (this `track_undo`,
// the generator `undo_slot`, the utility buffers). Holds the pre-gesture state
// (`before`) and, captured lazily at UNDO time from live, the post-gesture
// state (`after`) — so one SELECT+CLEAR toggles back and forth (undo -> redo ->
// undo) with no redo arm to track separately. CCM_SECTION: main SRAM is the
// scarce, MSP-stack-gated region on this fork (§A5; ~6 KB free) while CCM has
// ~2x the headroom — and both predecessor stores (track_undo, generator
// undo_slot) lived in CCM. The journal is task-context RAM-only (never DMA), so
// CCM is fine. The restore engine is geometry-safe (TrackInit re-applies the
// snapshot's own geometry) so a stale restore can't tear par/trg; disk-load
// paths still invalidate the journal via SEQ_CORE_JournalInvalidate so an UNDO
// can't clobber a freshly-loaded track.
typedef struct {
  u8 state;                      // seq_core_journal_state_t
  u8 scope;                      // seq_core_journal_scope_t (TRACK | ORGANISM)
  seq_core_track_undo_t before;
  seq_core_track_undo_t after;
} seq_core_action_journal_t;
// CCM_SECTION between the (named) type and the variable applies to the variable
// — the proven idiom (cf. the old `seq_core_track_undo_t CCM_SECTION track_undo`).
// Placed on an anonymous `struct {}` it binds to the type and is silently ignored.
static seq_core_action_journal_t CCM_SECTION action_journal;

// Fill `u` with one track's full persisted live state. RAM only, task context.
static s32 journal_snap(seq_core_track_undo_t *u, u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;

  u->valid = 0; // invalidate while the snapshot is in flight
  u->track = track;
  u->play_section = seq_core_trk[track].play_section;
  u->par_steps        = (u16)SEQ_PAR_NumStepsGet(track);
  u->par_layers       = (u8)SEQ_PAR_NumLayersGet(track);
  u->par_instruments  = (u8)SEQ_PAR_NumInstrumentsGet(track);
  u->trg_steps        = (u16)SEQ_TRG_NumStepsGet(track);
  u->trg_layers       = (u8)SEQ_TRG_NumLayersGet(track);
  u->trg_instruments  = (u8)SEQ_TRG_NumInstrumentsGet(track);
  memcpy(u->name, seq_core_trk[track].name, 81);

  int i;
  for(i=0; i<TRACK_UNDO_CC_COUNT; ++i) {
    s32 v = SEQ_CC_Get(track, i);
    u->cc[i] = (v >= 0) ? (u8)v : 0;
  }
  memcpy(u->anchors, seq_cc_trk[track].robotize_bar_anchors, sizeof(u->anchors));
  memcpy(u->par, seq_par_layer_value[track], SEQ_PAR_MAX_BYTES);
  memcpy(u->trg, seq_trg_layer_value[track], SEQ_TRG_MAX_BYTES);
  u->gen_count = SEQ_GENERATOR_TrackSnapshot(track, u->gen, SEQ_GENERATOR_PERSIST_SLOTS);

  u->valid = 1;
  return 0;
}

// Restore one track from `u`. Mirrors the TrackRead write path: CC replay
// through SEQ_CC_Set (re-syncs processor slots), TrackInit with the snapshot
// geometry, bulk par/trg, name/anchors/play_section, generator restore. The
// restore is itself a live gesture: sustain cancelled, bar-aligned drop. Does
// NOT touch journal.state (the caller owns the undo/redo state machine).
// Returns the restored track (>=0), -1 if the buffer is empty.
static s32 journal_restore(const seq_core_track_undo_t *u)
{
  if( !u->valid )
    return -1;

  u8 track = u->track;

  // RAM-only write phase under tick exclusion (fast — no SD I/O): a tick
  // between the CC replay and the bulk memcpys would render/emit torn state.
  portENTER_CRITICAL();

  memcpy(seq_core_trk[track].name, u->name, 81);

  int i;
  for(i=0; i<TRACK_UNDO_CC_COUNT; ++i)
    SEQ_CC_Set(track, i, u->cc[i]);

  SEQ_PAR_TrackInit(track, u->par_steps, u->par_layers, u->par_instruments);
  memcpy(seq_par_layer_value[track], u->par, SEQ_PAR_MAX_BYTES);

  SEQ_TRG_TrackInit(track, u->trg_steps, u->trg_layers, u->trg_instruments);
  memcpy(seq_trg_layer_value[track], u->trg, SEQ_TRG_MAX_BYTES);

  memcpy(seq_cc_trk[track].robotize_bar_anchors, u->anchors, sizeof(u->anchors));
  seq_core_trk[track].play_section = u->play_section;

  SEQ_CORE_RenderDirtySet(track);
  SEQ_CC_LinkUpdate(track);

  // Put the snapshot's generators back (and kill whatever a destructive verb
  // seeded). After TrackInit/LinkUpdate so the par-layer validation sees the
  // restored geometry.
  SEQ_GENERATOR_TrackRestore(track, u->gen, u->gen_count);

  portEXIT_CRITICAL();

  // Force a full quiet render — same emission-freshness contract as the pull
  // verb (a sweep-regime tick could consume the dirty flag window-only).
  seq_render_touched_ms[track] = 0;
  seq_render_dirty[track] = 1;
  SEQ_CORE_RenderTrack(track);

  // The restore is itself a track-grain load — mirror the pull verb's external
  // fan rows, or the rig stays on the prior track's program/bank and the
  // latches still describe the prior layer assignments. (UNMUTE stays out: an
  // undo must not change mute state.)
  SEQ_CORE_CancelSustainedNotes(track);

  if( !seq_core_options.PATTERN_CHANGE_DONT_RESET_LATCHED_PC )
    SEQ_LAYER_ResetLatchedValuesTrack(track);

  MUTEX_MIDIOUT_TAKE;
  SEQ_LAYER_SendPCBankValues(track, 0, 1);
  MUTEX_MIDIOUT_GIVE;

  SEQ_CORE_ManualSynchToMeasure(1 << track);

  // a restore wrote this track's CCs out-of-band -> stale any morph armed on
  // its group (same stale-A class as the pull/switch it usually reverts).
  SEQ_PATTERN_PhraseMorphInvalidateGroup(track / SEQ_CORE_NUM_TRACKS_PER_GROUP);

  return track;
}

/////////////////////////////////////////////////////////////////////////////
// Action journal — the unified one-deep UNDO/REDO net (§10(a2)).
//
// ARM: every deliberate track-grain gesture (pull, utility copy/paste/clear,
// generator ENGAGE, capture-to-track) calls SEQ_CORE_JournalArm BEFORE it
// overwrites the track — snapshots `before`, clears the redo arm, state =
// UNDOABLE. Only deliberate verbs (task context) arm; generator auto-mutate
// never does, so ambient wander can't pollute the journal.
//
// UNDO: snapshot `after` <- live (so REDO can re-apply the gesture's result),
// restore `before`, state = REDOABLE.
// REDO: snapshot `before` <- live (so UNDO can step back a REDO that clobbered
// work done after the undo), restore `after`, state = UNDOABLE.
//
// SYMMETRIC 2-way swap: each direction captures live before restoring, so the
// one-deep toggle never silently loses work — a REDO over hand-edits made after
// an UNDO is itself undoable. A fresh ARM overwrites `before` and re-arms.
// RAM only, no mutex; runs in task context (same contract as the old undo).
//
// ORGANISM scope (Stage 2b): REVERT arms via SEQ_CORE_JournalArmOrganism — no
// RAM snapshot, since the pre-revert jam and the checkpoint live in fixed
// anchor-bank SD slots that SEQ_PATTERN owns. UNDO/REDO re-read those slots
// (SEQ_PATTERN_RevertUndoRead / _RevertRedoRead). Those reads go through
// SnapshotRead, which invalidates the journal as a side effect (a whole-organism
// restore stales any track undo) — so each organism UNDO/REDO RE-ARMS the journal
// state afterwards. A fresh TRACK-grain ARM supersedes an organism arm.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_JournalArm(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;

  // Internal scratch capture (CaptureSpanToSlotTrack): the dst is a borrowed track
  // that is restored byte-identical, so arming it would silently replace the user's
  // real one-deep undo with a no-op. Leave the journal untouched.
  if( seq_core_cap_suppress_journal ) return 0;

  // The snapshot caps generators at SEQ_GENERATOR_PERSIST_SLOTS (the same cap
  // every persistence boundary uses — bank save, capture frame, pull). If the
  // track carries MORE engaged generators than that, a restore would silently
  // delete the overflow, so refuse to arm (leave the journal EMPTY: no undo
  // offered) rather than offer a destructive one. Mirrors the capture-ring
  // overflow guard. TrackEngagedCount uses the same predicate TrackSnapshot
  // copies with, so this detects truncation exactly.
  if( SEQ_GENERATOR_TrackEngagedCount(track) > SEQ_GENERATOR_PERSIST_SLOTS ) {
    SEQ_CORE_JournalInvalidate();
    return -2;
  }

  s32 r = journal_snap(&action_journal.before, track);
  if( r < 0 ) {
    SEQ_CORE_JournalInvalidate();
    return r;
  }
  action_journal.scope = SEQ_CORE_JRNL_TRACK; // a track gesture supersedes any organism arm
  action_journal.after.valid = 0;        // a new gesture clears the redo arm
  action_journal.state = SEQ_CORE_JRNL_UNDOABLE;
  return 0;
}

// Arm the whole-organism (REVERT) undo. No RAM snapshot: the pre-revert jam and
// the checkpoint live in fixed anchor-bank SD slots, so UNDO/REDO just re-read
// them. Called by SEQ_PATTERN_Revert AFTER its SnapshotRead has already
// invalidated any stale track-grain journal. RAM only, task context.
s32 SEQ_CORE_JournalArmOrganism(void)
{
  action_journal.scope = SEQ_CORE_JRNL_ORGANISM;
  action_journal.before.valid = 0;       // organism scope uses SD slots, not the RAM buffers
  action_journal.after.valid = 0;
  action_journal.state = SEQ_CORE_JRNL_UNDOABLE;
  return 0;
}

s32 SEQ_CORE_JournalUndo(void)
{
  if( action_journal.state != SEQ_CORE_JRNL_UNDOABLE )
    return -1;

  if( action_journal.scope == SEQ_CORE_JRNL_ORGANISM ) {
    // Restore the pre-revert jam. SnapshotRead invalidates the journal on its
    // way out (it stales any track undo), so re-establish the organism arm as
    // REDOABLE afterwards — SELECT+CLEAR again re-reverts to the checkpoint.
    s32 r = SEQ_PATTERN_RevertUndoRead();
    if( r < 0 ) return r;
    action_journal.scope = SEQ_CORE_JRNL_ORGANISM;
    action_journal.state = SEQ_CORE_JRNL_REDOABLE;
    return r;
  }

  if( !action_journal.before.valid )
    return -1;

  // capture the post-gesture (live) state so REDO can re-apply it
  journal_snap(&action_journal.after, action_journal.before.track);

  s32 r = journal_restore(&action_journal.before);
  if( r < 0 ) return r;

  action_journal.state = SEQ_CORE_JRNL_REDOABLE;
  return r;
}

s32 SEQ_CORE_JournalRedo(void)
{
  if( action_journal.state != SEQ_CORE_JRNL_REDOABLE )
    return -1;

  if( action_journal.scope == SEQ_CORE_JRNL_ORGANISM ) {
    // Re-apply the REVERT: re-read the checkpoint. Like UNDO, SnapshotRead
    // invalidates the journal, so re-arm UNDOABLE afterwards. (Unlike the TRACK
    // scope this is a fixed 2-way swap between two SD slots, NOT symmetric
    // against hand-edits made after an undo — a REDO of REVERT means "revert
    // again", so discarding the post-undo live state is the gesture's intent. A
    // fresh deliberate track gesture re-arms a TRACK-grain undo instead.)
    s32 r = SEQ_PATTERN_RevertRedoRead();
    if( r < 0 ) return r;
    action_journal.scope = SEQ_CORE_JRNL_ORGANISM;
    action_journal.state = SEQ_CORE_JRNL_UNDOABLE;
    return r;
  }

  if( !action_journal.after.valid )
    return -1;

  // Symmetric with UNDO: re-capture live into `before` first, so any work done
  // on the track between the undo and this redo is preserved and a following
  // UNDO steps back to it (the toggle is a true reversible 2-way swap, never a
  // silent clobber). before.track == after.track, so this re-snaps the same track.
  journal_snap(&action_journal.before, action_journal.after.track);

  s32 r = journal_restore(&action_journal.after);
  if( r < 0 ) return r;

  action_journal.state = SEQ_CORE_JRNL_UNDOABLE;
  return r;
}

// Drop the journal. Called by disk-load paths so an UNDO can't clobber a
// freshly-loaded track with pre-load bytes (the generator undo had the same
// guard; the journal subsumes it).
s32 SEQ_CORE_JournalInvalidate(void)
{
  action_journal.state = SEQ_CORE_JRNL_EMPTY;
  action_journal.scope = SEQ_CORE_JRNL_TRACK;
  action_journal.before.valid = 0;
  action_journal.after.valid = 0;
  return 0;
}

// State peek (UI gesture / harness pin). Any out pointer may be NULL. `track` is
// meaningful only for TRACK scope (organism leaves before.valid = 0).
s32 SEQ_CORE_JournalInfoGet(u8 *state, u8 *track, u8 *scope)
{
  if( state ) *state = action_journal.state;
  if( track ) *track = action_journal.before.track;
  if( scope ) *scope = action_journal.scope;
  return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Backward-compat wrappers — the deliberate verbs and the pull's transactional
// rollback keep their names; they route through the unified journal.
/////////////////////////////////////////////////////////////////////////////

// Arm site for the destructive track-grain verbs (pull etc.).
s32 SEQ_CORE_TrackUndoSnapLive(u8 track)
{
  return SEQ_CORE_JournalArm(track);
}

// One-shot consume: restore `before` and EMPTY the journal (no redo). This is
// the pull verb's transactional rollback on a mid-bulk SD failure — a failed
// pull must NOT leave a "redo the failed pull" arm. The user-facing undo goes
// through SEQ_CORE_JournalUndo (which arms redo).
s32 SEQ_CORE_TrackUndoRestore(void)
{
  s32 r = journal_restore(&action_journal.before);
  SEQ_CORE_JournalInvalidate();
  return r;
}


/////////////////////////////////////////////////////////////////////////////
// Fork: pull ONE stored track section into a live track — the RECOMBINE verb
// (track-grain load, the missing grain cell). A transfusion into the running
// organism, not a pattern switch: seq_pattern[] / seq_pattern_name[] are
// never touched and the destination group's other tracks never change.
//
// Arms the track undo with dst_track's state first (UNDO is the live safety
// net — one gesture back), then streams the section via SEQ_FILE_B_TrackRead
// and runs the group-load side-effect fan translated per-track (the §3.4
// census): sustain cancel, latched-value reset + PC/bank send, the
// UNMUTE_ON_PATTERN_CHANGE bit, and a bar-aligned restart via
// SYNC_MEASURE. RATOPC is deliberately subsumed by that restart: the group
// flow's immediate reset lands on the bar only because the handler runs at
// the boundary — a pull happens mid-bar, where an immediate reset would drop
// the track off-phase. The mixer-map coupling is skipped deliberately.
//
// Per the no-smart-default rule dst is NOT gated on "empty"; the caller's
// pick is deliberate. Pulling from an unwritten slot is refused before any
// live write (the undo then holds a snapshot identical to live state — a
// harmless restore; one-deep means the previous victim is consumed either
// way, same contract as the ENGAGE undo).
//
// MUST run in task context (MUTEX_SDCARD around the read, MUTEX_MIDIOUT
// around the PC/bank send). Returns 0 on success, negative on error
// (SEQ_FILE_B codes; live track untouched on pre-write failures).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_LoadTrackFromSlot(u8 dst_track, u8 src_bank, u8 src_pattern, u8 src_slot_track)
{
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -1;

  SEQ_CORE_TrackUndoSnapLive(dst_track);

  // Pre-generate output across the stall, then exclude ticks for the whole
  // live-write window — the proven group-change recipe (SEQ_PATTERN_Change's
  // forward-delay margin + SEQ_PATTERN_Handler's critical section; SDCARD
  // must be taken BEFORE entering the critical section or the take hangs).
  // Without the exclusion, the mid-read CC replay arms the sweep regime and
  // ticks render/emit the half-loaded source for the whole SD-read window.
  if( SEQ_BPM_IsRunning() ) {
    MUTEX_MIDIOUT_TAKE;
    SEQ_CORE_AddForwardDelay(seq_core_pattern_switch_margin_ms);
    MUTEX_MIDIOUT_GIVE;
  }

  MUTEX_SDCARD_TAKE;
  portENTER_CRITICAL();
  s32 status = SEQ_FILE_B_TrackRead(src_bank, src_pattern, src_slot_track, dst_track);
  portEXIT_CRITICAL();
  MUTEX_SDCARD_GIVE;

  if( status < 0 ) {
    // ERR_READ is the only code TrackRead can return after its first live
    // write (mid-bulk SD failure -> half-written track). Make the verb
    // transactional: put the armed victim back (the restore runs its own
    // fan). Harmless when the failure was pre-write — the snapshot is then
    // identical to live state.
    if( status == SEQ_FILE_B_ERR_READ )
      SEQ_CORE_TrackUndoRestore();
    return status;
  }

  // Force a full quiet render: the mirror is the emission source, and the
  // RenderDirtySet armed inside TrackRead can be consumed by a sweep-regime
  // tick that refreshed only a window — the organism would play stale notes
  // until the next full pass (CaptureToTrack precedent).
  seq_render_touched_ms[dst_track] = 0;
  seq_render_dirty[dst_track] = 1;
  SEQ_CORE_RenderTrack(dst_track);

  // ---- per-track census fan (mirrors SEQ_PATTERN_Load's group fan) ----

  SEQ_CORE_CancelSustainedNotes(dst_track);

  if( seq_core_options.UNMUTE_ON_PATTERN_CHANGE ) {
    u16 mask = 1 << dst_track;
    MIOS32_IRQ_Disable();
    seq_core_trk_muted &= ~mask;
    seq_core_trk_synched_mute &= ~mask;
    seq_core_trk_synched_unmute &= ~mask;
    MIOS32_IRQ_Enable();
  }

  if( !seq_core_options.PATTERN_CHANGE_DONT_RESET_LATCHED_PC )
    SEQ_LAYER_ResetLatchedValuesTrack(dst_track);

  MUTEX_MIDIOUT_TAKE;
  SEQ_LAYER_SendPCBankValues(dst_track, 0, 1);
  MUTEX_MIDIOUT_GIVE;

  // bar-aligned drop (also delivers RATOPC's musical intent, see above)
  SEQ_CORE_ManualSynchToMeasure(1 << dst_track);

  // a pull into the morph's focused group replaced a track's live CCs -> the
  // arm-time A is stale; release the morph (per-group, like the pattern switch).
  SEQ_PATTERN_PhraseMorphInvalidateGroup(dst_track / SEQ_CORE_NUM_TRACKS_PER_GROUP);

  return 0;
}


// Phase D change-detection signature (see seq_render_live_sig). Folds every live
// input that a force-dirty processor reads at render time into one u32, so a track
// re-renders only when an input ACTUALLY changes (dial sweep / held-chord change /
// transposer move) rather than unconditionally every tick. *has_live is set if the
// track carries any CHORD_MASK / TENSION / live-PITCH slot (the only processors
// whose output depends on per-tick live inputs); the returned sig is meaningful
// only then. A rolling multiply-add mixes the inputs (collisions are astronomically
// unlikely over these small ranges); a per-id tag decorrelates the slots.
//
// CORRECTNESS (the whole risk): EVERY live input a force-dirty processor reads MUST
// be folded in here, or that track renders stale = wrong pitches (silent, hard to
// notice). Over-inclusion only costs an occasional extra render. Inputs that also
// dirty through another path (slot-sync CC writes; the global-scale RenderDirtySetAll
// sites) are folded in anyway so the signature is self-sufficient and does not depend
// on auditing every writer.
#define SEQ_RENDER_SIG_PRIME 2654435761u
static inline u32 render_sig_mix(u32 sig, u32 v) { return sig * SEQ_RENDER_SIG_PRIME + v; }

static u32 render_live_sig(u8 track, u8 *has_live)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  u32 sig = 0;
  u8 live = 0;
  u8 slot;
  for(slot=0; slot<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++slot) {
    const seq_processor_slot_t *p = &seq_processor_stack[track][slot];
    if( !p->enabled )
      continue;
    switch( p->id ) {
      case SEQ_PROCESSOR_ID_CHORD_MASK:
        // Live: the held chord PC-set on the slot's bus (SEQ_MIDI_IN — never dirties;
        // the ONLY thing that catches a held-chord change). bus/strength/drum_mask
        // change via SEQ_CORE_ChordMaskSlotSync (which dirties) — folded for safety.
        live = 1;
        sig = render_sig_mix(sig, 0x01u);
        sig = render_sig_mix(sig, (u32)SEQ_MIDI_IN_BusPCSetGet(p->bus));
        sig = render_sig_mix(sig, ((u32)p->bus << 8) | (u32)p->strength);
        sig = render_sig_mix(sig, (u32)p->drum_mask);
        break;

      case SEQ_PROCESSOR_ID_TENSION:
        // Live: GRAVITY (cockpit dial / RESOLVE ramp) AND the held chord on the bus —
        // SEQ_CORE_TensionBandMask reads BOTH the chord PC-set (L2c) AND the bass note
        // (BusLowestNoteGet → L0/root), plus the global scale/root the band is built
        // from. GRIP(strength)/bus/drum_mask change via SEQ_CORE_TensionSlotSync.
        live = 1;
        sig = render_sig_mix(sig, 0x02u);
        sig = render_sig_mix(sig, (u32)(u8)seq_core_tension_gravity);
        sig = render_sig_mix(sig, (u32)SEQ_MIDI_IN_BusPCSetGet(p->bus));
        sig = render_sig_mix(sig, (u32)SEQ_MIDI_IN_BusLowestNoteGet(p->bus));
        sig = render_sig_mix(sig, ((u32)p->bus << 8) | (u32)p->strength);
        sig = render_sig_mix(sig, (u32)p->drum_mask);
        sig = render_sig_mix(sig, ((u32)seq_core_global_scale << 16)
                                | ((u32)seq_core_global_scale_root_selection << 8)
                                | (u32)seq_core_keyb_scale_root);
        break;

      case SEQ_PROCESSOR_ID_PITCH:
        // PITCH joins the force-dirty set only while its input is live (Transpose
        // playmode / global transpose) — static transpose+FTS render on events, so a
        // non-live PITCH slot is NOT in the signature (and not force-dirtied). Live:
        // the transposer note (SEQ_MIDI_IN — never dirties) + the FTS scale/root
        // globals (FORCE_SCALE path). transpose/playmode/flags change via
        // SEQ_CORE_PitchSlotSync — folded for safety (trkmode_flags.ALL covers
        // FORCE_SCALE/HOLD/FIRST_NOTE in one byte).
        if( !pitch_slot_live(track) )
          break;
        live = 1;
        sig = render_sig_mix(sig, 0x03u);
        sig = render_sig_mix(sig, (u32)SEQ_MIDI_IN_TransposerNoteGet(
                                        p->bus, tcc->trkmode_flags.HOLD,
                                        tcc->trkmode_flags.FIRST_NOTE));
        sig = render_sig_mix(sig, ((u32)tcc->transpose_oct << 4) | (u32)tcc->transpose_semi);
        sig = render_sig_mix(sig, ((u32)tcc->playmode << 8) | (u32)tcc->trkmode_flags.ALL);
        sig = render_sig_mix(sig, (u32)seq_core_global_transpose_enabled);
        sig = render_sig_mix(sig, ((u32)seq_core_global_scale << 16)
                                | ((u32)seq_core_global_scale_root_selection << 8)
                                | (u32)seq_core_keyb_scale_root);
        sig = render_sig_mix(sig, (u32)p->bus);
        break;

      default:
        break;
    }
  }
  *has_live = live;
  return sig;
}

void SEQ_CORE_RenderTracks(void)
{
  u32 tick_cyc = 0;   // total render cycles this pass (all dirty tracks)
  u8  tick_dirty = 0; // tracks actually rendered this pass
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    // Phase D sweep/quiet change-detection. chord_mask / tension / live-pitch depend
    // on live inputs (the held chord on a bus, the global GRAVITY dial, the transposer
    // note) that can change between ticks WITHOUT a source dirty — but re-rendering
    // every armed track every tick pegs the CPU (the +2 UI task starves, control
    // surface dark). Compute a signature of those live inputs and dirty the track only
    // when it changes: a static field is sig-stable → no render → UI alive even with
    // all 16 gripped; a sweep changes the sig → re-renders during the sweep only.
    u8 has_live = 0;
    u32 sig = render_live_sig(track, &has_live);
    if( has_live && sig != seq_render_live_sig[track] )
      seq_render_dirty[track] = 1;

    if( seq_render_dirty[track] ) {
      // Render-cost probe: bracket only the real render. The counter is u32 and the
      // subtraction is wrap-safe; a render that gets preempted by an IRQ counts that
      // time too (wall-clock occupancy — which is exactly what starves emission).
      u32 c0 = SEQ_CORE_DWT_CYCCNT;
      SEQ_CORE_RenderTrack(track);
      u32 dc = SEQ_CORE_DWT_CYCCNT - c0;
      tick_cyc += dc;
      ++tick_dirty;
      if( dc > seq_core_render_max_track_cyc )
        seq_core_render_max_track_cyc = dc;

      // Store the live-input signature we just rendered, so the next tick re-renders
      // only if a live input changed since. Recorded regardless of which path set
      // dirty (a source edit renders the live inputs too), so the stored value always
      // reflects the rendered output. A live input that changes DURING the render is
      // caught next tick (sig differs) — one extra render, self-correcting, never stale.
      if( has_live )
        seq_render_live_sig[track] = sig;
    }
  }

  if( tick_dirty ) {
    if( tick_cyc > seq_core_render_max_tick_cyc )
      seq_core_render_max_tick_cyc = tick_cyc;
    if( tick_dirty > seq_core_render_max_dirty )
      seq_core_render_max_dirty = tick_dirty;
    seq_core_render_total_us += tick_cyc / SEQ_CORE_DWT_CYC_PER_US;
    ++seq_core_render_tick_count;
  }
  seq_core_render_last_dirty = tick_dirty;
}


/////////////////////////////////////////////////////////////////////////////
// Phase D.0 — MSP/handler-stack high-water measurement.
//
// Standard stack-paint pattern: at startup, fill the free MSP region with a
// sentinel; later, scan from the bottom upward and the first non-sentinel word
// marks the deepest MSP excursion. _eusrstack / _estack are linker symbols
// (see etc/ld/STM32F4xx/STM32F407VG.ld). _estack is the MSP top (0x20020000);
// _eusrstack is the bottom of the ~32KB region reserved for MSP growth (see
// §A5). Paint runs once, before FreeRTOS starts, while we're still on MSP —
// after that, kernel + ISRs consume MSP and the painted bytes get overwritten
// from the top down. Tasks run on PSP so they don't touch this region.
/////////////////////////////////////////////////////////////////////////////
extern char _eusrstack[];
extern char _estack[];

#define MSP_PAINT_PATTERN  0xa5a5a5a5u
#define MSP_PAINT_MARGIN   256u  // bytes below current SP we deliberately do NOT paint

static u32 *msp_paint_lo = 0;  // inclusive
static u32 *msp_paint_hi = 0;  // exclusive

void SEQ_CORE_MSPPaint(void)
{
  u32 sp;
  __asm__ volatile ("mov %0, sp" : "=r"(sp));

  u32 lo = (u32)(uintptr_t)_eusrstack;
  // Round SP-margin down to a word boundary; never paint above current SP or
  // we'd corrupt the live frame.
  u32 hi = (sp - MSP_PAINT_MARGIN) & ~3u;

  if( hi <= lo )
    return;

  msp_paint_lo = (u32 *)(uintptr_t)lo;
  msp_paint_hi = (u32 *)(uintptr_t)hi;

  u32 *p;
  for(p=msp_paint_lo; p<msp_paint_hi; ++p)
    *p = MSP_PAINT_PATTERN;
}

u32 SEQ_CORE_MSPHighWaterBytes(void)
{
  if( !msp_paint_lo || !msp_paint_hi )
    return 0;
  u32 *p;
  // Scan from low end upward — first non-pattern word marks the deepest reach.
  for(p=msp_paint_lo; p<msp_paint_hi; ++p)
    if( *p != MSP_PAINT_PATTERN )
      return (u32)((u8 *)msp_paint_hi - (u8 *)p);
  return 0;
}

u32 SEQ_CORE_MSPPaintExtent(void)
{
  if( !msp_paint_lo || !msp_paint_hi )
    return 0;
  return (u32)((u8 *)msp_paint_hi - (u8 *)msp_paint_lo);
}

u32 SEQ_CORE_MSPPaintInitialDepth(void)
{
  if( !msp_paint_hi )
    return 0;
  return (u32)((uintptr_t)_estack - (uintptr_t)msp_paint_hi);
}

u32 SEQ_CORE_MSPPaintLo(void) { return (u32)(uintptr_t)msp_paint_lo; }
u32 SEQ_CORE_MSPPaintHi(void) { return (u32)(uintptr_t)msp_paint_hi; }


/////////////////////////////////////////////////////////////////////////////
// This function schedules a MIDI event by considering the "normal" and "Fx"
// MIDI port
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_ScheduleEvent(u8 track, seq_core_trk_t *t, seq_cc_trk_t *tcc, mios32_midi_package_t midi_package, seq_midi_out_event_type_t event_type, u32 timestamp, u32 len, u8 is_echo, seq_robotize_flags_t robotize_flags)
{
  s32 status = 0;
  mios32_midi_port_t fx_midi_port = tcc->fx_midi_port ? tcc->fx_midi_port : tcc->midi_port;

  u8 shadow_enabled = seq_core_shadow_out_chn && SEQ_UI_VisibleTrackGet() == track;

  //check that there are more than 0 additional channels, that it's not just one channel and FX starting on current channel, check disable flag, check for robotizer
  if( ! ( tcc->fx_midi_num_chn & 0x3f ) || ( ( tcc->fx_midi_num_chn & 0x3f ) == 1 && tcc->fx_midi_chn == midi_package.chn  ) || ( ( tcc->fx_midi_num_chn & 0x40 ) && !robotize_flags.DUPLICATE ) ) {
    status |= SEQ_MIDI_OUT_Send(tcc->midi_port, midi_package, event_type, timestamp, len);

    if( event_type == SEQ_MIDI_OUT_OnEvent ) { // schedule off event at same port
      midi_package.velocity = 0;
      status |= SEQ_MIDI_OUT_Send(tcc->midi_port, midi_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);
    }
  } else {
    if( event_type == SEQ_MIDI_OUT_OnEvent || event_type == SEQ_MIDI_OUT_OnOffEvent ) {
      switch( tcc->fx_midi_mode.beh ) {
      case SEQ_CORE_FX_MIDI_MODE_BEH_Alternate:
      case SEQ_CORE_FX_MIDI_MODE_BEH_AlternateSynchedEcho: {
	// forward to next channel
	if( (tcc->fx_midi_mode.beh == SEQ_CORE_FX_MIDI_MODE_BEH_AlternateSynchedEcho && !is_echo) ||
	  ++t->fx_midi_ctr > ( tcc->fx_midi_num_chn & 0x3f ) )
	  t->fx_midi_ctr = 0;

	mios32_midi_port_t midi_port;
	if( t->fx_midi_ctr == 0 ) {
	  midi_port = tcc->midi_port;
	} else {
	  midi_port = fx_midi_port;
	  midi_package.chn = (( tcc->fx_midi_chn & 0x3f ) + (t->fx_midi_ctr-1)) % 16;
	}

	status |= SEQ_MIDI_OUT_Send(midi_port, midi_package, event_type, timestamp, len);

	if( event_type == SEQ_MIDI_OUT_OnEvent ) { // schedule off event at same port
	  midi_package.velocity = 0;
	  status |= SEQ_MIDI_OUT_Send(midi_port, midi_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);
	}
      } break;
	
      case SEQ_CORE_FX_MIDI_MODE_BEH_Random: {
	// select random channel
	int ix = SEQ_RANDOM_Gen_Range(0, ( tcc->fx_midi_num_chn & 0x3f ));
	mios32_midi_port_t midi_port;
	if( ix == 0 ) {
	  midi_port = tcc->midi_port;
	} else {
	  midi_port = fx_midi_port;
	  midi_package.chn = (( tcc->fx_midi_chn & 0x3f ) + ix-1) % 16;
	}

	status |= SEQ_MIDI_OUT_Send(midi_port, midi_package, event_type, timestamp, len);

	if( event_type == SEQ_MIDI_OUT_OnEvent ) { // schedule off event at same port
	  midi_package.velocity = 0;
	  status |= SEQ_MIDI_OUT_Send(midi_port, midi_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);
	}
      } break;

      default: { // && SEQ_CORE_FX_MIDI_MODE_BEH_Forward
	// forward to all channels

	// original channel
	status |= SEQ_MIDI_OUT_Send(tcc->midi_port, midi_package, event_type, timestamp, len);

	// all other channels
	int ix;
	for(ix=0; ix < ( tcc->fx_midi_num_chn & 0x3f ); ++ix) {
	  midi_package.chn = (tcc->fx_midi_chn + ix) % 16;
	  status |= SEQ_MIDI_OUT_Send(fx_midi_port, midi_package, event_type, timestamp, len);
	}

	if( event_type == SEQ_MIDI_OUT_OnEvent ) { // schedule off event at same port
	  midi_package.velocity = 0;

	  midi_package.chn = tcc->midi_chn % 16;
	  status |= SEQ_MIDI_OUT_Send(tcc->midi_port, midi_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);

	  for(ix=0; ix< ( tcc->fx_midi_num_chn & 0x3f ); ++ix) {
	    midi_package.chn = (tcc->fx_midi_chn + ix) % 16;
	    status |= SEQ_MIDI_OUT_Send(fx_midi_port, midi_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);
	  }
	}
      } break;


      }
    } else {
      // original channel
      status |= SEQ_MIDI_OUT_Send(tcc->midi_port, midi_package, event_type, timestamp, len);

      if( event_type == SEQ_MIDI_OUT_OnEvent ) { // schedule off event at same port
	midi_package.velocity = 0;
	status |= SEQ_MIDI_OUT_Send(tcc->midi_port, midi_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);
      }

      if( tcc->fx_midi_mode.fwd_non_notes ) {
	// all other channels
	int ix;
	for(ix=0; ix < ( tcc->fx_midi_num_chn & 0x3f ); ++ix) {
	  midi_package.chn = (tcc->fx_midi_chn + ix) % 16;
	  status |= SEQ_MIDI_OUT_Send(fx_midi_port, midi_package, event_type, timestamp, len);
	}
      }
    }
  }

  // duplicate to shadow out?
  if( shadow_enabled ) {
    mios32_midi_package_t shadow_package = midi_package;
    shadow_package.chn = seq_core_shadow_out_chn - 1;

    // duplicate to shadow channel
    if( shadow_enabled ) {
      status |= SEQ_MIDI_OUT_Send(seq_core_shadow_out_port, shadow_package, event_type, timestamp, len);

      if( event_type == SEQ_MIDI_OUT_OnEvent ) { // schedule off event at same port
	shadow_package.velocity = 0;
	status |= SEQ_MIDI_OUT_Send(seq_core_shadow_out_port, shadow_package, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0);
      }
    }
  }

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// this sequencer handler is called periodically to check for new requests
// from BPM generator
/////////////////////////////////////////////////////////////////////////////
// Reset the emission-task service-gap probe: zero the peak and anchor "last service"
// to the current ISR tick. Call this immediately before the operation under test
// (e.g. a phrase capture) while the transport is running.
void SEQ_CORE_ServiceGapReset(void)
{
  seq_core_service_last_tick = SEQ_BPM_TickGet();
  seq_core_service_max_gap = 0;
}

// Peak ISR-tick gap between emission-task (SEQ_CORE_Handler) services since the last
// reset. Folds in the gap that is STILL OPEN right now: during a full freeze the handler
// never runs to record its own peak, so the live distance from the last service to the
// current ISR tick is the only witness — without this fold-in a total stall would read 0.
u32 SEQ_CORE_ServiceMaxGapGet(void)
{
  u32 pending = SEQ_BPM_TickGet() - seq_core_service_last_tick;
  return (pending > seq_core_service_max_gap) ? pending : seq_core_service_max_gap;
}

// +2 UI-task starvation probe (control-surface hang during SD writes). Same shape as the
// emission pair above; the mark runs at the top of SEQ_TASK_Period1mS (app.c).
void SEQ_CORE_UIServiceGapReset(void)
{
  seq_core_ui_service_last_tick = SEQ_BPM_TickGet();
  seq_core_ui_service_max_gap = 0;
}

u32 SEQ_CORE_UIServiceMaxGapGet(void)
{
  u32 pending = SEQ_BPM_TickGet() - seq_core_ui_service_last_tick;
  return (pending > seq_core_ui_service_max_gap) ? pending : seq_core_ui_service_max_gap;
}

// Record how long the +2 UI task was starved since it last ran. Called at the top of
// SEQ_TASK_Period1mS. Cheap; the UI task only advances this when it actually gets to run,
// so during a starving SD write the gap is left open and read back via the pending fold-in.
void SEQ_CORE_UIServiceGapMark(void)
{
  u32 now = SEQ_BPM_TickGet();
  u32 gap = now - seq_core_ui_service_last_tick;
  if( gap > seq_core_ui_service_max_gap )
    seq_core_ui_service_max_gap = gap;
  seq_core_ui_service_last_tick = now;
}

// All-16 render-cost probe (play-readiness #5). Reset zeroes the accumulators and anchors
// the window. Get reports the window in microseconds (cycles converted here so the hot path
// stays divide-free). Drive with the transport running (or step the clock) after arming the
// tracks, then read back. See CMD_RENDER_PERF.
void SEQ_CORE_RenderCostReset(void)
{
  seq_core_render_total_us = 0;
  seq_core_render_max_tick_cyc = 0;
  seq_core_render_max_track_cyc = 0;
  seq_core_render_tick_count = 0;
  seq_core_render_last_dirty = 0;
  seq_core_render_max_dirty = 0;
  seq_core_render_reset_ms = (u32)MIOS32_TIMESTAMP_Get();
}

void SEQ_CORE_RenderCostGet(u32 *total_us, u32 *max_tick_us, u32 *max_track_us,
                            u32 *tick_count, u8 *last_dirty, u32 *elapsed_ms,
                            u8 *max_dirty)
{
  if( total_us )     *total_us     = seq_core_render_total_us;
  if( max_tick_us )  *max_tick_us  = seq_core_render_max_tick_cyc / SEQ_CORE_DWT_CYC_PER_US;
  if( max_track_us ) *max_track_us = seq_core_render_max_track_cyc / SEQ_CORE_DWT_CYC_PER_US;
  if( tick_count )   *tick_count   = seq_core_render_tick_count;
  if( last_dirty )   *last_dirty   = seq_core_render_last_dirty;
  if( elapsed_ms )   *elapsed_ms   = (u32)MIOS32_TIMESTAMP_GetDelay(seq_core_render_reset_ms);
  if( max_dirty )    *max_dirty    = seq_core_render_max_dirty;
}


s32 SEQ_CORE_Handler(void)
{
  // perf probe: record how many ISR ticks elapsed since this emission handler last ran
  // (see seq_core_service_* statics above). Cheap; runs every TASK_MIDI service.
  {
    u32 now = SEQ_BPM_TickGet();
    u32 gap = now - seq_core_service_last_tick;
    if( gap > seq_core_service_max_gap )
      seq_core_service_max_gap = gap;
    seq_core_service_last_tick = now;
  }

  // handle requests

  u8 num_loops = 0;
  u8 again = 0;
  do {
    ++num_loops;

    // note: don't remove any request check - clocks won't be propagated
    // as long as any Stop/Cont/Start/SongPos event hasn't been flagged to the sequencer
    if( SEQ_BPM_ChkReqStop() ) {
      SEQ_MIDI_ROUTER_SendMIDIClockEvent(0xfc, 0);
      SEQ_CORE_PlayOffEvents();
      SEQ_MIDPLY_PlayOffEvents();

      // Complete an in-flight RESOLVE ramp: the measure it was ramping into no
      // longer exists, and RESOLVE-while-stopped already means "land now" — so
      // STOP mid-ramp lands the remaining glide at 0 immediately (Boundary
      // clears the active flag, pins 0, touches the field tracks). Without
      // this, stopping in the glide-already-at-0 window (it reaches 0 BEFORE
      // the downbeat by design) strands tension_resolve_active=1 and the NEXT
      // play's first downbeat silently eats whatever the dial was set to in
      // between (Track-1 latent bug, surfaced by the Track-2 HIL). By-ear
      // revisit allowed: freeze-at-mid-ramp (bare Cancel) is the alternative.
      SEQ_CORE_TensionResolveBoundary();

      int track;
      for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
	SEQ_RECORD_Reset(track);
      }
    }

    if( SEQ_BPM_ChkReqCont() ) {
      // release slave mute
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;

      // update delays
      SEQ_MIDI_PORT_ClkDelayUpdateAll();

      // send continue event
      SEQ_MIDI_ROUTER_SendMIDIClockEvent(0xfb, 0);

      // release pause mode
      ui_seq_pause = 0;
    }

    if( SEQ_BPM_ChkReqStart() ) {
      // release slave mute
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;

      // update delays
      SEQ_MIDI_PORT_ClkDelayUpdateAll();

      // send start event and reset sequencer
      SEQ_MIDI_ROUTER_SendMIDIClockEvent(0xfa, 0);
      SEQ_SONG_Reset(0);
      SEQ_CORE_Reset(0);
      SEQ_MIDPLY_Reset();

      // song page: start song at current edit position
      if( ui_page == SEQ_UI_PAGE_SONG ) {
	SEQ_SONG_PosSet(ui_song_edit_pos);
      }
    }

    u16 new_song_pos;
    if( SEQ_BPM_ChkReqSongPos(&new_song_pos) ) {
      // release slave mute
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;

      // update delays
      SEQ_MIDI_PORT_ClkDelayUpdateAll();

      // new position
      u32 new_tick = new_song_pos * (SEQ_BPM_PPQN_Get() / 4);
      SEQ_CORE_Reset(new_tick);
      SEQ_SONG_Reset(new_tick);

#if 0
      // fast forward to new song position
      if( new_tick ) {
	u32 bpm_tick;
	for(bpm_tick=0; bpm_tick<=new_tick; bpm_tick+=24) {
	  SEQ_BPM_TickSet(bpm_tick);
	  SEQ_CORE_Tick(bpm_tick, -1, 1); // mute all non-loopback tracks
	}
	SEQ_BPM_TickSet(new_tick);
      }
#endif
      SEQ_MIDPLY_SongPos(new_song_pos, 1);
    }

    u32 bpm_tick;
    if( SEQ_BPM_ChkReqClk(&bpm_tick) > 0 ) {
      // check all requests again after execution of this part
      again = 1;

      // it's possible to forward the sequencer on pattern changes
      // in this case bpm_tick_prefetch_req is > bpm_tick
      // in all other cases, we only generate a single tick (realtime play)
      u32 add_bpm_ticks = 0;
      if( bpm_tick_prefetch_req > bpm_tick ) {
	add_bpm_ticks = bpm_tick_prefetch_req - bpm_tick;
      }
      // invalidate request before a new one will be generated (e.g. via SEQ_SONG_NextPos())
      bpm_tick_prefetch_req = 0;

      // consider negative delay offsets: preload the appr. number of ticks
      s8 max_negative_offset = SEQ_MIDI_PORT_TickDelayMaxNegativeOffset();	
      if( max_negative_offset < 0 ) {
	s32 offset = -max_negative_offset + 3; // +3 margin
	add_bpm_ticks += offset;
      }

      // remove ticks which have already been processed before
      u32 bpm_tick_target = bpm_tick + add_bpm_ticks;
      if( !seq_core_state.FIRST_CLK && bpm_tick <= bpm_tick_prefetched ) {
	bpm_tick = bpm_tick_prefetched + 1;
      }

      // processing remaining ticks
      for(; bpm_tick<=bpm_tick_target; ++bpm_tick) {
	bpm_tick_prefetched = bpm_tick;

#if LED_PERFORMANCE_MEASURING == 1
	MIOS32_BOARD_LED_Set(0x00000001, 1);
#endif
#if STOPWATCH_PERFORMANCE_MEASURING == 1
	SEQ_STATISTICS_StopwatchReset();
#endif

	// generate MIDI events
	SEQ_CORE_Tick(bpm_tick, -1, 0);
	SEQ_MIDPLY_Tick(bpm_tick);

#if LED_PERFORMANCE_MEASURING == 1
	MIOS32_BOARD_LED_Set(0x00000001, 0);
#endif
#if STOPWATCH_PERFORMANCE_MEASURING == 1
	SEQ_STATISTICS_StopwatchCapture();
#endif

	// load new pattern/song step if reference step reached measure
	// (this code is outside SEQ_CORE_Tick() to save stack space!)
	u8 pre_ticks = SEQ_BPM_TicksFor_mS(SEQ_CORE_SwitchMarginMs()); // pattern switch fires this many ticks before the boundary; margin tracks the measured I/O (B)
	if( pre_ticks >= 95 )
	  pre_ticks = 95;
	if( (bpm_tick % 96) == (96-pre_ticks) ) {
	  if( SEQ_SONG_ActiveGet() ) {
	    // to handle the case as described under http://midibox.org/forums/topic/19774-question-about-expected-behaviour-in-song-mode/
	    // seq_core_steps_per_measure was lower than seq_core_steps_per_pattern
	    u32 song_switch_step = (seq_core_steps_per_measure < seq_core_steps_per_pattern) ? seq_core_steps_per_measure : seq_core_steps_per_pattern;
	    if( ( seq_song_guide_track && seq_song_guide_track <= SEQ_CORE_NUM_TRACKS &&
		  seq_core_state.ref_step_song == seq_cc_trk[seq_song_guide_track-1].length) ||
		(!seq_song_guide_track && seq_core_state.ref_step_song == song_switch_step) ) {
	      
	      if( seq_song_guide_track ) {
		// request synch-to-measure for all tracks
		SEQ_CORE_ManualSynchToMeasure(0xffff);
		
		// corner case: we will load new tracks and the length of the guide track could change
		// in order to ensure that the reference step jumps back to 0, we've to force this here:
		seq_core_state.FORCE_REF_STEP_RESET = 1;
	      }
	      
	      SEQ_SONG_NextPos();
	    }
	  } else {
	    // SWITCH-QUANTIZE: fire the deferred pattern change when the NEXT 16th
	    // (the one this pre-margin check leads) is a multiple of the selected
	    // grid. (bpm_tick/96) is the global 16th index; +1 = the upcoming one.
	    // Default grid 16 (1 bar) == the old steps_per_pattern boundary for a
	    // default-length pattern, so stock setups are unchanged; tighter grids
	    // jump sooner, wider grids less often. Grid is floor-clamped to the I/O.
	    u16 switch_grid = SEQ_CORE_SwitchQuantize16ths();
	    u8 grid_fires = switch_grid
	      ? ((((bpm_tick / 96) + 1) % switch_grid) == 0)             // quantize to the selected grid
	      : (seq_core_state.ref_step_pattern == seq_core_steps_per_pattern); // grid 0 (Instant): EXACT old pattern-boundary behaviour
	    if( seq_core_options.SYNCHED_PATTERN_CHANGE && grid_fires ) {
	      SEQ_PATTERN_Handler();
	    }
	  }
	}
      }
    }
  } while( again && num_loops < 10 );

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This function plays all "off" events
// Should be called on sequencer reset/restart/pause to avoid hanging notes
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_PlayOffEvents(void)
{
  // play "off events"
  SEQ_MIDI_OUT_FlushQueue();

  // clear sustain/stretch flags
  u8 track;
  seq_core_trk_t *t = &seq_core_trk[0];
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t) {
    int i;

    t->state.SUSTAINED = 0;
    t->state.STRETCHED_GL = 0;
    for(i=0; i<4; ++i)
      t->glide_notes[i] = 0;
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Resets song position of sequencer
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Reset(u32 bpm_start)
{
  ui_seq_pause = 0;
  seq_core_state.FIRST_CLK = 1;

  // Retroactive CAPTURE: a run-start voids the ring's recorded history (seeds are
  // re-minted below, step pointers reset — the old frames no longer re-simulate).
  SEQ_CORE_CaptureRingReset();

  // reset latched PB/CC values
  SEQ_LAYER_ResetLatchedValues();

  int track;
  seq_core_trk_t *t = &seq_core_trk[0];
  seq_cc_trk_t *tcc = &seq_cc_trk[0];
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t, ++tcc) {
    // clear all states (except for recording)
    {
      u8 rec_dont_overwrite_next_step = t->state.REC_DONT_OVERWRITE_NEXT_STEP;
      t->state.ALL = 0;
      t->state.REC_DONT_OVERWRITE_NEXT_STEP = rec_dont_overwrite_next_step;
    }
    SEQ_CORE_ResetTrkPos(track, t, tcc);

    t->bar = 0;
    t->layer_muted_from_midi = 0;
    t->layer_muted_from_midi_next = 0;
    t->lfo_cc_muted_from_midi = 0;
    t->lfo_cc_muted_from_midi_next = 0;
    // ensure robotize replays its anchored variation from the top of the run
    t->robotize_loop_phase = 0;
    t->robotize_pending_resync = 0;
    t->robotize_measure_ctr = 0;
    if( tcc->robotize_loop_cycles ) {
      u8 palette = tcc->robotize_palette_length;
      if( palette == 0 || palette > 16 ) palette = 16;
      u8 idx = (tcc->robotize_loop_start + tcc->robotize_loop_rotate) % palette;
      t->robotize_seed_state = tcc->robotize_bar_anchors[idx];
    }
    t->robotize_seed_snapshots[0] = t->robotize_seed_state; // seed measure-0 snapshot

    // per-track-RNG keystone: mint the random-traversal stream fresh from the
    // global RNG at run-start (|1 forces non-zero). Differs run-to-run (today's
    // feel) yet is deterministic + seekable within the run — the re-sim window.
    t->random_traverse_state = SEQ_RANDOM_Gen(0) | 1;

    // add track offset depending on start position
    if( bpm_start ) {
#if 0
      u32 step_length = ((tcc->clkdiv.value+1) * (tcc->clkdiv.TRIPLETS ? 4 : 6));
#else
      // leads to bad result with Logic Audio: it starts one step earlier and assumes 16th steps!
      u32 step_length = 96;
#endif
      u32 pos_step = (u8)((bpm_start / step_length) % ((u32)tcc->length+1));
      u32 pos_bar  = (u8)((bpm_start / step_length) / ((u32)tcc->length+1));

      // next part depends on forward/backward direction
      if( tcc->dir_mode == SEQ_CORE_TRKDIR_Backward ) {
	t->step = tcc->length - pos_step;
      } else {
	t->step = pos_step;
      }

      t->bar = pos_bar;
    }
  }

  // since timebase has been changed, ensure that Off-Events are played 
  // (otherwise they will be played much later...)
  SEQ_CORE_PlayOffEvents();

  // set BPM tick
  SEQ_BPM_TickSet(bpm_start);

  // cancel prefetch requests/counter
  bpm_tick_prefetch_req = 0;
  bpm_tick_prefetched = bpm_start;
  seq_core_recall_rephase_req = 0; // drop a pending grid re-phase on transport reset

  // cancel stop and set step request
  seq_core_state.MANUAL_TRIGGER_STOP_REQ = 0;

  // reset reference step
  seq_core_state.ref_step = (u16)((bpm_start / 96) % ((u32)seq_core_steps_per_measure+1));
  seq_core_state.ref_step_pattern = seq_core_state.ref_step;
  if( seq_song_guide_track ) {
    seq_core_state.ref_step_song = (u16)((bpm_start / 96) % ((u32)seq_cc_trk[seq_song_guide_track-1].length+1));
  } else {
    seq_core_state.ref_step_song = seq_core_state.ref_step;
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// performs a single ppqn tick
// if "export_track" is -1, all tracks will be played
// if "export_track" is between 0 and 15, only the given track + all loopback
//   tracks will be played (for MIDI file export)
// if "mute_nonloopback_tracks" is set, the "normal" tracks won't be played
// this option is used for the "fast forward" function on song position changes
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Tick(u32 bpm_tick, s8 export_track, u8 mute_nonloopback_tracks)
{
  // Phase E generator hook: per-track measure-boundary mutate-then-rewrite
  // runs BEFORE the renderer so any source changes mark the track dirty and
  // the same prologue's renderer pass picks them up before the tick body
  // reads the output mirror.
  SEQ_GENERATOR_Tick();

  // RESOLVE glide (§3): walk the GRAVITY dial toward the detent BEFORE the
  // renderer reads it, so each tick of the ramp re-renders the field tracks.
  SEQ_CORE_TensionResolveTick(bpm_tick);

  // Phase A render-cache prologue: refresh dirty tracks' output mirrors before
  // any tick read. Identity copy only in phase A; processor stack lands in B/C.
  SEQ_CORE_RenderTracks();

  // get MIDI File play mode (if set to SEQ_MIDPLY_MODE_Exclusive, all tracks will be muted)
  u8 midply_solo = SEQ_MIDPLY_RunModeGet() != 0 && SEQ_MIDPLY_ModeGet() == SEQ_MIDPLY_MODE_Exclusive;

  // increment reference step on each 16th note
  // set request flag on overrun (tracks can synch to measure)
  u8 synch_to_measure_req = 0;
  if( (bpm_tick % (384/4)) == 0 ) {
    // SWITCH-QUANTIZE (Phase 2): land a deferred phrase-recall re-phase on the
    // selected grid. The recall already loaded its content at tap; here, on the
    // grid boundary, we request a full track-position reset (same mechanism as
    // RATOPC / the bar-aligned QUANTIZE landing, just on the chosen grid). This
    // is cheap (no SD), so it is safe in the tick. (bpm_tick/96)=16th index.
    if( seq_core_recall_rephase_req ) {
      u16 rq_grid = SEQ_CORE_SwitchQuantize16ths();
      if( rq_grid == 0 || (((bpm_tick / 96)) % rq_grid) == 0 ) {
        seq_core_state.reset_trkpos_req = 0xffff;
        seq_core_recall_rephase_req = 0;
      }
    }
    seq_core_trk_t *t = &seq_core_trk[0];
    seq_cc_trk_t *tcc = &seq_cc_trk[0];
    u8 track;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t, ++tcc) {
      if( seq_core_state.reset_trkpos_req & (1 << track) ) {
	SEQ_CORE_ResetTrkPos(track, t, tcc);
	++t->bar;
      }

      // NEW: temporary layer mutes on incoming MIDI
      // take over _next mutes
      int num_steps = (seq_core_options.LIVE_LAYER_MUTE_STEPS < 2) ? 1 : (seq_core_options.LIVE_LAYER_MUTE_STEPS-2+1);
      if( (bpm_tick % (num_steps*(384/4))) == 0 ) {
	// layer mutes
	t->layer_muted_from_midi = t->layer_muted_from_midi_next;
	t->layer_muted_from_midi_next = 0;

	// the same for the LFO CC
	t->lfo_cc_muted_from_midi = t->lfo_cc_muted_from_midi_next;
	t->lfo_cc_muted_from_midi_next = 0;
      }
    }
    seq_core_state.reset_trkpos_req = 0;

    if( seq_core_state.FIRST_CLK || seq_core_state.FORCE_REF_STEP_RESET ) {
      seq_core_state.FORCE_REF_STEP_RESET = 0;
      synch_to_measure_req = 1;
      seq_core_state.ref_step = 0;
      seq_core_state.ref_step_pattern = 0;
      seq_core_state.ref_step_song = 0;
    } else {
      if( ++seq_core_state.ref_step > seq_core_steps_per_measure ) {
	seq_core_state.ref_step = 0;
      }

      if( ++seq_core_state.ref_step_pattern > seq_core_steps_per_pattern ) {
	seq_core_state.ref_step_pattern = 0;
      }

      if( seq_song_guide_track ) {
	if( ++seq_core_state.ref_step_song > seq_cc_trk[seq_song_guide_track-1].length ) {
	  seq_core_state.ref_step_song = 0;
	}
      } else {
	seq_core_state.ref_step_song = seq_core_state.ref_step;
      }

      if( SEQ_SONG_ActiveGet() ) {
	if( seq_core_state.ref_step_song == 0 )
	  synch_to_measure_req = 1;
      } else {
	if( seq_core_state.ref_step == 0 )
	  synch_to_measure_req = 1;
      }
    }

    // robotize per-musical-measure tick: independent of any track's length,
    // so polymetric/polyrhythmic tracks share the same robotize clock.
    // Master-sync (song-mode-only) runs first so any pending_resync it sets
    // is consumed by the same measure tick below.
    if( seq_core_state.ref_step == 0 ) {
      // RESOLVE lands GRAVITY exactly on the One (§3 cadence).
      SEQ_CORE_TensionResolveBoundary();

      // POSTURE-MORPH lands a pending position change on the One too — per-
      // measure granularity (no-op while disarmed / position unchanged).
      SEQ_PATTERN_PhraseMorphTick();

      if( synch_to_measure_req && SEQ_SONG_ActiveGet() && seq_song_guide_track ) {
	seq_core_trk_t *t_ms = &seq_core_trk[0];
	seq_cc_trk_t *tcc_ms = &seq_cc_trk[0];
	u8 t_idx;
	for(t_idx = 0; t_idx < SEQ_CORE_NUM_TRACKS; ++t_idx, ++t_ms, ++tcc_ms) {
	  if( tcc_ms->robotize_sync_to_master )
	    t_ms->robotize_pending_resync = 1;
	}
      }

      seq_core_trk_t *t_m = &seq_core_trk[0];
      seq_cc_trk_t *tcc_m = &seq_cc_trk[0];
      u8 t_idx;
      for(t_idx = 0; t_idx < SEQ_CORE_NUM_TRACKS; ++t_idx, ++t_m, ++tcc_m) {
	++t_m->robotize_measure_ctr;
	SEQ_CORE_RobotizeLoopBarTick(t_m, tcc_m);
      }

      // Retroactive CAPTURE: ring the visible track's generative frame for the
      // measure now starting (shares the robotize index just advanced above).
      // bpm_tick = this measure's downbeat, stamped for the while-playing tape.
      SEQ_CORE_CaptureRingTick(bpm_tick);
    }
  }

  // disable slave clock mute if not in slave mode anymore
  // or start of if measure is reached and OffOnNextMeasure has been requested
  u8 is_master = SEQ_BPM_IsMaster();
  if( is_master ||
      (synch_to_measure_req && (seq_core_slaveclk_mute == SEQ_CORE_SLAVECLK_MUTE_OffOnNextMeasure)) ) {
    // enable ports
    seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
    if( !is_master ) {
      // release pause mode
      ui_seq_pause = 0;
      // TK: this makes sense! Request synch-to-measure for all tracks so that they restart properly
      SEQ_CORE_ManualSynchToMeasure(0xffff);
    }
  }

  // if no export and no mute:
  if( export_track == -1 && !mute_nonloopback_tracks ) {
    // send FA if external restart has been requested
    if( seq_core_state.EXT_RESTART_REQ && synch_to_measure_req ) {
      seq_core_state.EXT_RESTART_REQ = 0; // remove request
      seq_ui_display_update_req = 1; // request display update
      SEQ_MIDI_ROUTER_SendMIDIClockEvent(0xfa, bpm_tick);
    }

    // send MIDI clock on each 16th tick (since we are working at 384ppqn)
    if( (bpm_tick % 16) == 0 )
      SEQ_MIDI_ROUTER_SendMIDIClockEvent(0xf8, bpm_tick);

#if !defined(MIOS32_DONT_USE_AOUT)
    // trigger DIN Sync clock with a special event (0xf9 normaly used for "MIDI tick")
    // SEQ_MIDI_PORT_NotifyMIDITx filters it before it will be forwarded to physical ports
    {
      int clkout;

      mios32_midi_package_t p;
      p.ALL = 0;
      p.type = 0x5; // Single-byte system common message
      p.evnt0 = 0xf9;

      u16 *clk_divider = (u16 *)&seq_cv_clkout_divider[0];
      for(clkout=0; clkout<SEQ_CV_NUM_CLKOUT; ++clkout, ++clk_divider) {
	if( *clk_divider && *clk_divider < 16000 && (bpm_tick % *clk_divider) == 0 ) { // TODO: dirty code, we should handle this in SEQ_CV, because only there it's known that clk_divider 0 and 0xfffd/e/f are used for special functions
	  p.evnt1 = clkout; // Transfers the Clock Output
	  SEQ_MIDI_OUT_Send(0xff, p, SEQ_MIDI_OUT_ClkEvent, bpm_tick, 0);
	}
      }
    }
#endif

    // send metronome tick on each beat if enabled
    if( seq_core_state.METRONOME && seq_core_metronome_chn && (bpm_tick % 96) == 0 && (seq_core_state.ref_step % 4) == 0 ) {
      mios32_midi_package_t p;

      p.type     = NoteOn;
      p.cable    = 15; // use tag of track #16 - unfortunately more than 16 tags are not supported
      p.event    = NoteOn;
      p.chn      = seq_core_metronome_chn-1;
      p.note     = seq_core_metronome_note_b;
      p.velocity = 96;
      u16 len = 20; // ca. 25% of a 16th

      if( seq_core_state.ref_step == 0 ) {
	if( seq_core_metronome_note_m )
	  p.note = seq_core_metronome_note_m; // if this note isn't defined, use B note instead
	p.velocity = 127;
      }

      if( p.note )
	SEQ_MIDI_OUT_Send(seq_core_metronome_port, p, SEQ_MIDI_OUT_OnOffEvent, bpm_tick, len);
    }
  }

  // delayed mute
  if( synch_to_measure_req ) {
    // the priority handling for each individual track ensures
    // that mute/unmute will be done depending on the current mute state
    // unmute has higher priority than mute
    seq_core_trk_muted |= (seq_core_trk_synched_mute & ~seq_core_trk_muted);
    seq_core_trk_synched_mute = 0;

    seq_core_trk_muted &= ~(seq_core_trk_synched_unmute & seq_core_trk_muted);
    seq_core_trk_synched_unmute = 0;
  }

  // process all tracks
  // first the loopback port Bus1-4, thereafter parameters sent to common MIDI ports
  int round;
  for(round=0; round<2; ++round) {
    seq_core_trk_t *t = &seq_core_trk[0];
    seq_cc_trk_t *tcc = &seq_cc_trk[0];
    int track;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++t, ++tcc, ++track) {

      seq_robotize_flags_t robotize_flags;
      robotize_flags.ALL = 0;

      // round 0: loopback port Bus1-4, round 1: remaining ports
      u8 loopback_port = (tcc->midi_port & 0xf0) == 0xf0;
      if( (!round && !loopback_port) || (round && loopback_port) )
	continue;

      // for MIDI file export: (export_track != -1): only given track + all loopback tracks will be played
      if( round && export_track != -1 && export_track != track )
	continue;

      // recording enabled for this track?
#ifndef MBSEQV4L
      u8 track_record_enabled = (seq_record_state.ENABLED && (SEQ_UI_VisibleTrackGet() == track)) ? 1 : 0;
#else
      u8 track_record_enabled = (seq_record_state.ENABLED && (seq_record_state.ARMED_TRACKS & (1 << track)) != 0) ? 1 : 0;
#endif

      // calculate step length
      u16 step_length = ((tcc->clkdiv.value+1) * (tcc->clkdiv.TRIPLETS ? 4 : 6));

      // handle LFO effect
      SEQ_LFO_HandleTrk(track, step_length, bpm_tick);

      // send LFO CC (if enabled and not muted)
      if( !(seq_core_trk_muted & (1 << track)) && !seq_core_slaveclk_mute && !t->lfo_cc_muted_from_midi ) {
	mios32_midi_package_t p;
	if( SEQ_LFO_FastCC_Event(track, bpm_tick, &p, 0) > 0 ) {
	  if( loopback_port )
	    SEQ_MIDI_IN_BusReceive(tcc->midi_port & 0x0f, p, 1); // forward to MIDI IN handler immediately
	  else
	    SEQ_CORE_ScheduleEvent(track, t, tcc, p, SEQ_MIDI_OUT_CCEvent, bpm_tick, 0, 0, robotize_flags);
	}
      }

      // sustained note: play off event if sustain mode has been disabled and no stretched gatelength
      if( t->state.SUSTAINED && (t->state.CANCEL_SUSTAIN_REQ || (!tcc->trkmode_flags.SUSTAIN && !t->state.ROBOSUSTAINED && !t->state.STRETCHED_GL)) ) {
	int i;

	// important: play Note Off before new Note On to avoid that glide is triggered on the synth
	SEQ_MIDI_OUT_ReSchedule(track, SEQ_MIDI_OUT_OffEvent, bpm_tick ? (bpm_tick-1) : 0,
				track_record_enabled ? seq_record_played_notes : NULL);
	// clear state flag and note storage
	t->state.SUSTAINED = 0;
	for(i=0; i<4; ++i)
	  t->glide_notes[i] = 0;
      }
      t->state.CANCEL_SUSTAIN_REQ = 0;

      // if "synch to measure" flag set: reset track if master has reached the selected number of steps
      // MEMO: we could also provide the option to synch to another track
      if( synch_to_measure_req && (tcc->clkdiv.SYNCH_TO_MEASURE || t->state.SYNC_MEASURE) ) {
        SEQ_CORE_ResetTrkPos(track, t, tcc);
	++t->bar;
      }

      u8 mute_this_step = 0;
      u8 next_step_event = t->state.FIRST_CLK || bpm_tick >= t->timestamp_next_step;

      // step trigger: only play the new step if transposer has a note
      if( tcc->trkmode_flags.STEP_TRG ) {
	if( !t->state.TRIGGER_NEXT_STEP_REQ ) {
	  next_step_event = 0;
	}
      }
      t->state.TRIGGER_NEXT_STEP_REQ = 0;

      if( next_step_event ) {

	{
          // take over new step length
          t->step_length = step_length;

	  // set timestamp of next step w/o groove delay (reference timestamp)
	  if( t->state.FIRST_CLK )
	    t->timestamp_next_step_ref = bpm_tick + t->step_length;
	  else
	    t->timestamp_next_step_ref += t->step_length;

	  // increment step if not in arpeggiator mode or arp position == 0
	  u8 inc_step = tcc->playmode != SEQ_CORE_TRKMODE_Arpeggiator || !t->arp_pos;

	  // wrap step position around length - especially required for "section selection" later,
	  // which can set t->step beyond tcc->length+1
	  u8 prev_step = (u8)((int)t->step % ((int)tcc->length + 1));
	  t->step = prev_step; // store back wrapped step position

	  u8 skip_ctr = 0;
	  do {
	    if( t->state.MANUAL_STEP_REQ ) {
	      // manual step requested
	      t->state.MANUAL_STEP_REQ = 0;
	      t->step = t->manual_step;
	      t->step_saved = t->manual_step;
	      t->arp_pos = 0;
	    } else if( tcc->clkdiv.MANUAL ) {
	      // if clkdiv MANUAL mode: step was not requested, skip it!
	      mute_this_step = 1;
	    } else {
	      // determine next step depending on direction mode
	      if( !t->state.FIRST_CLK && inc_step )
		SEQ_CORE_NextStep(t, tcc, 0, 0); // 0, 0=with progression, not reverse
	      else {
		// ensure that position reset request is cleared
		t->state.POS_RESET = 0;
	      }
	    }
	    
	    // clear "first clock" flag (on following clock ticks we can continue as usual)
	    t->state.FIRST_CLK = 0;
  
	    // if skip flag set for this flag: try again
	    if( SEQ_TRG_SkipGet(track, t->step, 0) )
	      ++skip_ctr;
	    else
	      break;
	    
	  } while( skip_ctr < 32 ); // try 32 times maximum

	  // Section selection
	  // Approach:
	  // o enabled with t->play_section > 0
	  // o section width matches with the Track length, which means that the sequencer will
	  //   play steps beyond the "last step" with t->play_section > 0
	  // o section controlled via UI, MIDI Keyboard or BLM
	  // o lower priority than global loop mode
	  if( t->play_section > 0 ) {
	    // note: SEQ_TRG_Get() will return 0 if t->step beyond total track size - no need to consider this here
	    int step_offset = t->play_section * ((int)tcc->length+1);
	    t->step += step_offset;
	  }

	  // global loop mode handling
	  // requirements:
	  // o loop all or only select tracks
	  // o allow to loop the step view (16 step window) or a definable number of steps
	  // o wrap step position properly when loop mode is activated so that this
	  //   doesn't "dirsturb" the sequence output (ensure that the track doesn't get out of sync)
	  if( seq_core_state.LOOP ) {
	    u8 loop_active = 0;
	    int step_offset = 0;
	    
	    switch( seq_core_glb_loop_mode ) {
	    case SEQ_CORE_LOOP_MODE_ALL_TRACKS_STATIC:
	      loop_active = 1;
	      break;

	    case SEQ_CORE_LOOP_MODE_SELECTED_TRACK_STATIC:
	      if( SEQ_UI_IsSelectedTrack(track) )
		loop_active = 1;
	      break;

	    case SEQ_CORE_LOOP_MODE_ALL_TRACKS_VIEW:
	      loop_active = 1;
	      // no break!

	    case SEQ_CORE_LOOP_MODE_SELECTED_TRACK_VIEW:
	      if( SEQ_UI_IsSelectedTrack(track) )
		loop_active = 1;

	      step_offset = 16 * ui_selected_step_view;
	      break;
	    }

	    if( loop_active ) {
	      // wrap step position within given boundaries if required
	      step_offset += seq_core_glb_loop_offset;
	      step_offset %= ((int)tcc->length+1);

	      int loop_steps = seq_core_glb_loop_steps + 1;
	      int max_steps = (int)tcc->length + 1;
	      if( loop_steps > max_steps )
		loop_steps = max_steps;

	      int new_step = (int)t->step;
	      new_step = step_offset + ((new_step-step_offset) % loop_steps);

	      if( new_step > tcc->length )
		new_step = step_offset;
	      t->step = new_step;
	    }
	  }

	  // calculate number of cycles to next step
	  if( tcc->groove_style.sync_to_track ) {
	    t->timestamp_next_step = t->timestamp_next_step_ref + SEQ_GROOVE_DelayGet(track, t->step + 1);
	  } else {
	    t->timestamp_next_step = t->timestamp_next_step_ref + SEQ_GROOVE_DelayGet(track, seq_core_state.ref_step + 1);
	  }

	  if( !mute_this_step && !seq_record_options.FWD_MIDI && track_record_enabled ) { // if not already skipped (e.g. MANUAL mode)
	    mute_this_step = t->state.REC_DONT_OVERWRITE_NEXT_STEP;

	    if( seq_core_state.FIRST_CLK && seq_record_options.AUTO_START && seq_record_options.FWD_MIDI )
	      mute_this_step = 1; // mute initial step which is going to be recorded
	  }

	  // forward new step to recording function (only used in live recording mode)
	  if( track_record_enabled )
	    SEQ_RECORD_NewStep(track, prev_step, t->step, bpm_tick);

	  // forward to live function (for repeats)
	  // if it returns 1, the step won't be played
	  if( SEQ_LIVE_NewStep(track, prev_step, t->step, bpm_tick) == 1 )
	    mute_this_step = 1;

	  // inform UI about a new step (UI will clear this variable)
	  seq_core_step_update_req = 1;
	}

        // solo function: don't play MIDI event if track not selected
        // mute function
        // track disabled
	// mute for non-loopback tracks activated
	// MIDI player in exclusive mode
	// Record Mode, new step and FWD_MIDI off
	u8 track_soloed = seq_core_trk_soloed && (seq_core_trk_soloed & (1 << track));
        if( (!seq_core_trk_soloed && seq_ui_button_state.SOLO && !SEQ_UI_IsSelectedTrack(track)) ||
	    (seq_core_trk_soloed && !track_soloed) ||
	    (!track_soloed && (seq_core_trk_muted & (1 << track))) || // Track Mute function
	    seq_core_slaveclk_mute || // Slave Clock Mute Function
	    SEQ_MIDI_PORT_OutMuteGet(tcc->midi_port) || // Port Mute Function
	    tcc->playmode == SEQ_CORE_TRKMODE_Off || // track disabled
	    (round && mute_nonloopback_tracks) || // all non-loopback tracks should be muted
	    midply_solo || // MIDI player in exclusive mode
	    mute_this_step ) { // Record Mode, new step and FWD_MIDI off

	  if( t->state.STRETCHED_GL || t->state.SUSTAINED ) {
	    int i;

	    if( !t->state.STRETCHED_GL ) // important: play Note Off before new Note On to avoid that glide is triggered on the synth
	      SEQ_MIDI_OUT_ReSchedule(track, SEQ_MIDI_OUT_OffEvent, bpm_tick ? (bpm_tick-1) : 0,
				      track_record_enabled ? seq_record_played_notes : NULL);
	    else // Glide
	      SEQ_MIDI_OUT_ReSchedule(track, SEQ_MIDI_OUT_OffEvent, bpm_tick,
				      track_record_enabled ? seq_record_played_notes : NULL);

	    // clear state flags and note storage
	    t->state.STRETCHED_GL = 0;
	    t->state.SUSTAINED = 0;
	    for(i=0; i<4; ++i)
	      t->glide_notes[i] = 0;
	  }

	  continue;
	}

	// parameter layer mute flags (only if not in drum mode)
	u16 layer_muted = (tcc->event_mode != SEQ_EVENT_MODE_Drum) ? (t->layer_muted | t->layer_muted_from_midi) : 0;

        // if random gate trigger set: play step with 1:1 probability
        if( SEQ_TRG_RandomGateGet(track, t->step, 0) && (SEQ_RANDOM_Gen(0) & 1) )
	  continue;

	// check probability if not in drum mode
	// if probability < 100: play step with given probability
	// in drum mode, the probability is checked for each individual instrument inside the layer event loop
	if( tcc->event_mode != SEQ_EVENT_MODE_Drum ) {
	  u8 rnd_probability;
	  if( (rnd_probability=SEQ_PAR_ProbabilityGet(track, t->step, 0, layer_muted)) < 100 &&
	      SEQ_RANDOM_Gen_Range(0, 99) >= rnd_probability )
	    continue;
	}

	// store last glide notes before they will be cleared
	//memcpy(last_glide_notes, t->glide_notes, 4*4);
	// this will be a bit faster (memcpy copies bytes, by copying words we save some time)
	u32 prev_glide_notes[4];
	u32 next_glide_notes[4];
	{
	  u32 *src_ptr = (u32 *)&t->glide_notes[0];
	  u32 *dst_ptr = (u32 *)&prev_glide_notes[0];
	  u32 *next_ptr = (u32 *)&next_glide_notes[0];
	  int i;
	  for(i=0; i<4; ++i) {
	    *dst_ptr++ = *src_ptr++;
	    *next_ptr++ = 0;
	  }
	}
	
	// Loopback Port: propagate root&scale if assigned to parameter layer
	if( loopback_port ) {
	  // Track 2: FTS lives in the render stack, so a global scale/root move
	  // must dirty the armed tracks (guarded on change — an unconditional
	  // SetAll here would force 16 full renders every step this track plays).
	  // Renders pick it up in the next tick's prologue batch.
	  if( tcc->link_par_layer_scale >= 0 ) {
	    u8 scale = SEQ_PAR_Get(track, t->step, tcc->link_par_layer_scale, 0);
	    if( scale > 0 && (u8)(scale - 1) != seq_core_global_scale ) {
	      seq_core_global_scale = scale - 1;
	      SEQ_CORE_RenderDirtySetAll();
	    }
	  }

	  if( tcc->link_par_layer_root > 0 ) {
	    u8 root = SEQ_PAR_Get(track, t->step, tcc->link_par_layer_root, 0) % 13;
	    if( root > 0 && (u8)(root - 1) != seq_core_global_scale_root_selection ) {
	      seq_core_global_scale_root_selection = root - 1;
	      SEQ_CORE_RenderDirtySetAll();
	    }
	  }
	}

#ifdef MBSEQV4P
        seq_layer_evnt_t layer_events[83];
        s32 number_of_events = 0;
	number_of_events = SEQ_LAYER_GetEventsPlus(track, t->step, layer_events, 0, 1);
#else
        seq_layer_evnt_t layer_events[16];
        s32 number_of_events = 0;
	number_of_events = SEQ_LAYER_GetEvents(track, t->step, layer_events, 0, 1);
#endif

	if( number_of_events == 0
	    && t->state.STRETCHED_GL && t->state.SUSTAINED
	    && tcc->playmode == SEQ_CORE_TRKMODE_Transpose
	    && tcc->event_mode != SEQ_EVENT_MODE_Drum
	    && seq_processor_stack[track][SEQ_CORE_PITCH_SLOT].enabled
	    && SEQ_MIDI_IN_TransposerNoteGet(tcc->busasg.bus, tcc->trkmode_flags.HOLD,
	                                     tcc->trkmode_flags.FIRST_NOTE) < 0 ) {
	  // Track 2: transposer-with-no-key renders RESTS into the mirror, so
	  // GetEvents returns no events here and the legacy glide-release path
	  // (velocity-0 events → gen_off_events in the first pass) can't run —
	  // a glided note would ring until the next keyed step. Release the held
	  // glide at the step boundary instead. Genuine source rests never took
	  // that path (no events pre-migration either) and still glide through.
	  SEQ_MIDI_OUT_ReSchedule(track, SEQ_MIDI_OUT_OffEvent,
	                          bpm_tick + t->bpm_tick_delay + t->step_length,
	                          track_record_enabled ? seq_record_played_notes : NULL);
	  t->state.SUSTAINED = 0;
	  t->state.STRETCHED_GL = 0;
	  {
	    u32 *dst_ptr = (u32 *)&t->glide_notes[0];
	    int gi;
	    for(gi=0; gi<4; ++gi)
	      *dst_ptr++ = 0;
	  }
	}

	if( number_of_events > 0 ) {
	  int i;

	  //////////////////////////////////////////////////////////////////////////////////////////
	  // First pass: handle length and apply functions which modify the MIDI events
	  //////////////////////////////////////////////////////////////////////////////////////////
	  u16 prev_bpm_tick_delay = t->bpm_tick_delay;
	  u8 gen_on_events = 0; // new On Events will be generated
	  u8 gen_sustained_events = 0; // new sustained On Events will be generated
	  u8 gen_off_events = 0; // Off Events of previous step will be generated, the variable contains the remaining gatelength

          seq_layer_evnt_t *e = &layer_events[0];
          for(i=0; i<number_of_events; ++e, ++i) {
            mios32_midi_package_t *p = &e->midi_package;

	    // instrument layers only used for drum tracks
	    u8 instrument = (tcc->event_mode == SEQ_EVENT_MODE_Drum) ? e->layer_tag : 0;

	    // individual for each instrument in drum mode:
	    // if probability < 100: play step with given probability
	    if( tcc->event_mode == SEQ_EVENT_MODE_Drum ) {
	      u8 rnd_probability;
	      if( (rnd_probability=SEQ_PAR_ProbabilityGet(track, t->step, instrument, layer_muted)) < 100 &&
		  SEQ_RANDOM_Gen_Range(0, 99) >= rnd_probability )
		continue;
	    }

	    // get nofx flag
	    robotize_flags = SEQ_ROBOTIZE_Event(track, t->step, e);
	    u8 no_fx = SEQ_TRG_NoFxGet(track, t->step, instrument);

	    // get nth trigger flag
	    // note: this check will be done again during the second pass for some triggers which are not handled during first pass
	    u8 nth_trigger = 0;
	    {
	      u8 nth_variant = 0; // Nth1 or Nth2
	      u8 nth_value = SEQ_PAR_Nth1ValueGet(track, t->step, instrument, layer_muted);
	      if( !nth_value ) {
		nth_variant = 1;
		nth_value = SEQ_PAR_Nth2ValueGet(track, t->step, instrument, layer_muted);
	      }

	      if( nth_value ) {
		int bar = nth_value & 0xf;
		int trigger = nth_variant ? ((t->bar % (bar+1)) == bar) : ((t->bar % (bar+1)) == 0);

		int mode = (nth_value >> 4) & 0x7;
		if( mode == SEQ_PAR_TYPE_NTH_PLAY ) {
		  if( !trigger )
		    continue; // step not played
		} else if( mode == SEQ_PAR_TYPE_NTH_MUTE ) {
		  if( trigger )
		    continue; // step not played
		} else if( mode == SEQ_PAR_TYPE_NTH_FX ) {
		  if( !trigger )
		    no_fx = 1;
		} else if( mode == SEQ_PAR_TYPE_NTH_NO_FX ) {		  
		  if( trigger )
		    no_fx = 1;
		} else {
		  if( trigger )
		    nth_trigger = mode;
		}
	      }
	    }

            // Track 2: the pitch chain (transpose + force-to-scale; the note
            // limit follows in Stage B) lives in the render stack — the mirror
            // value this event was built from is already the heard pitch. The
            // legacy emission chain remains only for what the stack cannot
            // represent: Arpeggiator playmode (multi-arp runtime state; fenced,
            // user call 2026-06-10), Drum event mode (0 is lay_const fallback
            // there, not a rest — see the processor section header), and Chord
            // par layers (the par byte is a chord index — pitch only exists
            // post-expansion).
            u8 legacy_pitch = (tcc->playmode == SEQ_CORE_TRKMODE_Arpeggiator)
                           || (tcc->event_mode == SEQ_EVENT_MODE_Drum);
            if( !legacy_pitch && tcc->event_mode != SEQ_EVENT_MODE_Drum ) {
              seq_par_layer_type_t lt = (seq_par_layer_type_t)tcc->lay_const[e->layer_tag & 0x0f];
              legacy_pitch = (lt == SEQ_PAR_Type_Chord1 || lt == SEQ_PAR_Type_Chord2
                              || lt == SEQ_PAR_Type_Chord3);
            }

            if( legacy_pitch ) {
              // transpose notes/CCs (legacy emission chain)
              SEQ_CORE_Transpose(track, instrument, t, tcc, p);
            } else if( p->type == NoteOn && p->note == 0 ) {
              // disabled note — was SEQ_CORE_Transpose's job before migration
              p->velocity = 0;
            }

            // glide trigger
            if( e->len > 0 && tcc->event_mode != SEQ_EVENT_MODE_Drum ) {
	      if( SEQ_TRG_GlideGet(track, t->step, instrument) )
		e->len = 96; // Glide
            }

	    // if glided note already played: omit new note event by setting velocity to 0
	    if( t->state.STRETCHED_GL ) {
	      u32 ix = p->note / 32;
	      u32 mask = (1 << (p->note % 32));
	      if( prev_glide_notes[ix] & mask ) {
		if( e->len >= 96 ) {
		  next_glide_notes[ix] |= mask;
		}
		p->velocity = 0;
	      }
	    }
  
            // skip if velocity has been cleared by transpose or glide function
            // (e.g. no key pressed in transpose mode)
            if( p->type == NoteOn && !p->velocity ) {
	      // stretched note, length < 96: queue off event
	      if( t->state.STRETCHED_GL && t->state.SUSTAINED && (e->len < 96) )
		gen_off_events = (t->step_length * e->len) / 96;
	      continue;
	    }

	    // get delay of step (0..95)
	    // note: negative delays stored in step parameters would require to pre-generate bpm_ticks, 
	    // which would reduce the immediate response on value/trigger changes
	    // therefore negative delays are only supported for groove patterns, and they are
	    // applied over the whole track (e.g. drum mode: all instruments of the appr. track)
	    t->bpm_tick_delay = SEQ_PAR_StepDelayGet(track, t->step, instrument, layer_muted);

	    // scale delay (0..95) over next clock counter to consider the selected clock divider
	    if( t->bpm_tick_delay )
	      t->bpm_tick_delay = (t->bpm_tick_delay * t->step_length) / 96;


            if( p->type != NoteOn ) {
	      // apply Pre-FX
	      if( !no_fx ) {
		SEQ_HUMANIZE_Event(track, t->step, e);

		if( !robotize_flags.NOFX ) {
                  SEQ_LFO_Event(track, e);
                }
	      }

            } else if( p->velocity && (e->len >= 0) ) {
	      // Note Event

	      // groove it
	      if( tcc->groove_style.sync_to_track ) {
		SEQ_GROOVE_Event(track, t->step, e);
	      } else {
		SEQ_GROOVE_Event(track, seq_core_state.ref_step, e);
	      }

	      // apply Pre-FX before force-to-scale
	      if( !no_fx ) {
		u8 prefx_note = p->note;
		SEQ_HUMANIZE_Event(track, t->step, e);

		if( !robotize_flags.NOFX ) {
		  SEQ_LFO_Event(track, e);
		}

		// Track 2 Stage C: FTS + the note limit live in the render
		// stack, so an emission-side note mutator (humanize-note /
		// LFO-note) would land off-scale / out-of-window un-caught.
		// Narrow re-snap + re-fold ONLY when a mutator actually moved
		// the note — stack output (including a TENSION push) passes
		// through untouched otherwise. Robotize needs no scale
		// carve-out: it walks scale degrees itself under FORCE_SCALE.
		// Legacy events skip this — their full chain runs just below.
		if( !legacy_pitch && p->note != prefx_note ) {
		  if( tcc->trkmode_flags.FORCE_SCALE ) {
		    u8 scale, root_selection, root;
		    SEQ_CORE_FTS_GetScaleAndRoot(track, t->step, instrument, tcc, &scale, &root_selection, &root);
		    SEQ_SCALE_Note(p, scale, root);
		  }
		  if( tcc->limit_lower || tcc->limit_upper )
		    SEQ_CORE_Limit(t, tcc, e); // fold the mutated note back into the window
		}
	      }

	      t->state.ROBOSUSTAINED = ( robotize_flags.SUSTAIN ) ? 1 : 0 ;// set robosustain flag

	      // force to scale — legacy emission events only (arp / chord
	      // expansion); stack-rendered notes are already snapped in the mirror
	      if( legacy_pitch && tcc->trkmode_flags.FORCE_SCALE ) {
		u8 scale, root_selection, root;
		SEQ_CORE_FTS_GetScaleAndRoot(track, t->step, instrument, tcc, &scale, &root_selection, &root);
		SEQ_SCALE_Note(p, scale, root);
	      }

	      // apply Pre-FX after force-to-scale — legacy emission events only
	      // (arp / drum / chord expansion); stack-rendered notes are already
	      // folded by the LIMIT slot (Track 2 Stage B)
	      if( legacy_pitch && !no_fx ) {
		SEQ_CORE_Limit(t, tcc, e); // should be the last Fx in the chain!
	      }

	      // force velocity to 0x7f (drum mode: selectable value) if accent flag set
	      if( nth_trigger == SEQ_PAR_TYPE_NTH_ACCENT || SEQ_TRG_AccentGet(track, t->step, instrument) ) {
		if( tcc->event_mode == SEQ_EVENT_MODE_Drum )
		  p->velocity = tcc->lay_const[2*16 + i];
		else
		  p->velocity = 0x7f;
	      }

	      // sustained or stretched note: play off event of previous step
	      if( t->state.SUSTAINED )
		gen_off_events = 1;

	      if( tcc->trkmode_flags.SUSTAIN || t->state.ROBOSUSTAINED || e->len >= 96 )
		gen_sustained_events = 1;
	      else {
		// generate common On event with given length
		gen_on_events = 1;
	      }
	    } else if( t->state.STRETCHED_GL && t->state.SUSTAINED && (e->len < 96) ) {
	      // stretched note, length < 96: queue off events
	      gen_off_events = (t->step_length * e->len) / 96;
	    }
	  }

	  // should Note Off events be played before new events are queued?
	  if( gen_off_events ) {
	    u32 rescheduled_tick = bpm_tick + prev_bpm_tick_delay + gen_off_events;
	    if( !t->state.STRETCHED_GL ) // important: play Note Off before new Note On to avoid that glide is triggered on the synth
	      rescheduled_tick -= 1;

	    SEQ_MIDI_OUT_ReSchedule(track, SEQ_MIDI_OUT_OffEvent, rescheduled_tick,
				    track_record_enabled ? seq_record_played_notes : (t->state.STRETCHED_GL ? next_glide_notes : NULL));

	    // clear state flag and note storage
	    t->state.SUSTAINED = 0;
	    if( track_record_enabled || !t->state.STRETCHED_GL ) {
	      t->state.STRETCHED_GL = 0;

	      u32 *dst_ptr = (u32 *)&t->glide_notes[0];
	      int i;
	      for(i=0; i<4; ++i) {
		*dst_ptr++ = 0;
	      }
	    } else if( t->state.STRETCHED_GL ) {
	      // improved glide handling for polyphonic steps
	      u8 any_glide = 0;
	      u32 *src_ptr = (u32 *)&next_glide_notes[0];
	      u32 *dst_ptr = (u32 *)&t->glide_notes[0];
	      int i;
	      for(i=0; i<4; ++i) {
		if( *src_ptr )
		  any_glide |= 1;

		*dst_ptr++ = *src_ptr++;
	      }

	      if( !any_glide )
		t->state.STRETCHED_GL = 0;
	    }
	  }


	  //////////////////////////////////////////////////////////////////////////////////////////
	  // Second pass: schedule new events
	  //////////////////////////////////////////////////////////////////////////////////////////
          e = &layer_events[0];
	  u8 reset_stacks_done = 0;
          for(i=0; i<number_of_events; ++e, ++i) {
            mios32_midi_package_t *p = &e->midi_package;

	    // instrument layers only used for drum tracks
	    u8 instrument = (tcc->event_mode == SEQ_EVENT_MODE_Drum) ? e->layer_tag : 0;

	    robotize_flags = SEQ_ROBOTIZE_Event(track, t->step, e);
	    u8 no_fx = SEQ_TRG_NoFxGet(track, t->step, instrument);

	    // get nth trigger flag
	    // note: this check was already done during first pass, do it here again for triggers which are handled in the second pass
	    u8 nth_trigger = 0;
	    {
	      u8 nth_variant = 0; // Nth1 or Nth2
	      u8 nth_value = SEQ_PAR_Nth1ValueGet(track, t->step, instrument, layer_muted);
	      if( !nth_value ) {
		nth_variant = 1;
		nth_value = SEQ_PAR_Nth2ValueGet(track, t->step, instrument, layer_muted);
	      }

	      if( nth_value ) {
		int bar = nth_value & 0xf;
		int trigger = nth_variant ? ((t->bar % (bar+1)) == bar) : ((t->bar % (bar+1)) == 0);

		int mode = (nth_value >> 4) & 0x7;
		if( mode == SEQ_PAR_TYPE_NTH_PLAY ) {
		  if( !trigger )
		    continue; // step not played
		} else if( mode == SEQ_PAR_TYPE_NTH_MUTE ) {
		  if( trigger )
		    continue; // step not played
		} else if( mode == SEQ_PAR_TYPE_NTH_FX ) {
		  if( !trigger )
		    no_fx = 1;
		} else if( mode == SEQ_PAR_TYPE_NTH_NO_FX ) {		  
		  if( trigger )
		    no_fx = 1;
		} else {
		  if( trigger )
		    nth_trigger = mode;
		}
	      }
	    }

	    if( p->type != NoteOn ) {
	      // e.g. CC, PitchBend, ProgramChange
	      if( loopback_port )
		SEQ_MIDI_IN_BusReceive(tcc->midi_port & 0x0f, *p, 1); // forward to MIDI IN handler immediately
	      else
		SEQ_CORE_ScheduleEvent(track, t, tcc, *p, SEQ_MIDI_OUT_CCEvent, bpm_tick + t->bpm_tick_delay, 0, 0, robotize_flags);
	      t->vu_meter = 0x7f; // for visualisation in mute menu
	    } else {
	      // skip in record mode if the same note is already played
	      if( track_record_enabled && t->state.STRETCHED_GL &&
		  (seq_record_played_notes[p->note>>5] & (1 << (p->note&0x1f))) )
		continue;

	      // skip in glide mode if note stretched
	      if( t->state.STRETCHED_GL &&
		  (next_glide_notes[p->note>>5] & (1 << (p->note&0x1f))) )
		continue;

	      // sustained/glided note: play note at timestamp, and queue off event at 0xffffffff (so that it can be re-scheduled)		
	      if( gen_sustained_events ) {
		// for visualisation in mute menu
		t->vu_meter = p->velocity;

		if( loopback_port ) {
		  if( !reset_stacks_done ) {
		    // reset current stack
		    SEQ_MIDI_IN_ResetSingleTransArpStacks(tcc->midi_port & 0x0f);
		    reset_stacks_done = 1;
		  }
		  // forward to MIDI IN handler immediately
		  SEQ_MIDI_IN_BusReceive(tcc->midi_port & 0x0f, *p, 1);
		} else {
		  u32 scheduled_tick = bpm_tick + t->bpm_tick_delay;

		  // glide: if same note already played, play the new one a tick later for 
		  // proper handling of "fingered portamento" function on some synths
		  if( prev_glide_notes[p->note / 32] & (1 << (p->note % 32)) )
		    scheduled_tick += 1;

		  // Note On (the Note Off will be prepared as well in SEQ_CORE_ScheduleEvent)
		  SEQ_CORE_ScheduleEvent(track, t, tcc, *p, SEQ_MIDI_OUT_OnEvent, scheduled_tick, 0, 0, robotize_flags);

		  // apply Post-FX
		  if( !no_fx && !robotize_flags.NOFX ) {
		    u8 local_gatelength = 95; // echo only with reduced gatelength to avoid killed notes

		    SEQ_CORE_Echo(track, instrument, t, tcc, *p, bpm_tick + t->bpm_tick_delay, local_gatelength, robotize_flags);
		  }
		}

		// notify stretched gatelength if not in sustain mode
		t->state.SUSTAINED = 1;
		if( !tcc->trkmode_flags.SUSTAIN && !t->state.ROBOSUSTAINED ) {
		  t->state.STRETCHED_GL = 1;
		  // store glide note number in 128 bit array for later checks
		  t->glide_notes[p->note / 32] |= (1 << (p->note % 32));
		}

	      } else if( gen_on_events ) {
		// for visualisation in mute menu
		t->vu_meter = p->velocity;

		if( loopback_port ) {
		  if( !reset_stacks_done ) {
		    reset_stacks_done = 1;
		    // reset current stack
		    SEQ_MIDI_IN_ResetSingleTransArpStacks(tcc->midi_port & 0x0f);
		  }
		  // forward to MIDI IN handler immediately
		  SEQ_MIDI_IN_BusReceive(tcc->midi_port & 0x0f, *p, 1);
		  // multi triggers, but also echo not possible on loopback ports
		} else {
		  u16 gatelength = e->len;
		  u8 triggers = 1;

		  // roll/flam?
		  // get roll mode from parameter layer
		  u8 roll_mode = 0;
		  u8 roll2_mode = 0; // taken if roll1 not assigned
		  if( SEQ_TRG_RollGateGet(track, t->step, instrument) ) { // optional roll gate
		    roll_mode = SEQ_PAR_RollModeGet(track, t->step, instrument, layer_muted);
		    // with less priority (parameter == 0): force roll mode if Roll trigger is set
		    if( nth_trigger == SEQ_PAR_TYPE_NTH_ROLL || (!roll_mode && SEQ_TRG_RollGet(track, t->step, instrument)) )
		      roll_mode = 0x0a; // 2D10
		    // if roll mode != 0: increase number of triggers
		    if( roll_mode ) {
		      triggers = ((roll_mode & 0x30)>>4) + 2;
		    } else {
		      roll2_mode = SEQ_PAR_Roll2ModeGet(track, t->step, instrument, layer_muted);
		      if( roll2_mode )
			triggers = (roll2_mode >> 5) + 2;
		    }
		  }

		  if( triggers > 1 ) {
		    if( roll2_mode ) {
		      // force gatelength depending on roll2 value
		      gatelength = (8 - 2*(roll2_mode >> 5)) * ((roll2_mode&0x1f)+1);

		      // scale length (0..95) over next clock counter to consider the selected clock divider
		      int gatelength = (4 - (roll2_mode >> 5)) * ((roll2_mode&0x1f)+1);

		      u32 half_gatelength = gatelength/2;
		      if( !half_gatelength )
			half_gatelength = 1;
      	      
		      int i;
		      for(i=triggers-1; i>=0; --i)
			SEQ_CORE_ScheduleEvent(track, t, tcc, *p, SEQ_MIDI_OUT_OnOffEvent, bpm_tick + t->bpm_tick_delay + i*gatelength, half_gatelength, 0, robotize_flags);
		    } else {
		      // force gatelength depending on number of triggers
		      if( triggers < 6 ) {
			//       number of triggers:    2   3   4   5
			const u8 gatelength_tab[4] = { 48, 32, 36, 32 };
			// strategy:
			// 2 triggers: played within 1 step at 0 and 48
			// 3 triggers: played within 1 step at 0, 32 and 64
			// 4 triggers: played within 1.5 steps at 0, 36, 72 and 108
			// 5 triggers: played within 1.5 steps at 0, 32, 64, 96 and 128

			// in addition, scale length (0..95) over next clock counter to consider the selected clock divider
			gatelength = (gatelength_tab[triggers-2] * t->step_length) / 96;
		      }

		      u32 half_gatelength = gatelength/2;
		      if( !half_gatelength )
			half_gatelength = 1;
      	      
		      mios32_midi_package_t p_multi = *p;
		      u16 roll_attenuation = 256 - (2 * triggers * (16 - (roll_mode & 0x0f))); // magic formula for nice effects
		      if( roll_mode & 0x40 ) { // upwards
			int i;
			for(i=triggers-1; i>=0; --i) {
			  SEQ_CORE_ScheduleEvent(track, t, tcc, p_multi, SEQ_MIDI_OUT_OnOffEvent, bpm_tick + t->bpm_tick_delay + i*gatelength, half_gatelength ,0, robotize_flags);
			  u16 velocity = roll_attenuation * p_multi.velocity;
			  p_multi.velocity = velocity >> 8;
			}
		      } else { // downwards
			int i;
			for(i=0; i<triggers; ++i) {
			  SEQ_CORE_ScheduleEvent(track, t, tcc, p_multi, SEQ_MIDI_OUT_OnOffEvent, bpm_tick + t->bpm_tick_delay + i*gatelength, half_gatelength, 0, robotize_flags);
			  if( roll_mode ) {
			    u16 velocity = roll_attenuation * p_multi.velocity;
			    p_multi.velocity = velocity >> 8;
			  }
			}
		      }
		    }
		  } else {
		    if( !gatelength )
		      gatelength = 1;
		    else // scale length (0..95) over next clock counter to consider the selected clock divider
		      gatelength = (gatelength * t->step_length) / 96;
		    SEQ_CORE_ScheduleEvent(track, t, tcc, *p, SEQ_MIDI_OUT_OnOffEvent, bpm_tick + t->bpm_tick_delay, gatelength, 0, robotize_flags);
		  }

		  // apply Post-FX
		  if( !no_fx && !robotize_flags.NOFX) {
		    if( ( (tcc->echo_repeats & 0x3f) && ( !(tcc->echo_repeats & 0x40) || robotize_flags.ECHO ) && gatelength ) )
		      SEQ_CORE_Echo(track, instrument, t, tcc, *p, bpm_tick + t->bpm_tick_delay, gatelength, robotize_flags);
		  }
		}
	      }
            }
          }
        }
      }
    }
  }

  // clear "first clock" flag if it was set before
  seq_core_state.FIRST_CLK = 0;

  // if manual trigger function requested stop: stop sequencer at end of reference step
  if( seq_core_state.MANUAL_TRIGGER_STOP_REQ && (bpm_tick % 96) == 95 )
    SEQ_BPM_Stop();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Resets the step position variables of a track
/////////////////////////////////////////////////////////////////////////////
// Called once per track at each musical measure boundary (global ref_step==0).
// Independent of track length, so all tracks share the same robotize clock -
// enables polymetric / polyrhythmic structures where each track's own length
// drifts against a measure-locked robotize variation.
//
// Anchor lookup formula:
//   idx = (start + (rotate + phase) % loop_cycles) % palette_length
// - palette_length: how many anchors are active (1..16)
// - start: which anchor index begins the playing window (0..15)
// - loop_cycles: window size in measures (0=off, 1..16)
// - rotate: phase offset within the window (lets you cycle the slice live)
//
// In wandering mode (loop_cycles == 0): state evolves naturally; snapshots
// capture the live trajectory so freeze can later rewind to it.
// In loop mode: at every measure boundary inside the loop, state is restored
// from bar_anchors[idx] - each measure of the loop is independent, which
// lets individual measures be re-rolled without affecting the others.
static inline u8 robotize_anchor_index(seq_cc_trk_t *tcc, u8 phase)
{
  u8 palette = tcc->robotize_palette_length;
  if( palette == 0 || palette > 16 ) palette = 16; // safety
  u8 loop = tcc->robotize_loop_cycles;
  if( loop == 0 ) loop = 1; // shouldn't be called when not looping, but be defensive
  return (tcc->robotize_loop_start + ((tcc->robotize_loop_rotate + phase) % loop)) % palette;
}

static void SEQ_CORE_RobotizeLoopBarTick(seq_core_trk_t *t, seq_cc_trk_t *tcc)
{
  if( t->robotize_pending_resync ) {
    t->robotize_loop_phase = 0;
    t->robotize_pending_resync = 0;
    if( tcc->robotize_loop_cycles )
      t->robotize_seed_state = tcc->robotize_bar_anchors[robotize_anchor_index(tcc, 0)];
  } else if( tcc->robotize_loop_cycles ) {
    ++t->robotize_loop_phase;
    if( t->robotize_loop_phase >= tcc->robotize_loop_cycles )
      t->robotize_loop_phase = 0;
    t->robotize_seed_state = tcc->robotize_bar_anchors[robotize_anchor_index(tcc, t->robotize_loop_phase)];
  }
  t->robotize_seed_snapshots[t->robotize_measure_ctr & 0x0f] = t->robotize_seed_state;
}


static s32 SEQ_CORE_ResetTrkPos(u8 track, seq_core_trk_t *t, seq_cc_trk_t *tcc)
{
  // synch to measure done
  t->state.SYNC_MEASURE = 0;

  // don't increment on first clock event
  t->state.FIRST_CLK = 1;

  // clear delay
  t->bpm_tick_delay = 0;

  // reset step progression counters
  t->step_replay_ctr = 0;
  t->step_fwd_ctr = 0;
  t->step_interval_ctr = 0;
  t->step_repeat_ctr = 0;
  t->step_skip_ctr = 0;

  // and MIDI Fx channel counter
  t->fx_midi_ctr = 0xff; // start with original channel

  // next part depends on forward/backward direction
  if( tcc->dir_mode == SEQ_CORE_TRKDIR_Backward ) {
    // only for Backward mode
    t->state.BACKWARD = 1;
    t->step = tcc->length;
  } else {
    // for Forward/PingPong/Pendulum/Random/...
    t->state.BACKWARD = 0;
    t->step = 0;
  }

  // save position (for repeat function)
  t->step_saved = t->step;

  t->arp_pos = 0;

  // reset LFO
  SEQ_LFO_ResetTrk(track);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Resets the step position variables of all tracks
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_ResetTrkPosAll(void)
{
  seq_core_trk_t *t = &seq_core_trk[0];
  seq_cc_trk_t *tcc = &seq_cc_trk[0];
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t, ++tcc)
    SEQ_CORE_ResetTrkPos(track, t, tcc);

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Sets the track position for the given track (optionally scaled over 7bit)
// the manual flag will set the step immediately, and play it
/////////////////////////////////////////////////////////////////////////////
extern s32 SEQ_CORE_SetTrkPos(u8 track, u8 value, u8 scale_value)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1; // invalid track

  seq_core_trk_t *t = &seq_core_trk[track];
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  // scale CC value over track length
  // selectable steps: 128 maximum
  int step = value;
  if( scale_value ) {
    int length = (int)tcc->length + 1;
    if( length > 128 )
      length = 128;
    step = ((step * length) / 128);
  }

  if( !SEQ_BPM_IsRunning() ) {
    // reset track position
    SEQ_CORE_ResetTrkPos(track, t, tcc);

    // change step
    t->step = step;
    t->step_saved = t->step;
  } else {
    // request next step
    t->manual_step = step;
    t->state.MANUAL_STEP_REQ = 1;

    if( tcc->clkdiv.MANUAL )
      t->state.FIRST_CLK = 1; // change to step immediately (not synchronized)
  }

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Determine next step depending on direction mode
/////////////////////////////////////////////////////////////////////////////
static s32 SEQ_CORE_NextStep(seq_core_trk_t *t, seq_cc_trk_t *tcc, u8 no_progression, u8 reverse)
{
  int i;
  u8 save_step = 0;
  u8 new_step = 1;

  // handle progression parameters if position shouldn't be reset
  if( !no_progression && !t->state.POS_RESET ) {
    if( ++t->step_fwd_ctr > tcc->steps_forward ) {
      t->step_fwd_ctr = 0;
      if( tcc->steps_jump_back ) {
	for(i=0; i<tcc->steps_jump_back; ++i)
	  SEQ_CORE_NextStep(t, tcc, 1, 1); // 1=no progression, 1=reverse
      }
      if( ++t->step_replay_ctr > tcc->steps_replay ) {
	t->step_replay_ctr = 0;
	save_step = 1; // request to save the step in t->step_saved at the end of this routine
      } else {
	t->step = t->step_saved;
	new_step = 0; // don't calculate new step
      }
    }

    if( ++t->step_interval_ctr > tcc->steps_rs_interval ) {
      t->step_interval_ctr = 0;
      t->step_repeat_ctr = tcc->steps_repeat;
      t->step_skip_ctr = tcc->steps_skip;
    }

    if( t->step_repeat_ctr ) {
      --t->step_repeat_ctr;
      new_step = 0; // repeat step until counter reached zero
    } else {
      while( t->step_skip_ctr ) {
	SEQ_CORE_NextStep(t, tcc, 1, 0); // 1=no progression, 0=not reverse
	--t->step_skip_ctr;
      }
    }
  }

  if( new_step ) {
    // special cases:
    switch( tcc->dir_mode ) {
      case SEQ_CORE_TRKDIR_Forward:
	t->state.BACKWARD = 0; // force backward flag
	break;

      case SEQ_CORE_TRKDIR_Backward:
	t->state.BACKWARD = 1; // force backward flag
	break;

      case SEQ_CORE_TRKDIR_PingPong:
      case SEQ_CORE_TRKDIR_Pendulum:
	// nothing else to do...
	break;

      // Per-track-RNG keystone (2026-06-19): the random traversal modes draw
      // from the track's OWN xorshift stream (t->random_traverse_state) instead
      // of the global RNG, so a span's step trajectory is deterministic +
      // seekable (re-simulatable from a captured seed) and decoupled from every
      // other track's generative draws. This is a behaviour-preserving swap:
      // the same decision masks read raw GenXorshift bits in the same order, and
      // GenRangeXorshift keeps Gen_Range's loop==length zero-advance short-circuit
      // so the draw count is unchanged. (NB the Random_D_S inner `rnd < 0x40`
      // dir-flip test compares the FULL word, not `rnd & 0xff` — a pre-existing
      // quirk that makes the dir-flip branch rarely fire; preserved verbatim, the
      // commented 50/25/25 split is the original's intent, not its real behaviour.)
      case SEQ_CORE_TRKDIR_Random_Dir:
	// set forward/backward direction with 1:1 probability
	t->state.BACKWARD = SEQ_RANDOM_GenXorshift(&t->random_traverse_state) & 1;
        break;

      case SEQ_CORE_TRKDIR_Random_Step:
	t->step = SEQ_RANDOM_GenRangeXorshift(&t->random_traverse_state, tcc->loop, tcc->length);
	new_step = 0; // no new step calculation required anymore
        break;

      case SEQ_CORE_TRKDIR_Random_D_S:
	{
	  // we continue with a probability of 50%
	  // we change the direction with a probability of 25%
	  // we jump to a new step with a probability of 25%
	  u32 rnd;
	  if( ((rnd=SEQ_RANDOM_GenXorshift(&t->random_traverse_state)) & 0xff) < 0x80 ) {
	    if( rnd < 0x40 ) {
	      // set forward/backward direction with 1:1 probability
	      t->state.BACKWARD = SEQ_RANDOM_GenXorshift(&t->random_traverse_state) & 1;
	    } else {
	      t->step = SEQ_RANDOM_GenRangeXorshift(&t->random_traverse_state, tcc->loop, tcc->length);
	      new_step = 0; // no new step calculation required anymore
	    }
	  }
	}
	break;
    }
  }

  if( new_step ) { // note: new_step will be cleared in SEQ_CORE_TRKDIR_Random_Step mode
    // branch depending on forward/backward mode, take reverse flag into account
    if( t->state.BACKWARD ^ reverse ) {
      // jump to last step if first loop step has been reached or a position reset has been requested
      // in pendulum mode: switch to forward direction
      if( t->state.POS_RESET || t->step <= tcc->loop ) {
	++t->bar;

	if( tcc->dir_mode == SEQ_CORE_TRKDIR_Pendulum ) {
	  t->state.BACKWARD = 0;
	} else {
	  t->step = tcc->length;
	}
	// reset arp position as well
	t->arp_pos = 0;
      } else {
	// no reset required; decrement step
	--t->step;

	// in pingpong mode: turn direction if loop step has been reached after this decrement
	if( t->step <= tcc->loop && tcc->dir_mode == SEQ_CORE_TRKDIR_PingPong )
	  t->state.BACKWARD = 0;
      }
    } else {
      // jump to first (loop) step if last step has been reached or a position reset has been requested
      // in pendulum mode: switch to backward direction
      if( t->state.POS_RESET || t->step >= tcc->length ) {
	++t->bar;

	if( tcc->dir_mode == SEQ_CORE_TRKDIR_Pendulum ) {
	  t->state.BACKWARD = 1;
	} else {
	  t->step = tcc->loop;
	}
	// reset arp position as well
	t->arp_pos = 0;
      } else {
	// no reset required; increment step
	++t->step;

	// in pingpong mode: turn direction if last step has been reached after this increment
	if( t->step >= tcc->length && tcc->dir_mode == SEQ_CORE_TRKDIR_PingPong )
	  t->state.BACKWARD = 1;
      }
    }
  }

  if( !reverse ) {
    // requested by progression handler
    if( save_step )
      t->step_saved = t->step;

    t->state.POS_RESET = 0;
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Transposes if midi_package contains a Note Event
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Transpose(u8 track, u8 instrument, seq_core_trk_t *t, seq_cc_trk_t *tcc, mios32_midi_package_t *p)
{
  u8 is_cc = p->type != NoteOn && p->type != NoteOff; // CC, Pitchbender, Programchange

  if( is_cc && tcc->event_mode != SEQ_EVENT_MODE_CC ) // only transpose CC/Pitchbender/Program Change in CC mode
    return -1;

  int note = is_cc ? p->value : p->note;

  if( !is_cc && p->note == 0 ) // before transpose: ensure that velocity is 0 in case no note should be played (original note is 0)
    p->velocity = 0;           // this allows to transpose to note 0 (c-2) and get it played, and ensures that disabled notes are not played

  int inc_oct = tcc->transpose_oct;
  if( inc_oct >= 8 )
    inc_oct -= 16;

  int inc_semi = tcc->transpose_semi;
  if( inc_semi >= 8 )
    inc_semi -= 16;

  // in transpose or arp playmode we allow to transpose notes and CCs
  if( tcc->playmode == SEQ_CORE_TRKMODE_Transpose ||
      (!is_cc && seq_core_global_transpose_enabled) ) {
    int tr_note = SEQ_MIDI_IN_TransposerNoteGet(tcc->busasg.bus, tcc->trkmode_flags.HOLD, tcc->trkmode_flags.FIRST_NOTE);

    if( tr_note < 0 ) {
      p->velocity = 0; // disable note and exit
      return -1; // note has been disabled
    }

    inc_semi += tr_note - 0x3c; // C-3 is the base note
  } else if( tcc->playmode == SEQ_CORE_TRKMODE_Arpeggiator ) {
    int key_num = (note >> 2) & 0x3;
    int arp_oct = (note >> 4) & 0x7;

    if( arp_oct < 2 ) { // Multi Arp Event
      inc_oct += ((note >> 2) & 7) - 4;
      key_num = t->arp_pos;
    } else {
      inc_oct += arp_oct - 4;
      t->arp_pos = 0; // ensure that no multi arp event is played anymore
    }

    int arp_note = SEQ_MIDI_IN_ArpNoteGet(tcc->busasg.bus, tcc->trkmode_flags.HOLD, !tcc->trkmode_flags.UNSORTED, key_num);

    if( arp_note & 0x80 ) {
      t->arp_pos = 0;
    } else {
      if( arp_oct < 2 ) { // Multi Arp Event
	// play next key, step will be incremented once t->arp_pos reaches 0 again
	if( ++t->arp_pos >= 4 )
	  t->arp_pos = 0;
      }
    }

    note = arp_note & 0x7f;

    if( !note ) { // disable note and exit
      p->velocity = 0;
      return -1; // note has been disabled
    }
  } else if( tcc->playmode == SEQ_CORE_TRKMODE_ChordMask ) {
    // §6 chord-context playmode. Phase C: the snap logic migrated to the
    // chord_mask processor (see chord_mask_render). The TRKMODE picker +
    // CHORDMASK_STRENGTH CC act as a UX shortcut that populates a processor
    // slot via the SEQ_CC_Set bridge; nothing to do here.
  } else {
    // neither transpose nor arpeggiator mode: transpose based on root note if specified in parameter layer
    // TK: I think that this was a wrong assumption - we don't want to transpose, but we want to define the root note via SEQ_CORE_GetScaleAndRoot
#if 0
    if( !is_cc && tcc->link_par_layer_root >= 0 ) {
      u8 root = SEQ_PAR_Get(track, t->step, tcc->link_par_layer_root, instrument);
      if( !root ) {
	root = seq_core_global_scale_root_selection;
      }

      if( root ) {
	inc_semi += root - 1;
      } else {
	int tr_note = SEQ_MIDI_IN_TransposerNoteGet(tcc->busasg.bus, tcc->trkmode_flags.HOLD, tcc->trkmode_flags.FIRST_NOTE);

	if( tr_note < 0 ) {
	  p->velocity = 0; // disable note and exit
	  return -1; // note has been disabled
	}

	inc_semi += tr_note - 0x3c; // C-3 is the base note
      }
    }
#endif
  }

  // apply transpose octave/semitones parameter
  if( inc_oct ) {
    note += 12 * inc_oct;
  }

  if( inc_semi ) {
    note += inc_semi;
  }

  // ensure that note is in the 0..127 range
  note = SEQ_CORE_TrimNote(note, 0, 127);

  if( is_cc ) // if CC, Pitchbender, ProgramChange
    p->value = note;
  else
    p->note = note;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Returns the selected scale and root note selection depending on
// global/group specific settings or local track if the appr. parameter 
// layers are available
// scale and root note are for interest while playing the sequence -> SEQ_CORE
// scale and root selection are for interest when editing the settings -> SEQ_UI_OPT
// Both modules are calling this function to ensure consistency
// 
// if *tcc is NULL, the function will always return the global settings
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_FTS_GetScaleAndRoot(u8 track, u8 step, u8 instrument, seq_cc_trk_t *tcc, u8 *scale, u8 *root_selection, u8 *root)
{
  if( tcc && tcc->link_par_layer_scale >= 0 ) {
    *scale = SEQ_PAR_Get(track, step, tcc->link_par_layer_scale, instrument);
    if( *scale ) {
      *scale -= 1;
    } else {
      *scale = seq_core_global_scale;
    }
  } else {
    *scale = seq_core_global_scale;
  }

  *root_selection = seq_core_global_scale_root_selection;
  if( tcc && tcc->link_par_layer_root >= 0 ) {
    *root = SEQ_PAR_Get(track, step, tcc->link_par_layer_root, instrument) % 13;
    if( *root ) {
      *root -= 1;
    } else {
      *root = (*root_selection == 0) ? seq_core_keyb_scale_root : (*root_selection-1);
    }
  } else {
    *root = (*root_selection == 0) ? seq_core_keyb_scale_root : (*root_selection-1);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Limit Fx
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Limit(seq_core_trk_t *t, seq_cc_trk_t *tcc, seq_layer_evnt_t *e)
{
  u8 lower = tcc->limit_lower;
  u8 upper = tcc->limit_upper;

  // check if any limit defined
  if( !lower && !upper )
    return 0; // no limit

  // exit if no note event
  mios32_midi_package_t *p = &e->midi_package;
  if( p->type != NoteOn )
    return 0; // no Note

  // if not set: allow full range
  if( !upper )
    upper = 127;

  // swap if lower value is greater than upper
  if( lower > upper ) {
    u8 tmp = upper;
    upper=lower;
    lower=tmp;
  }

  // apply limit
  p->note = SEQ_CORE_TrimNote(p->note, lower, upper);

  return 0; // no error
}



/////////////////////////////////////////////////////////////////////////////
// Name of Delay mode (we should outsource the echo function to seq_echo.c later)
/////////////////////////////////////////////////////////////////////////////
// Note: newer gcc versions don't allow to return a "const" parameter, therefore
// this array is declared outside the SEQ_CORE_Echo_GetDelayModeName() function

#define NUM_DELAY_VALUES 23
static const char delay_str[NUM_DELAY_VALUES+1][5] = {
    " 64T",
    " 64 ",
    " 32T",
    " 32 ",
    " 16T",
    " 16 ",
    "  8T",
    "  8 ",
    "  4T",
    "  4 ",
    "  2T",
    "  2 ",
    "  1T",
    "  1 ",
    "Rnd1",
    "Rnd2",
    " 64d", // new with Beta30
    " 32d", // new with Beta30
    " 16d", // new with Beta30
    "  8d", // new with Beta30
    "  4d", // new with Beta30
    "  2d", // new with Beta30
    "  0 ", // new with Beta42
    "????",
  };

const char *SEQ_CORE_Echo_GetDelayModeName(u8 delay_mode)
{
  if( delay_mode < NUM_DELAY_VALUES )
    return delay_str[delay_mode];

  return delay_str[NUM_DELAY_VALUES];
}


/////////////////////////////////////////////////////////////////////////////
// Maps delay values from old to new format
// Used to keep pattern binaries compatible to enhanced delay entries
/////////////////////////////////////////////////////////////////////////////
static const u8 delay_value_map[NUM_DELAY_VALUES] = {
  22, //"  0 ",
  0,  //" 64T",
  1,  //" 64 ",
  2,  //" 32T",
  16, //" 64d",
  3,  //" 32 ",
  4,  //" 16T",
  17, //" 32d",
  5,  //" 16 ",
  6,  //"  8T",
  18, //" 16d",
  7,  //"  8 ",
  8,  //"  4T",
  19, //"  8d",
  9,  //"  4 ",
  10, //"  2T",
  20, //"  4d",
  11, //"  2 ",
  12, //"  1T",
  21, //"  2d",
  13, //"  1 ",
  14, //"Rnd1",
  15, //"Rnd2",
};

u8 SEQ_CORE_Echo_MapUserToInternal(u8 user_value)
{
  if( user_value < NUM_DELAY_VALUES )
    return delay_value_map[user_value];

  return 0;
}

u8 SEQ_CORE_Echo_MapInternalToUser(u8 internal_value)
{
  int i;
  for(i=0; i<NUM_DELAY_VALUES; ++i)
    if( delay_value_map[i] == internal_value )
      return i;

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Echo Fx
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Echo(u8 track, u8 instrument, seq_core_trk_t *t, seq_cc_trk_t *tcc, mios32_midi_package_t p, u32 bpm_tick, u32 gatelength, seq_robotize_flags_t robotize_flags)
{
  // thanks to MIDI queuing mechanism, this is a no-brainer :)

  // 64T, 64, 32T, 32, 16T, 16, ... 1, Rnd1 and Rnd2, 64d..2d (new), 0 (supernew)
  s32 fb_ticks;
  s32 echo_delay = tcc->echo_delay;
  if( echo_delay >= 22 ) // new zero delay
    fb_ticks = 0;
  else if ( echo_delay >= 16 ) // new dotted delays
    fb_ticks = 36 * (1 << (echo_delay-16));
  else {
    if( echo_delay >= 14 ) // Rnd1 and Rnd2
      echo_delay = SEQ_RANDOM_Gen_Range(3, 7); // between 32 and 8
    fb_ticks = ((tcc->echo_delay & 1) ? 24 : 16) * (1 << (echo_delay>>1));
  }

  s32 fb_note = p.note;
  s32 fb_note_base = fb_note; // for random function

  // the initial velocity value allows to start with a low velocity,
  // and to increase it with each step via FB velocity value
  s32 fb_velocity = p.velocity;
  if( tcc->echo_velocity != 20 ) { // 20 == 100% -> no change
    fb_velocity = (fb_velocity * 5*tcc->echo_velocity) / 100;
    if( fb_velocity > 127 )
      fb_velocity = 127;
    p.velocity = (u8)fb_velocity;
  }

  seq_midi_out_event_type_t event_type = SEQ_MIDI_OUT_OnOffEvent;
  if( (p.type == CC || p.type == PitchBend || p.type == ProgramChange) && !gatelength )
    event_type = SEQ_MIDI_OUT_CCEvent;

  // for the case that force-to-scale is activated
  u8 scale, root_selection, root;
  SEQ_CORE_FTS_GetScaleAndRoot(track, t->step, instrument, tcc, &scale, &root_selection, &root);

  u32 echo_offset = fb_ticks;
  u8 echo_repeats = tcc->echo_repeats;

  if( robotize_flags.ECHO ) {
	// remove 0x40 flag indicating that echo is active (it's reversed, so 1 indicates echo is set to off)
	// have to strip this flag out or the MSB flag makes a huge # of echo_repeats.
	echo_repeats = echo_repeats & 0x0F;
  }
	
  if( echo_repeats & 0x40 && !robotize_flags.ECHO) // disable flag
    echo_repeats = 0;
    
    
  int i;
  for(i=0; i<echo_repeats; ++i) {
    if( i ) { // no feedback of velocity or echo ticks on first step
      if( tcc->echo_fb_velocity != 20 ) { // 20 == 100% -> no change
	fb_velocity = (fb_velocity * 5*tcc->echo_fb_velocity) / 100;
	if( fb_velocity > 127 )
	  fb_velocity = 127;
	p.velocity = (u8)fb_velocity;
      }

      if( tcc->echo_fb_ticks != 20 ) { // 20 == 100% -> no change
	fb_ticks = (fb_ticks * 5*tcc->echo_fb_ticks) / 100;
      }
      echo_offset += fb_ticks;
    }

    if( tcc->echo_fb_note != 24 ) { // 24 == 0 -> no change
      if( tcc->echo_fb_note == 49 ) // random
	fb_note = fb_note_base + ((s32)SEQ_RANDOM_Gen_Range(0, 48) - 24);
      else
	fb_note = fb_note + ((s32)tcc->echo_fb_note-24);

      // ensure that note is in the 0..127 range
      fb_note = SEQ_CORE_TrimNote(fb_note, 0, 127);

      p.note = (u8)fb_note;
    }

    if( gatelength && tcc->echo_fb_gatelength != 20 ) { // 20 == 100% -> no change
      gatelength = (gatelength * 5*tcc->echo_fb_gatelength) / 100;
      if( !gatelength )
	gatelength = 1;
    }

    // force to scale
    if( tcc->trkmode_flags.FORCE_SCALE ) {
      SEQ_SCALE_Note(&p, scale, root);
    }

    SEQ_CORE_ScheduleEvent(track, t, tcc, p, event_type, bpm_tick + echo_offset, gatelength, 1, robotize_flags);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Manually triggers a step of all selected tracks
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_ManualTrigger(u8 step)
{
  MIOS32_IRQ_Disable();

  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    if( SEQ_UI_IsSelectedTrack(track) ) {
      SEQ_CORE_SetTrkPos(track, step, 0);
    }
  }

  if( !SEQ_BPM_IsRunning() ) {
    // start sequencer if not running, but only for one step
    SEQ_BPM_Cont();
    seq_core_state.MANUAL_TRIGGER_STOP_REQ = 1;
  }

  MIOS32_IRQ_Enable();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Manually requests synch to measure for given tracks
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_ManualSynchToMeasure(u16 tracks)
{
  MIOS32_IRQ_Disable();

  u8 track;
  seq_core_trk_t *t = &seq_core_trk[0];
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t)
    if( tracks & (1 << track) )
      t->state.SYNC_MEASURE = 1;

  MIOS32_IRQ_Enable();

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Used by the transposer to request the next step in "step trigger" mode
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_StepTriggerReq(u8 bus)
{
  u8 track;
  seq_core_trk_t *t = &seq_core_trk[0];
  seq_cc_trk_t *tcc = &seq_cc_trk[0];

  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t, ++tcc) {
    if( tcc->busasg.bus == bus ) {
      t->state.TRIGGER_NEXT_STEP_REQ = 1;
    }
  }
  
  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This function is called by the "Live" function on incoming MIDI events,
// currently we use it to control the temporary layer mutes.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_NotifyIncomingMIDIEvent(u8 track, mios32_midi_package_t p)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1; // invalid track

  if( !seq_core_options.LIVE_LAYER_MUTE_STEPS )
    return 0; // disabled

  seq_core_trk_t *t = &seq_core_trk[track];
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  if( tcc->event_mode == SEQ_EVENT_MODE_Drum )
    return 0; // (currently) not relevant in drum mode

  switch( p.event ) {
  //case NoteOff:
  case NoteOn: {
    if( p.velocity == 0 ) // ignore Note Offs
      break;

    // temporary mute layers which are assigned to notes or chords
    u8 *layer_type_ptr = (u8 *)&tcc->lay_const[0*16];
    int par_layer;
    int num_p_layers = SEQ_PAR_NumLayersGet(track);
    u16 mask = 1;
    for(par_layer=0; par_layer<num_p_layers; ++par_layer, ++layer_type_ptr, mask <<= 1) {
      if( *layer_type_ptr == SEQ_PAR_Type_Note || *layer_type_ptr == SEQ_PAR_Type_Chord1 || *layer_type_ptr == SEQ_PAR_Type_Chord2 || *layer_type_ptr == SEQ_PAR_Type_Chord3 ) {
	// hm... should we also play a note off for active notes?
	// and should we mute the sequencer notes as long as no Note Off has been played?
	// problem: we would have to track all actively played MIDI notes, this consumes a lot of memory

	portENTER_CRITICAL();
	if( seq_core_options.LIVE_LAYER_MUTE_STEPS == 1 ) {
	  t->layer_muted |= mask;      // mute layer immediately
	} else {
	  t->layer_muted_from_midi |= mask;      // mute layer immediately
	  t->layer_muted_from_midi_next |= mask; // and for the next step
	}
	portEXIT_CRITICAL();
      }
    }
  } break;

  //case PolyPressure:
  case CC:
  case ProgramChange:
  //case Aftertouch:
  case PitchBend: {
    // temporary mute layers which are assigned to the corresponding event
    u8 *layer_type_ptr = (u8 *)&tcc->lay_const[0*16];
    int par_layer;
    int num_p_layers = SEQ_PAR_NumLayersGet(track);
    u16 mask = 1;
    for(par_layer=0; par_layer<num_p_layers; ++par_layer, ++layer_type_ptr, mask <<= 1) {
      u8 apply_mask = 0;
      switch( *layer_type_ptr ) {
      case SEQ_PAR_Type_CC: {
	if( p.event == CC && p.cc_number == tcc->lay_const[1*16 + par_layer] ) {
	  apply_mask = 1;
	}
      } break;

      case SEQ_PAR_Type_PitchBend: {
	if( p.event == PitchBend ) {
	  apply_mask = 1;
	}
      } break;

      case SEQ_PAR_Type_ProgramChange: {
	if( p.event == ProgramChange ) {
	  apply_mask = 1;
	}
      } break;
      }

      if( apply_mask ) {
	portENTER_CRITICAL();
	if( seq_core_options.LIVE_LAYER_MUTE_STEPS == 1 ) {
	  t->layer_muted |= mask;      // mute layer immediately
	} else {
	  t->layer_muted_from_midi |= mask;      // mute layer immediately
	  t->layer_muted_from_midi_next |= mask; // and for the next step
	}
	portEXIT_CRITICAL();
      }
    }

    // check also LFO CC (note: only handled as temporary change)
    if( p.event == CC && p.cc_number == tcc->lfo_cc ) {
      portENTER_CRITICAL();
      t->lfo_cc_muted_from_midi = 1;
      t->lfo_cc_muted_from_midi_next = 1;
      portEXIT_CRITICAL();
    }

  } break;
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This function requests to pre-generate sequencer ticks for a given time
// It returns -1 if the previously requested delay hasn't passed yet
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_AddForwardDelay(u16 delay_ms)
{
  if( bpm_tick_prefetch_req )
    return -1; // ongoing request

  // calculate how many BPM ticks have to be forwarded
  u32 delay_ticks = SEQ_BPM_TicksFor_mS(delay_ms);
  bpm_tick_prefetch_req = SEQ_BPM_TickGet() + delay_ticks;

  return delay_ticks; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Effective forward-delay margin (ms) for a switch.  Tracks the MEASURED switch
// I/O time (SEQ_PATTERN_Handler / phrase recall store it into
// seq_core_pattern_switch_measured_ms) plus headroom, never below a small floor;
// falls back to the configured seq_core_pattern_switch_margin_ms until the first
// real measurement exists.  This is what shrinks the conservative fixed 100 mS to
// the actual SD cost so the forward-delay window (and the switch feel) is tight.
/////////////////////////////////////////////////////////////////////////////
u16 SEQ_CORE_SwitchMarginMs(void)
{
  u16 m = seq_core_pattern_switch_measured_ms;
  if( m ) {
    m += 20; // headroom over the measured worst case
    if( m < 30 )
      m = 30;
  } else {
    m = seq_core_pattern_switch_margin_ms; // fallback until the first measurement
  }
  if( m > 250 )
    m = 250; // sane cap (forward-delay window must stay well under a measure)
  return m;
}


/////////////////////////////////////////////////////////////////////////////
// Global switch-quantize grid in 16th-note steps (0 = Instant).  Maps the
// SWITCH_QUANTIZE_GRID ladder index to a grid, then FLOOR-CLAMPS it up so the
// grid interval (grid*96 ticks) can never be shorter than the switch I/O needs
// at the current tempo — otherwise a switch couldn't finish before its own
// boundary.  The caller fires a deferred switch when the next 16th is a multiple.
/////////////////////////////////////////////////////////////////////////////
u16 SEQ_CORE_SwitchQuantize16ths(void)
{
  // ladder: 0=Instant 1=1/16 2=1/8 3=1/4beat 4=1/2bar 5=1bar 6=2bar 7=4bar 8=8bar
  static const u16 grid_tab[9] = { 0, 1, 2, 4, 8, 16, 32, 64, 128 };
  u8 q = seq_core_options.SWITCH_QUANTIZE_GRID;
  if( q > 8 )
    q = 8;
  if( q == 0 )
    return 0; // Instant (no quantize; handled by the immediate-change path)

  u32 margin_ticks = SEQ_BPM_TicksFor_mS(SEQ_CORE_SwitchMarginMs());
  while( q < 8 && (u32)grid_tab[q] * 96 < margin_ticks )
    ++q; // bump up until the grid interval covers the I/O margin
  return grid_tab[q];
}


/////////////////////////////////////////////////////////////////////////////
// This function updates the BPM rate in a given sweep time
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_BPM_Update(float bpm, float sweep_ramp)
{
  if( sweep_ramp <= 0.0 ) {
    seq_core_bpm_target = bpm;
    SEQ_BPM_Set(seq_core_bpm_target);
    SEQ_MIDI_PORT_ClkDelayUpdateAll();
    seq_core_bpm_sweep_inc = 0.0;
  } else {
    seq_core_bpm_target = bpm;
    seq_core_bpm_sweep_inc = (seq_core_bpm_target - SEQ_BPM_Get()) / (10.0 * sweep_ramp);
  }

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// This function should be called each mS to update the BPM
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_BPM_SweepHandler(void)
{
  static u8 prescaler = 0;

  // next step each 100 mS
  if( ++prescaler < 100 )
    return 0;
  prescaler = 0;

  if( seq_core_bpm_sweep_inc != 0.0 ) {
    float current_bpm = SEQ_BPM_Get();
    float tolerance = 0.1;

    if( (seq_core_bpm_sweep_inc > 0.0 && current_bpm >= (seq_core_bpm_target-tolerance)) ||
	(seq_core_bpm_sweep_inc < 0.0 && current_bpm <= (seq_core_bpm_target+tolerance)) ) {
      seq_core_bpm_sweep_inc = 0.0; // final value reached
      SEQ_BPM_Set(seq_core_bpm_target);
      SEQ_MIDI_PORT_ClkDelayUpdateAll();
    } else {
      SEQ_BPM_Set(current_bpm + seq_core_bpm_sweep_inc);
      SEQ_MIDI_PORT_ClkDelayUpdateAll();
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Scrub function called from UI when SCRUB button pressed and Datawheel
// is moved
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_Scrub(s32 incrementer)
{
  // simple but useful: increment/decrement the step of each track
  // (in MBSEQ V3 we only generated some additional clocks, which had
  // the disadvantage, that sequences couldn't be scrubbed back)

  // this simplified method currently has following disadvantages:
  // - clock dividers not taken into account (difficult, needs some code restructuring in SEQ_CORE_Tick())
  // - ...and?
  // advantage:
  // - sequencer stays in sync with outgoing/incoming MIDI clock!
  // - reverse scrubbing for some interesting effects while played live (MB-808 has a similar function: nudge)

  u8 track;
  seq_core_trk_t *t = &seq_core_trk[0];
  seq_cc_trk_t *tcc = &seq_cc_trk[0];
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t, ++tcc) {
    // SCRUB is a manual nudge, NOT part of the deterministic playback timeline.
    // For a random-traversal track, NextStep draws from random_traverse_state;
    // save/restore it around the scrub so the manual gesture cannot advance (and
    // thus desync) the re-simulatable playback stream (per-track-RNG keystone).
    u32 saved_traverse = t->random_traverse_state;
    SEQ_CORE_NextStep(t, tcc, 0, incrementer >= 0 ? 0 : 1);
    t->random_traverse_state = saved_traverse;
  }

#if 0
  // disabled so that we stay in Sync with MIDI clock!
  // increment/decrement reference step
  if( incrementer >= 0 ) {
    if( ++seq_core_state.ref_step > seq_core_steps_per_measure )
      seq_core_state.ref_step = 0;
  } else {
    if( seq_core_state.ref_step )
      --seq_core_state.ref_step;
    else
      seq_core_state.ref_step = seq_core_steps_per_measure;
  }
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This function has to be called from the UI whenever notes have been
// updated to ensure that an ongoing sustained note is cancled if
// there is no step played by the track anymore.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CORE_CancelSustainedNotes(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1; // invalid track

  seq_core_trk_t *t = &seq_core_trk[track];
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  if( t->state.SUSTAINED && tcc->event_mode != SEQ_EVENT_MODE_Drum ) {
    u8 gate_trg_assignment = tcc->trg_assignments.gate;

    if( gate_trg_assignment ) {
      u8 any_gate_set = 0;
      u8 trg_instrument = 0;
      int trk_len = (int)tcc->length + 1;
      int i;

      for(i=0; i<trk_len; ++i) {
	if( SEQ_TRG_Get(track, i, gate_trg_assignment-1, trg_instrument) ) {
	  any_gate_set = 1;
	  break;
	}
      }

      if( !any_gate_set ) {
	t->state.CANCEL_SUSTAIN_REQ = 1;
      }
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This function ensures, that a (transposed) note is within
// the <lower>..<upper> range.
//
// If the note is outside the range, it will be "trimmed" in the semitone
// range, and the octave will be kept.
/////////////////////////////////////////////////////////////////////////////
u8 SEQ_CORE_TrimNote(s32 note, u8 lower, u8 upper)
{
  // negative note (e.g. after transpose?)
  // shift it to the positive range
  if( note < 0 )
    note = 11 - ((-note - 1) % 12);

  // check for lower boundary
  if( note < (s32)lower ) {
    note = 12*(lower/12) + (note % 12);
  }

  // check for upper boundary
  if( note > (s32)upper ) {
    note = 12*(upper/12) + (note % 12);

    // if note still > upper value (e.g. upper is set to >= 120)
    // an if (instead of while) should work in all cases, because note will be (12*int(127/12)) + 11 = 131 in worst case!
    if( note > upper )
      note -= 12;
  }

  return note;
}
