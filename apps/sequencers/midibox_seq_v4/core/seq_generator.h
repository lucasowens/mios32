// $Id$
/*
 * Phase E — generator workflow (Turing-style pitch generator per drum slot).
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
 * Deferred to §8 step 6 (no fields allocated yet): per-step LOCK, MULT,
 * ANCHOR, SNAP, ROLL, contour shapes. The 64-byte loop is the only generator
 * state for now — phase 6 fields grow the struct.
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

#define SEQ_GENERATOR_DEFAULT_RANGE_MIN  36   // C2
#define SEQ_GENERATOR_DEFAULT_RANGE_MAX  84   // C6
#define SEQ_GENERATOR_DEFAULT_RATE        8   // gentle mutation


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
  u8 mutation_rate;  // 0..127 — per-cell reroll probability per measure
  u8 reserved;       // pad to 8B header
  u8 loop[SEQ_GENERATOR_LOOP_LEN];  // Turing loop array — pitch per step
} seq_generator_t;


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

// Restore the par-buffer from the auto-undo slot and disengage every
// generator on the snapshot's track. One-deep, global — most recent ENGAGE
// wins. Returns -1 if no snapshot is held.
extern s32 SEQ_GENERATOR_Undo(void);

// Returns 1 if a generator slot is allocated AND engaged for (track, instr).
extern s32 SEQ_GENERATOR_IsEngaged(u8 track, u8 instrument);

// Returns the slot for (track, instrument) or NULL. Callers may mutate
// range_min/range_max/mutation_rate directly — these are live dials.
extern seq_generator_t *SEQ_GENERATOR_Get(u8 track, u8 instrument);

// Force a one-shot reroll + rewrite for every engaged generator on `track`,
// outside the normal measure-boundary cadence. Used at ENGAGE to make the
// generator audible without waiting for the next wrap.
extern void SEQ_GENERATOR_ForceRewrite(u8 track);

// Tick prologue hook. Call BEFORE SEQ_CORE_RenderTracks() each tick: if a
// track just wrapped to step 0, mutate every engaged generator on that track
// and write the loop into the source par-layer. Sets render-dirty so phase D
// picks it up.
extern void SEQ_GENERATOR_Tick(void);


#endif /* _SEQ_GENERATOR_H */
