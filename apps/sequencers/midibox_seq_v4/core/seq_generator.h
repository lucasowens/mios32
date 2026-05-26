// $Id$
/*
 * Phase E — generator workflow (Turing-style pitch generator per drum slot).
 * Phase G (§8 step 6 polish) — per-step LOCK, ROLL gesture, mutation depth,
 * contour shapes.
 * Phase H (§8 step 6 follow-on) — ANCHOR/SNAP frozen-identity gesture and
 * per-step MULT multiplier.
 *
 * §3 noun. A generator deposits material into a *source* buffer, always
 * overwrites at its destination, and stays engaged across measures — the §5
 * journey lives in continuous mutation while engaged.
 *
 * Shape (phase E baseline):
 *   - Static cap-64 pool, per-(track, instrument) slot allocation. Refuse-with-
 *     message on full (LRU deferred).
 *   - ENGAGE/DISENGAGE on PITCHGEN page; LED reflects engaged state. ENGAGE
 *     snapshots the pre-engagement par-buffer into a one-deep auto-undo slot;
 *     UNDO restores it.
 *   - Per measure boundary (track wraps to step 0), mutate loop and rewrite
 *     the source Note par-layer for the engaged (track, instrument) pair.
 *     Mutation rate + range min/max are live dials.
 *
 * Phase G (§8 step 6) adds:
 *   - locks[]: 64-bit bitmap (1 bit per step) — locked steps survive mutation
 *     in both perturb and reroll paths. Toggled from the PITCHGEN page via the
 *     cursor encoder + dedicated button.
 *   - mutation_depth: how *far* each touched step moves per measure.
 *     0 = no change (frozen), 127 = full reroll across [range_min..range_max].
 *     In between = perturb existing value by ±depth semitones, clamped to
 *     range. Stacks with mutation_rate (how *many* steps get touched).
 *   - contour_shape: bias for the reroll distribution. UNIFORM (default,
 *     phase E behavior), LOW_BIAS, HIGH_BIAS, TRIANGLE. Affects only the
 *     full-reroll path (depth=127), not perturb.
 *   - ROLL gesture (SEQ_GENERATOR_Roll): one-shot reroll-of-unlocked-steps
 *     immediately, independent of measure-boundary cadence. Lets the user
 *     "freeze the music" (rate=0 + locks held) and trigger fresh variations
 *     on demand.
 *
 * Phase H (§8 step 6 follow-on; §11 glossary lines 1199–1202) adds:
 *   - anchor[]: 64-byte frozen snapshot of loop[]. Auto-captured at first
 *     ENGAGE seed; refreshable via the ANCHOR gesture. SNAP restores
 *     loop[] from anchor[] (hard return to identity, the §5.3 "identifiable
 *     yet unexpected return" mechanism). Does not disengage.
 *   - mult[]: 64 packed 4-bit codes (32 B) — per-step multiplier on the
 *     touch probability. Codes: 0 = 0× (mute mutation), 1 = 0.5×,
 *     2 = 1× (default — phase G behavior), 3 = 2×. ROLL ignores MULT
 *     (it's the on-demand override). Stored 0x22 (= code 2 both nibbles)
 *     by default at ENGAGE so existing behavior is preserved.
 *
 * ==========================================================================
 */

#ifndef _SEQ_GENERATOR_H
#define _SEQ_GENERATOR_H

#include "seq_core.h"


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////

#define SEQ_GENERATOR_POOL_SIZE      64
#define SEQ_GENERATOR_LOOP_LEN       64
#define SEQ_GENERATOR_INSTRUMENTS    16   // max drum slots per track

#define SEQ_GENERATOR_LOCKS_BYTES    (SEQ_GENERATOR_LOOP_LEN / 8)  // 8 — bitmap
#define SEQ_GENERATOR_MULT_BYTES     (SEQ_GENERATOR_LOOP_LEN / 2)  // 32 — 4 bits/step

// Defaults tuned 2026-05-26 for the techno bass-line use case: narrow
// one-octave bass range, aggressive mutation, near-full reroll, triangle
// contour (clusters notes around mid-range). The original phase-E
// defaults (4-octave range, gentle rate, full reroll, uniform contour)
// were preserved for backward compatibility through phase G but never
// matched the actual live use — every ENGAGE got immediately re-dialed.
#define SEQ_GENERATOR_DEFAULT_RANGE_MIN  36   // LCD "C-1" (one octave below mid-C)
#define SEQ_GENERATOR_DEFAULT_RANGE_MAX  48   // LCD "C-2" (one octave above min)
#define SEQ_GENERATOR_DEFAULT_RATE      100   // aggressive mutation
#define SEQ_GENERATOR_DEFAULT_DEPTH     120   // near-full reroll
#define SEQ_GENERATOR_DEFAULT_CONTOUR     3   // TRIANGLE (mid-biased)

// MULT codes (4-bit). Codes 0..3 are the live UI cycle; 4..15 are reserved
// (treated as default 1× by the mutate path) so future expansion is non-
// breaking. Default = 2 (1× — phase G behavior preserved).
#define SEQ_GENERATOR_MULT_MUTE     0   // 0×  — step frozen in the rate path
#define SEQ_GENERATOR_MULT_HALF     1   // 0.5×
#define SEQ_GENERATOR_MULT_DEFAULT  2   // 1×  — no scaling (default)
#define SEQ_GENERATOR_MULT_DOUBLE   3   // 2×
#define SEQ_GENERATOR_MULT_NUM      4   // cycle length
#define SEQ_GENERATOR_MULT_PACKED_DEFAULT  0x22  // both nibbles = MULT_DEFAULT

// Contour shape codes. The reroll path uses this to bias the distribution
// (full-reroll path only — depth=127). Perturb path ignores contour.
typedef enum {
  SEQ_GENERATOR_CONTOUR_UNIFORM   = 0,  // flat across [range_min..range_max]
  SEQ_GENERATOR_CONTOUR_LOW_BIAS  = 1,  // weighted toward range_min (parabolic)
  SEQ_GENERATOR_CONTOUR_HIGH_BIAS = 2,  // weighted toward range_max
  SEQ_GENERATOR_CONTOUR_TRIANGLE  = 3,  // weighted toward mid-range
  SEQ_GENERATOR_CONTOUR_NUM
} seq_generator_contour_t;


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////

typedef struct {
  u8 in_use;         // pool slot assigned to a (track, instrument)
  u8 engaged;        // 1 = mutating + writing source on measure boundary
  u8 track;          // 0..SEQ_CORE_NUM_TRACKS-1
  u8 instrument;     // 0..SEQ_GENERATOR_INSTRUMENTS-1
  u8 range_min;      // pitch lower bound (1..127, clamped to ≤ range_max)
  u8 range_max;      // pitch upper bound (1..127, clamped to ≥ range_min)
  u8 mutation_rate;  // 0..127 — per-cell touch probability per measure
  u8 mutation_depth; // 0..127 — how far each touched cell moves
                     //   0   = no change (frozen)
                     //   127 = full reroll (= phase E behavior)
                     //   N   = perturb existing by ±N semitones, clamped
  u8 contour_shape;  // seq_generator_contour_t — biases full-reroll only
  u8 anchor_valid;   // 1 = anchor[] holds a captured snapshot (phase H)
  u8 reserved[2];    // pad — keep header 12B aligned to follow the loop
  u8 loop[SEQ_GENERATOR_LOOP_LEN];        // Turing loop array — pitch per step
  u8 locks[SEQ_GENERATOR_LOCKS_BYTES];    // bitmap; bit set => step locked
  u8 anchor[SEQ_GENERATOR_LOOP_LEN];      // phase H — frozen identity (SNAP target)
  u8 mult[SEQ_GENERATOR_MULT_BYTES];      // phase H — 4-bit per-step multiplier
} seq_generator_t;


// Lock bitmap helpers — small, inlinable, kept here so callers don't
// reach into locks[] directly.
static inline u8 SEQ_GENERATOR_LockGet(const seq_generator_t *g, u8 step)
{
  if( step >= SEQ_GENERATOR_LOOP_LEN ) return 0;
  return (g->locks[step >> 3] >> (step & 7)) & 1;
}

static inline void SEQ_GENERATOR_LockSet(seq_generator_t *g, u8 step, u8 on)
{
  if( step >= SEQ_GENERATOR_LOOP_LEN ) return;
  if( on ) g->locks[step >> 3] |=  (1 << (step & 7));
  else     g->locks[step >> 3] &= ~(1 << (step & 7));
}


// Phase H — per-step MULT helpers. 4 bits per step, packed two-per-byte
// (low nibble = even step, high nibble = odd step). Returns the raw 4-bit
// code; mutate-path maps 0..3 to {0×, 0.5×, 1×, 2×} (codes 4..15 fall
// through to 1× as a forward-compatible reserved range).
static inline u8 SEQ_GENERATOR_MultGet(const seq_generator_t *g, u8 step)
{
  if( step >= SEQ_GENERATOR_LOOP_LEN ) return SEQ_GENERATOR_MULT_DEFAULT;
  u8 byte = g->mult[step >> 1];
  return (step & 1) ? (u8)(byte >> 4) : (u8)(byte & 0x0f);
}

static inline void SEQ_GENERATOR_MultSet(seq_generator_t *g, u8 step, u8 code)
{
  if( step >= SEQ_GENERATOR_LOOP_LEN ) return;
  code &= 0x0f;
  u8 *p = &g->mult[step >> 1];
  if( step & 1 ) *p = (u8)((*p & 0x0f) | (code << 4));
  else           *p = (u8)((*p & 0xf0) | code);
}


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_GENERATOR_Init(u32 mode);

// Allocate (or re-engage) a generator slot for (track, instrument).
// Returns 0 on success, -1 if pool is full, -2 if the track is not in drum
// mode, -3 if no Note par-layer is assigned. On first ENGAGE for a given
// (track, instrument), snapshots the track's par-buffer into the auto-undo
// slot and seeds the loop with an initial reroll; subsequent re-engages flip
// engaged back on without re-snapshotting.
extern s32 SEQ_GENERATOR_Engage(u8 track, u8 instrument);

// Stop mutating; source stays as last written. Slot remains allocated so the
// loop survives DISENGAGE→ENGAGE without re-snapshotting undo.
extern s32 SEQ_GENERATOR_Disengage(u8 track, u8 instrument);

// Phase F BOUNCE (generator half): freeze + disengage + free the pool slot.
// Source stays as last written (the transcribed loop). Loop is discarded, so
// the next ENGAGE on this (track, instrument) seeds a fresh Turing line. The
// undo slot is preserved — UNDO after BOUNCE still rolls back to pre-engage,
// which is the live-safety net (§3 "live-safety" rule). Returns 0 if a slot
// was freed, -1 if no slot existed.
extern s32 SEQ_GENERATOR_Bounce(u8 track, u8 instrument);

// Phase F.2 BOUNCE-relocate: deliver the §3 destination semantic for BOUNCE.
// Transcribes the engaged gen's loop into the destination's Note par-layer
// (additive at dst — original dst content overwritten by the bounce), then
// restores the src track's whole par-buffer from the global one-deep undo
// slot so the source is back to pre-engagement (the user's iterate-and-stack
// workflow). Frees the gen slot and invalidates the undo (it has been spent).
//
// Returns:
//   0  on success
//  -1  no gen engaged at (src_track, src_instr)
//  -2  bad track/instr indices, or dst is not drum-mode / missing Note layer
//  -3  undo slot doesn't cover this gen's track (e.g. another gen on a
//      different track was the most-recent first-ENGAGE) — refuse rather
//      than restore stale state. Caller should UNDO + re-ENGAGE first.
//
// Caveat: whole-track restore on src wipes any edits to *other* drum slots
// on the src track made since this gen's first ENGAGE. The one-deep undo
// is a live-safety net, not a transactional history (§3 wording).
extern s32 SEQ_GENERATOR_BounceRelocate(u8 src_track, u8 src_instr,
                                        u8 dst_track, u8 dst_instr);

// Returns 1 if any pool slot is in_use && engaged on `track`, writing the
// instrument coordinate to *out_instr. Returns 0 if none. Used by PITCHGEN
// BOUNCE to find the gen to relocate when the cursor has moved to a
// different drum slot than the engaged gen.
extern s32 SEQ_GENERATOR_FindEngagedOnTrack(u8 track, u8 *out_instr);

// Restore the par-buffer from the auto-undo slot and disengage every
// generator on the snapshot's track. One-deep, global — most recent ENGAGE
// wins. Returns -1 if no snapshot is held.
extern s32 SEQ_GENERATOR_Undo(void);

// Returns 1 if a generator slot is allocated AND engaged for (track, instr).
extern s32 SEQ_GENERATOR_IsEngaged(u8 track, u8 instrument);

// Returns the slot for (track, instrument) or NULL. Callers may mutate
// range_min/range_max/mutation_rate/mutation_depth/contour_shape directly —
// these are live dials.
extern seq_generator_t *SEQ_GENERATOR_Get(u8 track, u8 instrument);

// Force a one-shot reroll + rewrite for every engaged generator on `track`,
// outside the normal measure-boundary cadence. Used at ENGAGE to make the
// generator audible without waiting for the next wrap.
extern void SEQ_GENERATOR_ForceRewrite(u8 track);

// Phase G ROLL gesture: for every engaged generator on `track`, reroll every
// *unlocked* step in its loop (locked steps preserved verbatim) and rewrite
// the source par-layer. Independent of measure-boundary cadence and of the
// rate dial — this is the on-demand reroll button. Honors contour_shape.
// Returns the number of generators rolled (0 if none engaged on track).
extern u8 SEQ_GENERATOR_Roll(u8 track);

// Phase G per-step LOCK toggle. Returns the new lock state (0/1) on success
// or -1 if no allocated slot exists for (track, instrument) or step out of
// range. Locks survive both perturb and reroll; they're cleared when the slot
// is freed (BOUNCE-in-place, relocate, or pool re-init).
extern s32 SEQ_GENERATOR_LockToggle(u8 track, u8 instrument, u8 step);

// Phase H ANCHOR — re-snapshot current loop[] into anchor[]. Marks the slot's
// anchor as valid (auto-anchor at ENGAGE already sets this; ANCHOR refreshes
// the captured identity to "what's playing right now"). Returns 0 on success,
// -1 if no allocated slot exists for (track, instrument).
extern s32 SEQ_GENERATOR_Anchor(u8 track, u8 instrument);

// Phase H SNAP — restore loop[] from anchor[] and rewrite source. Hard return
// to the frozen identity (§5.3 mechanism). Does NOT disengage. Returns 0 on
// success, -1 if no slot, -2 if the slot has never been anchored. Locked
// steps are NOT special-cased: SNAP is unconditional. Per-step MULT settings
// are unchanged (they live on top of the anchor's loop).
extern s32 SEQ_GENERATOR_Snap(u8 track, u8 instrument);

// Phase H MULT cycle — advance the per-step multiplier code at `step` by one
// position (0→1→2→3→0). Returns the new code on success or -1 if no slot /
// step out of range. Codes ≥ MULT_NUM (reserved) wrap back to 0 on the next
// press. MULT participates in the rate-gated mutate path only; ROLL ignores
// it (ROLL is the on-demand override that bypasses rate, depth, and MULT).
extern s32 SEQ_GENERATOR_MultCycle(u8 track, u8 instrument, u8 step);

// Tick prologue hook. Call BEFORE SEQ_CORE_RenderTracks() each tick: if a
// track just wrapped to step 0, mutate every engaged generator on that track
// and write the loop into the source par-layer. Sets render-dirty so phase D
// picks it up.
extern void SEQ_GENERATOR_Tick(void);


#endif /* _SEQ_GENERATOR_H */
