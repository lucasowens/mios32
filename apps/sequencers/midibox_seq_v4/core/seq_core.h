// $Id$
/*
 * Header file for core routines
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_CORE_H
#define _SEQ_CORE_H

/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////

#define SEQ_CORE_NUM_GROUPS            4
#define SEQ_CORE_NUM_TRACKS_PER_GROUP  4
#define SEQ_CORE_NUM_TRACKS            (SEQ_CORE_NUM_TRACKS_PER_GROUP*SEQ_CORE_NUM_GROUPS)

#define SEQ_CORE_NUM_BPM_PRESETS       16

#define SEQ_CORE_NUM_PROCESSOR_SLOTS   4


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////

typedef union {
  u32 ALL;
  struct {
    u32 INIT_CC:8;
    u32 SYNCHED_PATTERN_CHANGE:1;
    u32 PATTERN_CHANGE_DONT_RESET_LATCHED_PC:1;
    u32 PASTE_CLR_ALL:1;
    u32 RATOPC:1;
    u32 SYNCHED_MUTE:1;
    u32 SYNCHED_UNMUTE:1;
    u32 UNMUTE_ON_PATTERN_CHANGE:1;
    u32 PATTERN_MIXER_MAP_COUPLING:1;
    u32 MIXER_LIVE_SEND:1;
    u32 INIT_WITH_TRIGGERS:1;
    u32 LIVE_LAYER_MUTE_STEPS:3; // 0=off, 1=permanent, 2..4 steps
  };
} seq_core_options_t;

typedef union {
  u32 ALL;
  struct {
    u16 ref_step; // u16 instead of u8 to cover overrun on 256 steps per measure
    u16 ref_step_pattern; // independent reference step for pattern changes
    u16 ref_step_song; // reference step can be different in song mode if a guide track is used
    u16 reset_trkpos_req; // resets the track with the next step

    u16 FIRST_CLK:1;
    u16 FORCE_REF_STEP_RESET:1;
    u16 METRONOME:1;
    u16 MANUAL_TRIGGER_STOP_REQ:1;
    u16 MANUAL_TRIGGER_STEP_REQ:1;
    u16 EXT_RESTART_REQ:1;
    u16 LOOP:1;
    u16 FOLLOW:1;
  };
} seq_core_state_t;


typedef union {
  u16 ALL;
  struct {
    u16 DISABLED:1;    // set if no pattern is selected to avoid editing of trigger/layer values
    u16 POS_RESET:1;   // set by MIDI handler if position of ARP/Transpose track should be reset
    u16 BACKWARD:1;    // if set, the track will be played in backward direction
    u16 FIRST_CLK:1;   // don't increment on the first clock event
    u16 REC_DONT_OVERWRITE_NEXT_STEP:1; // if a recorded step has been shifted forward
    u16 SYNC_MEASURE:1; // temporary request for synch to measure (used during pattern switching)
    u16 SUSTAINED:1;    // sustained note
    u16 ROBOSUSTAINED:1;  // events are temporarily sustained by the robotizer
    u16 STRETCHED_GL:1; // stretched gatelength
    u16 MANUAL_STEP_REQ:1; // manual_step should be copied to step
    u16 CANCEL_SUSTAIN_REQ:1; // cancel ongoing sustain
    u16 TRIGGER_NEXT_STEP_REQ:1; // to continue with next step in STEP_TRG mode; flag is set by transposer
  };
} seq_core_trk_state_t;


typedef struct seq_core_trk_t {
  seq_core_trk_state_t state;            // various status flags (see structure definition above)
  char                 name[81];         // the track name (80 chars + zero terminator)
  u8                   step;             // current track position
  u8                   bar;              // bar counter
  u16                  step_length;      // length of the current step
  u32                  timestamp_next_step; // timestamp at which the next step will be played
  u32                  timestamp_next_step_ref; // timestamp of next step w/o groove delay
  u32                  glide_notes[4];   // 128 bit to store notes in glide state
  u16                  bpm_tick_delay;   // delay of current step
  u8                   step_replay_ctr;  // step replay counter
  u8                   step_saved;       // for replay mechanism
  u8                   manual_step;      // requested step in manual trigger mode
  u8                   step_fwd_ctr;     // step forward counter
  u8                   step_interval_ctr; // step interval counter
  u8                   step_repeat_ctr;  // step repeat counter
  u8                   step_skip_ctr;    // step skip counter
  u16                  layer_muted;      // separate layer mutes
  u16                  layer_muted_from_midi; // temporary layer mutes on incoming (and matching) events
  u16                  layer_muted_from_midi_next; // will be taken over with the next step
  u8                   lfo_cc_muted_from_midi:1; // the same for the LFO CC
  u8                   lfo_cc_muted_from_midi_next:1;
  u8                   arp_pos;          // arpeggiator position
  u8                   vu_meter;         // for visualisation in mute menu
  u32                  rec_timestamp;    // for recording function
  u8                   rec_poly_ctr;     // for recording function
  u8                   play_section;     // selects the section which should be played. If -1, no section selection
  u8                   fx_midi_ctr;      // Fx MIDI channel counter
  u32                  robotize_seed_state; // xorshift32 state for robotize; restored from tcc->robotize_bar_anchors[phase] at each musical measure boundary inside the loop
  u32                  robotize_seed_snapshots[16]; // ring of PRNG state at the start of each of the last 16 musical measures (indexed by robotize_measure_ctr & 0x0f); freeze pulls from here
  u32                  robotize_measure_ctr; // monotonic per-track musical-measure counter; increments on each global measure boundary (ref_step==0). Independent of track length, so polymetric/polyrhythmic tracks share the same robotize clock.
  u8                   robotize_loop_phase;  // measures since last anchor restore; wraps to 0 when >= tcc->robotize_loop_cycles
  u8                   robotize_pending_resync; // 1 = on next measure boundary, restore state=bar_anchors[0] and zero phase (quantized freeze OR master-sync)
} seq_core_trk_t;


typedef enum {
  SEQ_CORE_TRKMODE_Off,
  SEQ_CORE_TRKMODE_Normal,
  SEQ_CORE_TRKMODE_Transpose,
  SEQ_CORE_TRKMODE_Arpeggiator,
  SEQ_CORE_TRKMODE_ChordMask
} seq_core_trk_playmode_t;

typedef union {
  u8 ALL;
  struct {
    u8 bus:2;
  };
} seq_core_busasg_t;

typedef enum {
  SEQ_CORE_TRKDIR_Forward,
  SEQ_CORE_TRKDIR_Backward,
  SEQ_CORE_TRKDIR_PingPong,
  SEQ_CORE_TRKDIR_Pendulum,
  SEQ_CORE_TRKDIR_Random_Dir,
  SEQ_CORE_TRKDIR_Random_Step,
  SEQ_CORE_TRKDIR_Random_D_S
} seq_core_trk_dir_t;

typedef enum {
  SEQ_CORE_SLAVECLK_MUTE_Off,
  SEQ_CORE_SLAVECLK_MUTE_Enabled,
  SEQ_CORE_SLAVECLK_MUTE_OffOnNextMeasure,
} seq_core_slaveclk_mute_t;

// shared mode parameters (each track holds another value)
typedef union {
  struct {
    u8 chain; // stored in track #1
  };
  struct {
    u8 morph_pattern; // stored in track #2
  };
  struct {
    u8 scale; // stored in track #3
  };
  struct {
    u8 scale_root; // stored in track #4
  };
} seq_core_shared_t;


typedef union {
  u8 ALL;
  struct {
    u8 UNSORTED:1;     // sort mode for arpeggiator
    u8 HOLD:1;         // hold mode for transposer/arpeggiator
    u8 RESTART:1;      // track restart on key press
    u8 FORCE_SCALE:1;  // note values are forced to scale
    u8 SUSTAIN:1;      // events are sustained because SUSTAIN is set as active on the track
    u8 FIRST_NOTE:1;   // transposer takes the first played note instead of the last one
    u8 STEP_TRG:1;     // next step has to be triggered from transposer (e.g. via loopback track or external keyboard)
  };
} seq_core_trkmode_flags_t;

typedef enum {
  SEQ_CORE_FX_MIDI_MODE_BEH_Forward,   // forward to all channels
  SEQ_CORE_FX_MIDI_MODE_BEH_Alternate, // alternate between channels
  SEQ_CORE_FX_MIDI_MODE_BEH_AlternateSynchedEcho, // Alternate only on echo taps
  SEQ_CORE_FX_MIDI_MODE_BEH_Random,    // forward to random channel
} seq_core_fx_midi_mode_beh_t;

typedef union {
  u8 ALL;
  struct {
    u8 beh:3;           // Fx behaviour (seq_core_fx_midi_mode_beh_t)
    u8 fwd_non_notes:1; // forward CCs, PitchBender, Channel Pressure, Program Change to all Fx channels
  };
} seq_core_fx_midi_mode_t;

typedef union {
  u16 ALL;
  struct {
    u8 value;                // clock divider value
    u8 flags;                // combines all flags (for CC access)
  };
  struct {
    u8 value_dummy;          // clock divider value
    u8 SYNCH_TO_MEASURE:1;   // synch to globally selectable measure
    u8 TRIPLETS:1;           // play triplets
    u8 MANUAL:1;             // clock to next step only on manual requests (or via Step CC)
  };
} seq_core_clkdiv_t;


#define SEQ_CORE_NUM_LOOP_MODES 4
typedef enum {
  SEQ_CORE_LOOP_MODE_ALL_TRACKS_VIEW,
  SEQ_CORE_LOOP_MODE_ALL_TRACKS_STATIC,
  SEQ_CORE_LOOP_MODE_SELECTED_TRACK_VIEW,
  SEQ_CORE_LOOP_MODE_SELECTED_TRACK_STATIC,
} seq_core_loop_mode_t;


// Processor (noun, §3). Per-track 4-slot stack, zero-initialized. Empty slots
// (id == SEQ_PROCESSOR_ID_NONE) are skipped during render. SEQ_CORE_RenderTrack
// dispatches via switch(p->id).
//
// Phase C: chord_mask is the first real processor. It rewrites note-bearing
// bytes in the output par buffer to snap toward the bus's currently-held
// chord (PC-set). Because its input includes a live signal (bus chord), the
// renderer dirties any track carrying an enabled chord_mask slot at the top
// of every tick — see SEQ_CORE_RenderTracks.
typedef enum {
  SEQ_PROCESSOR_ID_NONE       = 0,
  SEQ_PROCESSOR_ID_CHORD_MASK = 1,
  SEQ_PROCESSOR_ID_TENSION    = 2, // GRAVITY field (Tension Workbench, §2)
  SEQ_PROCESSOR_ID_PITCH      = 3, // transpose+FTS (Track 2 pitch-chain migration)
  SEQ_PROCESSOR_ID_LIMIT      = 4, // note-limit octave fold (Track 2 Stage B)
} seq_processor_id_t;

typedef struct {
  u8  id;        // seq_processor_id_t; NONE = empty slot
  u8  enabled;   // 0 = bypass (even when id != NONE)
  u8  strength;  // 0..127 universal sweep dial; 0 = pass-through (§3)
  u8  bus;       // bus selector; meaning depends on id
  u16 drum_mask; // drum-mode scope: bit i = process drum i. 0xFFFF = all drums
                 // (matches legacy whole-track behavior). Unused outside drum mode.
} seq_processor_slot_t;


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

#include "seq_cc.h"
#include "seq_layer.h"
#include "seq_robotize.h"
#include <seq_midi_out.h>

extern s32 SEQ_CORE_Init(u32 mode);

extern s32 SEQ_CORE_ScheduleEvent(u8 track, seq_core_trk_t *t, seq_cc_trk_t *tcc, mios32_midi_package_t midi_package, seq_midi_out_event_type_t event_type, u32 timestamp, u32 len, u8 is_echo, seq_robotize_flags_t robotize_flags);

extern s32 SEQ_CORE_Reset(u32 bpm_start);
extern s32 SEQ_CORE_PlayOffEvents(void);
extern s32 SEQ_CORE_Tick(u32 bpm_tick, s8 export_track, u8 mute_nonloopback_tracks);

extern s32 SEQ_CORE_Handler(void);

extern s32 SEQ_CORE_FTS_GetScaleAndRoot(u8 track, u8 step, u8 instrument, seq_cc_trk_t *tcc, u8 *scale, u8 *root_selection, u8 *root);

extern const char *SEQ_CORE_Echo_GetDelayModeName(u8 delay_mode);
extern u8 SEQ_CORE_Echo_MapUserToInternal(u8 user_value);
extern u8 SEQ_CORE_Echo_MapInternalToUser(u8 internal_value);

extern s32 SEQ_CORE_Transpose(u8 track, u8 instrument, seq_core_trk_t *t, seq_cc_trk_t *tcc, mios32_midi_package_t *p);
extern s32 SEQ_CORE_Limit(seq_core_trk_t *t, seq_cc_trk_t *tcc, seq_layer_evnt_t *e);

extern s32 SEQ_CORE_Echo(u8 track, u8 instrument, seq_core_trk_t *t, seq_cc_trk_t *tcc, mios32_midi_package_t p, u32 bpm_tick, u32 gatelength, seq_robotize_flags_t robotize_flags);

extern s32 SEQ_CORE_ResetTrkPosAll(void);
extern s32 SEQ_CORE_SetTrkPos(u8 track, u8 value, u8 scale_value);

extern s32 SEQ_CORE_ManualTrigger(u8 step);
extern s32 SEQ_CORE_ManualSynchToMeasure(u16 tracks);

extern s32 SEQ_CORE_StepTriggerReq(u8 bus);

extern s32 SEQ_CORE_NotifyIncomingMIDIEvent(u8 track, mios32_midi_package_t p);

extern s32 SEQ_CORE_AddForwardDelay(u16 delay_ms);

extern s32 SEQ_CORE_BPM_Update(float bpm, float sweep_ramp);
extern s32 SEQ_CORE_BPM_SweepHandler(void);

extern s32 SEQ_CORE_Scrub(s32 incrementer);

extern s32 SEQ_CORE_CancelSustainedNotes(u8 track);

extern u8 SEQ_CORE_TrimNote(s32 note, u8 lower, u8 upper);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////

extern seq_core_options_t seq_core_options;
extern u8 seq_core_steps_per_measure;
extern u8 seq_core_steps_per_pattern;

extern u8 seq_core_pattern_switch_margin_ms;

extern u16 seq_core_trk_muted;
extern u16 seq_core_trk_synched_mute;
extern u16 seq_core_trk_synched_unmute;
extern seq_core_slaveclk_mute_t seq_core_slaveclk_mute;
extern u16 seq_core_trk_soloed;

extern u8 seq_core_step_update_req;

extern u8 seq_core_global_scale;
extern u8 seq_core_global_scale_root_selection;
extern u8 seq_core_keyb_scale_root;

// GRAVITY field (Tension Workbench, §2). Global bipolar dial −64..+63, center
// 0 = true pass-through. Performance state (like the SHADE scale choice it sits
// beside), NOT pattern state — persisted to the config file, not the bank.
extern s8 seq_core_tension_gravity;

extern u8 seq_core_global_transpose_enabled;

extern u8 seq_core_bpm_preset_num;
extern float seq_core_bpm_preset_tempo[SEQ_CORE_NUM_BPM_PRESETS];
extern float seq_core_bpm_preset_ramp[SEQ_CORE_NUM_BPM_PRESETS];

extern mios32_midi_port_t seq_core_metronome_port;
extern u8 seq_core_metronome_chn;
extern u8 seq_core_metronome_note_m;
extern u8 seq_core_metronome_note_b;

extern mios32_midi_port_t seq_core_shadow_out_port;
extern u8 seq_core_shadow_out_chn;

extern seq_core_state_t seq_core_state;
extern seq_core_trk_t seq_core_trk[SEQ_CORE_NUM_TRACKS];

extern seq_core_loop_mode_t seq_core_glb_loop_mode;
extern u8 seq_core_glb_loop_offset;
extern u8 seq_core_glb_loop_steps;

// Phase A render cache (see seq_core.c). Tick path reads from the per-track
// output mirrors declared in seq_par.h / seq_trg.h; any source-mutating site
// must mark the track dirty so the next tick's prologue refreshes the mirror.
extern u8 seq_render_dirty[SEQ_CORE_NUM_TRACKS];

// Phase D.1 — per-track sweep/quiet timestamp (§A2 sweep regime, §10 knob-
// detect sub-decision). Any site that mutates a processor-stack input bumps
// the timestamp via SEQ_CORE_RenderTouched(track). SEQ_CORE_RenderSweeping()
// returns 1 if the last touch landed within SEQ_RENDER_SWEEP_MS — used by
// the renderer to choose between sweep (bounded current+lookahead) and quiet
// (full whole-buffer) paths. Zero-init = "never touched" = treat as quiet.
#define SEQ_RENDER_SWEEP_MS  50

// Phase D.2 — sweep window. During sweep the renderer rewrites only this
// many steps starting at the playhead (the bytes the tick is about to read).
// Lower bound is 1 (just the current step); §A2 suggests 2–4. Chosen at 4
// because at 96 BPM 32nd-notes ≈ 78 ticks/s, a 50ms sweep window covers ~4
// ticks, so a 4-step lookahead exactly catches what the playhead reads during
// the sweep before the quiet catch-up render fires.
#define SEQ_RENDER_SWEEP_LOOKAHEAD  4

extern u32 seq_render_touched_ms[SEQ_CORE_NUM_TRACKS];

extern void SEQ_CORE_RenderTouched(u8 track);
extern u8   SEQ_CORE_RenderSweeping(u8 track);

// Phase D.3 — per-track active half-buffer index (0 or 1). Tick reads the
// active half via SEQ_PAR_OutputActive(track)/SEQ_TRG_OutputActive(track);
// renderer writes the inactive half during a quiet render and flips this
// byte at end (single-byte write = atomic on Cortex-M). Sweep renders write
// to the active half directly (no swap, tearing acceptable during knob
// motion). Zero-init: track 0 starts reading half 0 — first quiet render
// fills half 1 and swaps, so the tick never reads pre-init garbage.
extern u8 seq_render_active_buf[SEQ_CORE_NUM_TRACKS];

// Re-declared here (matching seq_par.h / seq_trg.h) so the inline accessors
// below have visibility without re-including those headers (which would
// circularly re-enter seq_core.h via their own includes).
#ifndef SEQ_PAR_MAX_BYTES
#define SEQ_PAR_MAX_BYTES   1024
#endif
#ifndef SEQ_TRG_MAX_BYTES
#define SEQ_TRG_MAX_BYTES    256
#endif
extern u8 seq_par_output_value[SEQ_CORE_NUM_TRACKS][2][SEQ_PAR_MAX_BYTES];
extern u8 seq_trg_output_value[SEQ_CORE_NUM_TRACKS][2][SEQ_TRG_MAX_BYTES];

static inline u8 *SEQ_PAR_OutputActive(u8 track) {
  return seq_par_output_value[track][seq_render_active_buf[track]];
}
static inline u8 *SEQ_PAR_OutputInactive(u8 track) {
  return seq_par_output_value[track][seq_render_active_buf[track] ^ 1];
}
static inline u8 *SEQ_TRG_OutputActive(u8 track) {
  return seq_trg_output_value[track][seq_render_active_buf[track]];
}
static inline u8 *SEQ_TRG_OutputInactive(u8 track) {
  return seq_trg_output_value[track][seq_render_active_buf[track] ^ 1];
}

// Phase B processor stack scaffolding (see seq_core.c). Zero-initialized;
// SEQ_CORE_RenderTrack iterates after the identity copy and skips empty
// slots, so phase B is observably identical to phase A.
extern seq_processor_slot_t seq_processor_stack[SEQ_CORE_NUM_TRACKS][SEQ_CORE_NUM_PROCESSOR_SLOTS];

extern void SEQ_CORE_RenderDirtySet(u8 track);
extern void SEQ_CORE_RenderDirtySetAll(void);
extern void SEQ_CORE_RenderTrack(u8 track);
extern void SEQ_CORE_RenderTracks(void);

// Phase C bridge: keep slot 0 mirrored from tcc (playmode + strength + bus)
// whenever the underlying CCs change. Called from SEQ_CC_Set.
extern void SEQ_CORE_ChordMaskSlotSync(u8 track);

// Tension Workbench (§2): keep the TENSION processor slot mirrored from tcc
// (GRIP + the shared chord-context bus/drum scope). Called from SEQ_CC_Set.
extern void SEQ_CORE_TensionSlotSync(u8 track);

// Track 2 (pitch-chain migration): keep the PITCH processor slot mirrored from
// tcc (playmode / transpose / FORCE_SCALE / transposer bus). Armed only when
// the chain is non-neutral; never armed in Arpeggiator playmode (fenced at
// emission). Called from SEQ_CC_Set.
extern void SEQ_CORE_PitchSlotSync(u8 track);

// Track 2 Stage B: keep the LIMIT processor slot mirrored from tcc
// (limit_lower/upper). Armed iff either bound is set; same arp/drum fences as
// PITCH. Called from SEQ_CC_Set.
extern void SEQ_CORE_LimitSlotSync(u8 track);

// Set the global GRAVITY dial (clamped −64..+63) and touch the field-bearing
// tracks so a live cockpit-encoder sweep re-renders smoothly. Called from the
// GRAVITY page encoder.
extern void SEQ_CORE_TensionGravitySet(s8 gravity);

// RESOLVE (§3): bar-quantized ramp of GRAVITY to the detent, landing on the next
// downbeat (or instant when the transport is stopped). TensionResolveTick runs
// the per-tick glide from the prologue; TensionResolveBoundary pins the exact 0
// landing at ref_step==0; TensionResolveCancel aborts an in-flight ramp (e.g. a
// manual GRAVITY turn).
extern void SEQ_CORE_TensionResolve(void);
extern void SEQ_CORE_TensionResolveTick(u32 bpm_tick);
extern u8   SEQ_CORE_TensionResolveBoundary(void);
extern void SEQ_CORE_TensionResolveCancel(void);

// Tension Workbench (§2.1): compute the target pitch-class band (12-bit mask)
// for the GRAVITY zone implied by `gravity`, against the chord held on `bus`
// and the global scale/root. Writes the zone id (0=detent, 1..6 = DRONE..SLIP)
// to *zone. Returns 0 at the detent (pass-through). Public for HIL pinning.
extern u16 SEQ_CORE_TensionBandMask(s8 gravity, u8 bus, u8 *zone);

// Fork capture engine — shared primitive. Forces a full quiet render of
// `track` (sweep-safe: OutputActive current across the whole buffer) and
// snapshots the lossless post-processor par/trg into caller-provided buffers.
// par_dst >= SEQ_PAR_MAX_BYTES, trg_dst >= SEQ_TRG_MAX_BYTES. Returns 0 on
// success, negative on bad args.
extern s32 SEQ_CORE_CaptureTrackOutput(u8 track, u8 *par_dst, u8 *trg_dst);

// In-place processor freeze: commit the current output (post-processor) back
// into the source par/trg layer and clear every enabled processor slot. Also
// untangles the chord_mask tcc mirror (playmode → Normal, strength → 0) so the
// next SlotSync doesn't re-arm the slot. Returns 1 if a bounce occurred (any
// slot was enabled), 0 otherwise. Sweep-safe via SEQ_CORE_CaptureTrackOutput.
extern s32 SEQ_CORE_ProcessorBounce(u8 track);

// (Track 2, 2026-06-10: SEQ_CORE_BakeForceScale is gone — the pitch chain
//  renders into the output mirror, so captures hold the heard pitch by
//  construction. The per-effect bake program ended here.)

// Fork capture verb — capture src_track's computed output into the destination
// pattern slot (dst_bank, dst_pattern), lossless, CC included. dst_group is
// vestigial (the write source-group is derived from src_track) and kept only
// for the testctrl payload shape. The source track's live RAM is snapshot and
// restored byte-identical. The destination's generative CC is reset and the
// name gets a "BNC" prefix. Takes MUTEX_SDCARD — task context only. Returns the
// SEQ_FILE_B_PatternWrite status (>=0 ok).
extern s32 SEQ_CORE_CaptureToSlot(u8 src_track, u8 dst_group, u8 dst_bank, u8 dst_pattern);

// Fork capture verb — capture src_track's computed output onto dst_track in the
// current pattern (RAM only). dst inherits src's event-mode/geometry/lower-48
// CCs so the captured bytes read correctly, then its generative CC is reset.
// Returns 0 on success, -1 bad index, -2 src == dst (use the freeze verb).
extern s32 SEQ_CORE_CaptureToTrack(u8 src_track, u8 dst_track);

// Fork capture verb — capture src_track's computed output into dst_track of the
// pattern slot (dst_bank, dst_pattern), PERSISTED to SD. Reads the target slot
// into dst_track's group (so the slot's other tracks are preserved), replaces
// dst_track with the captured output (geometry inherited from src, generative CC
// reset), writes the slot back, and restores the dst group's live RAM so the
// operation doesn't disturb what's currently loaded/playing. Takes MUTEX_SDCARD
// (two SD ops) — task context only. Returns >=0 on success, negative on error.
extern s32 SEQ_CORE_CaptureToSlotTrack(u8 src_track, u8 dst_track, u8 dst_bank, u8 dst_pattern);

// Phase D.0 — MSP/handler-stack high-water measurement (§10 gating). Paints the
// free region between `_eusrstack` and the current MSP at paint time with a
// sentinel pattern; later reads scan upward from `_eusrstack` to find the first
// non-pattern word (the deepest MSP excursion since paint). Painted ONCE from
// APP_Init before FreeRTOS scheduler starts — after that, kernel + ISRs run on
// MSP and the painted bytes get progressively overwritten as the MSP grows.
extern void SEQ_CORE_MSPPaint(void);
extern u32 SEQ_CORE_MSPHighWaterBytes(void);   // peak MSP usage since paint (0 if never grown)
extern u32 SEQ_CORE_MSPPaintExtent(void);      // hi - lo, painted region size in bytes
extern u32 SEQ_CORE_MSPPaintInitialDepth(void); // _estack - hi, MSP already used at paint time
extern u32 SEQ_CORE_MSPPaintLo(void);          // absolute address of paint floor (= &_eusrstack)
extern u32 SEQ_CORE_MSPPaintHi(void);          // absolute address of paint ceiling (= SP at paint - margin)

#endif /* _SEQ_CORE_H */
