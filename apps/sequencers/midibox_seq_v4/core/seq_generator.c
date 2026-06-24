// $Id$
/*
 * Phase E — generator workflow.
 * Phase G — §8 step 6 polish (per-step LOCK, ROLL, depth, contour).
 * Phase H — §8 step 6 follow-on (ANCHOR/SNAP frozen-identity, per-step MULT).
 * See seq_generator.h for the model. This file implements:
 *   - the cap-64 pool + per-(track, instrument) sparse lookup
 *   - one-deep global auto-undo
 *   - per-measure-boundary mutate-then-rewrite (Turing loop → source par)
 *   - phase G: lock-aware mutation, depth-controlled perturb vs reroll,
 *     contour-biased reroll distribution, on-demand ROLL gesture
 *   - phase H: auto-anchor at ENGAGE seed, ANCHOR refresh, SNAP restore,
 *     per-step MULT multiplier scaling the rate-gated mutate decision
 *
 * Measure-boundary detection: per-track last_seen_step. SEQ_GENERATOR_Tick
 * fires when t->step == 0 AND last_seen != 0 (or on the first call after
 * init, via 0xFF sentinel). Runs from the tick prologue in SEQ_CORE_Tick
 * BEFORE SEQ_CORE_RenderTracks, so any source rewrites mark the track
 * render-dirty and phase D's renderer picks up the change before the tick
 * body reads the output mirror.
 *
 * ==========================================================================
 */

#include <mios32.h>
#include <string.h>

#include "seq_generator.h"
#include "seq_core.h"
#include "seq_cc.h"
#include "seq_par.h"
#include "seq_pattern.h"
#include "seq_layer.h"
#include "seq_random.h"


#ifndef CCM_SECTION
#define CCM_SECTION
#endif


/////////////////////////////////////////////////////////////////////////////
// Local state — CCMRAM
/////////////////////////////////////////////////////////////////////////////

static seq_generator_t CCM_SECTION pool[SEQ_GENERATOR_POOL_SIZE];

// PHRASES drift gate: 1 only while the Tick is transcribing an auto-mutate into
// the source (see header). Lets SEQ_PATTERN_DirtySetTrack keep generator
// wandering out of the phrase-drift signal without touching seq_pattern_dirty.
u8 seq_generator_in_automutate = 0;

// Sparse map: pool index per (track, instrument), 0xFF = unallocated.
static u8 CCM_SECTION pool_index[SEQ_CORE_NUM_TRACKS][SEQ_GENERATOR_INSTRUMENTS];

// The generator one-deep auto-undo is now part of the unified action journal
// (seq_core.c, §10(a2)): ENGAGE arms it via SEQ_CORE_JournalArm and the GP2
// UNDO / disk-load invalidate route through SEQ_CORE_JournalUndo /
// SEQ_CORE_JournalInvalidate. The journal snapshots the FULL track (incl. this
// track's generators), a superset of the old par-only slot, so a generator
// UNDO restores the pre-ENGAGE source AND disengages the seeded generators in
// one shot (the journal_restore generator-restore subsumes the old explicit
// disengage loop).


/////////////////////////////////////////////////////////////////////////////
// Local state — main RAM (small)
/////////////////////////////////////////////////////////////////////////////

// 0xFF sentinel ⇒ fire on first prologue call after Reset (treat as if the
// track just wrapped). After that, normal wrap-detect logic.
static u8 last_seen_step[SEQ_CORE_NUM_TRACKS];


/////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////

static void normalize_range(u8 *lo, u8 *hi)
{
  if( *lo > *hi ) { u8 t = *lo; *lo = *hi; *hi = t; }
  if( *lo == 0 ) *lo = 1; // 0 is the drum lay_const fallback marker
  if( *hi > 127 ) *hi = 127;
  if( *lo > *hi ) *lo = *hi;
}


// Pick a fresh pitch in [lo..hi] biased by contour shape. UNIFORM is the
// existing phase E behavior. Bias shapes draw two uniforms and combine them
// to cheaply approximate the target distribution without trig or tables.
// Per-track-RNG keystone (2026-06-19): draws from the caller-owned xorshift
// stream `*seed` (the slot's own seed) instead of the global RNG, so the
// wander is deterministic + seekable. GenRangeXorshift shares Gen_Range's
// min==max zero-advance short-circuit, so the draw COUNT is identical to the
// legacy code — a degenerate (span==1) range advances the stream zero times.
static u8 reroll_pitch(u32 *seed, u8 lo, u8 hi, u8 contour)
{
  normalize_range(&lo, &hi);
  u32 span = (u32)(hi - lo) + 1;  // ≥1

  switch( contour ) {
    case SEQ_GENERATOR_CONTOUR_LOW_BIAS: {
      // min of two uniforms ⇒ density falls toward hi — concentrates near lo.
      u32 a = SEQ_RANDOM_GenRangeXorshift(seed, 0, span - 1);
      u32 b = SEQ_RANDOM_GenRangeXorshift(seed, 0, span - 1);
      return (u8)(lo + (a < b ? a : b));
    }
    case SEQ_GENERATOR_CONTOUR_HIGH_BIAS: {
      u32 a = SEQ_RANDOM_GenRangeXorshift(seed, 0, span - 1);
      u32 b = SEQ_RANDOM_GenRangeXorshift(seed, 0, span - 1);
      return (u8)(lo + (a > b ? a : b));
    }
    case SEQ_GENERATOR_CONTOUR_TRIANGLE: {
      // sum of two uniforms over half-span ⇒ triangular, peak at mid-range.
      u32 half = (span + 1) / 2;
      u32 a = SEQ_RANDOM_GenRangeXorshift(seed, 0, half - 1);
      u32 b = SEQ_RANDOM_GenRangeXorshift(seed, 0, half - 1);
      u32 v = a + b;
      if( v >= span ) v = span - 1;
      return (u8)(lo + v);
    }
    default: // SEQ_GENERATOR_CONTOUR_UNIFORM
      return (u8)SEQ_RANDOM_GenRangeXorshift(seed, lo, hi);
  }
}


// Perturb an existing value by ±depth semitones, clamped to [lo..hi].
// depth==0 returns existing unchanged; the caller is responsible for not
// calling perturb_pitch when depth==0 (early-out keeps the hot path clean).
static u8 perturb_pitch(u32 *seed, u8 existing, u8 lo, u8 hi, u8 depth)
{
  normalize_range(&lo, &hi);
  if( existing < lo ) existing = lo;
  if( existing > hi ) existing = hi;

  // Symmetric ±depth window; 2*depth+1 buckets from the slot's xorshift stream.
  u32 d = depth;
  u32 bucket = SEQ_RANDOM_GenRangeXorshift(seed, 0, 2*d);
  s32 delta = (s32)bucket - (s32)d;
  s32 v = (s32)existing + delta;
  if( v < (s32)lo ) v = lo;
  if( v > (s32)hi ) v = hi;
  return (u8)v;
}


// Phase H — map a 4-bit MULT code to the rate-gate threshold. Comparison in
// mutate_loop is `r >= threshold` with r ∈ [0, 254]. MULT_DEFAULT (code 2)
// gives the phase G threshold rate*2 unchanged; codes 0 / 1 / 3 give the
// 0× / 0.5× / 2× sweeps; codes 4..15 are reserved (treated as 1×).
static u32 mult_threshold(u8 rate, u8 mult_code)
{
  switch( mult_code ) {
    case SEQ_GENERATOR_MULT_MUTE:   return 0;
    case SEQ_GENERATOR_MULT_HALF:   return (u32)rate;
    case SEQ_GENERATOR_MULT_DOUBLE: {
      u32 t = (u32)rate * 4;
      return t > 255 ? 255 : t;
    }
    default: /* DEFAULT + reserved */ return (u32)rate * 2;
  }
}


// Mutate the loop in place. Phase G upgrades phase E's pure-reroll path:
//   - locked steps are never touched (LOCK survives any mutation)
//   - rate gates "is this step touched at all?" (per-step probability)
//   - depth selects perturb-vs-reroll: 0 = no-op, 127 = full reroll
//     (phase E behavior), in between = perturb by ±depth around existing
//   - contour biases the reroll path only
// Phase H multiplies the rate-gate per step via MULT (codes 0..3).
static void mutate_loop(seq_generator_t *g)
{
  if( g->mutation_rate == 0 )
    return; // pass-through (§2.3 sweep contract)
  if( g->mutation_depth == 0 )
    return; // frozen — touched cells still don't move

  u8 i;
  for(i=0; i<SEQ_GENERATOR_LOOP_LEN; ++i) {
    if( SEQ_GENERATOR_LockGet(g, i) )
      continue; // §G LOCK — preserved verbatim

    // A uniform u8 in [0,254] from the slot's own xorshift stream; the
    // threshold is rate*2 scaled by the per-step MULT code (phase H). The gate
    // and the reroll/perturb value draw MUST share g->seed: the per-step
    // interleave (gate-i → maybe value-i → gate-(i+1)) is one ordered stream,
    // so splitting the gate onto a separate source would desync re-sim.
    u32 threshold = mult_threshold(g->mutation_rate, SEQ_GENERATOR_MultGet(g, i));
    if( threshold == 0 )
      continue; // MULT_MUTE — step frozen in the rate-gated path

    u32 r = SEQ_RANDOM_GenRangeXorshift(&g->seed, 0, 254);
    if( r >= threshold )
      continue;

    if( g->mutation_depth >= 127 ) {
      g->loop[i] = reroll_pitch(&g->seed, g->range_min, g->range_max, g->contour_shape);
    } else {
      g->loop[i] = perturb_pitch(&g->seed, g->loop[i], g->range_min, g->range_max,
                                 g->mutation_depth);
    }
  }
}


// ROLL gesture (phase G): reroll *every unlocked step* immediately, ignoring
// rate and depth. Always honors contour. This is the "manual variation"
// trigger — pairs naturally with rate=0 (frozen) + locks + ROLL = punctuated
// surprise within an otherwise stable loop.
static void roll_loop(seq_generator_t *g)
{
  u8 i;
  for(i=0; i<SEQ_GENERATOR_LOOP_LEN; ++i) {
    if( SEQ_GENERATOR_LockGet(g, i) )
      continue;
    g->loop[i] = reroll_pitch(&g->seed, g->range_min, g->range_max, g->contour_shape);
  }
}


// Transcribe loop[] → Note par-layer `par_layer` of the *target* (track,
// instrument). Track-type-agnostic: on a drum track dst_instr selects the drum
// line and par_layer is the shared link_par_layer_note; on a normal track
// dst_instr is 0 and par_layer is the cursor's chosen Note layer — SEQ_PAR
// addressing is the same (track, step, par_layer, instrument). Tiles when
// num_p_steps > LOOP_LEN. Sets render-dirty on the target track. Returns 0 on
// success, -1 if par_layer is out of range (or the track has zero steps).
static s32 write_loop_to(const seq_generator_t *g, u8 dst_track, u8 dst_instr,
                         u8 par_layer)
{
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_instr >= SEQ_GENERATOR_INSTRUMENTS ) return -1;
  if( par_layer >= SEQ_PAR_NumLayersGet(dst_track) ) return -1;

  s32 num_p_steps_s = SEQ_PAR_NumStepsGet(dst_track);
  if( num_p_steps_s <= 0 ) return -1;
  u16 num_p_steps = (u16)num_p_steps_s;

  u16 step;
  for(step=0; step<num_p_steps; ++step) {
    u8 v = g->loop[step & (SEQ_GENERATOR_LOOP_LEN - 1)];
    SEQ_PAR_Set(dst_track, step, par_layer, dst_instr, v);
  }

  seq_render_dirty[dst_track] = 1;
  return 0;
}


// Convenience wrapper: write to the gen's own (track, instrument, par_layer) —
// the engage / measure-boundary path. Failures here are by construction the
// "track misconfigured" case which the engage path already screens for.
static void write_loop_to_source(const seq_generator_t *g)
{
  (void)write_loop_to(g, g->track, g->instrument, g->par_layer);
}


// Seed loop[] with an initial reroll across the range (called once at first
// ENGAGE so the engaged generator is immediately audible without waiting for
// the next measure). Honors contour; locks aren't relevant — fresh slot.
static void seed_loop(seq_generator_t *g)
{
  u8 i;
  for(i=0; i<SEQ_GENERATOR_LOOP_LEN; ++i)
    g->loop[i] = reroll_pitch(&g->seed, g->range_min, g->range_max, g->contour_shape);
}


// Allocate a fresh pool slot or return NULL. Does NOT set in_use — the caller
// initializes the slot fields and then sets in_use, keeping the pool walk
// simple and the slot in a sane state by the time anyone could read it.
static seq_generator_t *alloc_slot(void)
{
  u8 i;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    if( !pool[i].in_use )
      return &pool[i];
  }
  return NULL;
}


/////////////////////////////////////////////////////////////////////////////
// Init
/////////////////////////////////////////////////////////////////////////////

s32 SEQ_GENERATOR_Init(u32 mode)
{
  // Pool slots zero-init via .bss_ccm; explicit zeroing here defeats DCE and
  // also lets a runtime re-init (mode-1 reload) start clean.
  memset(pool, 0, sizeof(pool));
  memset(pool_index, 0xFF, sizeof(pool_index));
  memset(last_seen_step, 0xFF, sizeof(last_seen_step));
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Public API
/////////////////////////////////////////////////////////////////////////////

s32 SEQ_GENERATOR_IsEngaged(u8 track, u8 instrument)
{
  if( track >= SEQ_CORE_NUM_TRACKS || instrument >= SEQ_GENERATOR_INSTRUMENTS )
    return 0;
  u8 ix = pool_index[track][instrument];
  if( ix == 0xFF || ix >= SEQ_GENERATOR_POOL_SIZE ) return 0;
  return pool[ix].engaged ? 1 : 0;
}


// Count this track's allocated (in_use) generator slots — matches what
// SEQ_GENERATOR_TrackSnapshot will copy. Used by the CAPTURE ring to detect
// when a track has more slots than the ring caps (incomplete capture -> refuse).
u8 SEQ_GENERATOR_TrackEngagedCount(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0;
  u8 n = 0, instr;
  for(instr=0; instr<SEQ_GENERATOR_INSTRUMENTS; ++instr)
    if( SEQ_GENERATOR_Get(track, instr) != NULL )
      ++n;
  return n;
}


// last_seen_step accessors — the per-track measure-wrap detector SEQ_GENERATOR_Tick
// uses (mutate fires when step wraps 0 with prev!=0). The CAPTURE re-sim sets this
// to 0 at the window-start rewind so the FIRST driven boundary does NOT re-mutate
// the restored (post-mutate) window-start loop, and snapshots/restores it so the
// borrow is non-destructive.
u8 SEQ_GENERATOR_LastSeenStepGet(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0xFF;
  return last_seen_step[track];
}

void SEQ_GENERATOR_LastSeenStepSet(u8 track, u8 step)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return;
  last_seen_step[track] = step;
}


seq_generator_t *SEQ_GENERATOR_Get(u8 track, u8 instrument)
{
  if( track >= SEQ_CORE_NUM_TRACKS || instrument >= SEQ_GENERATOR_INSTRUMENTS )
    return NULL;
  u8 ix = pool_index[track][instrument];
  if( ix == 0xFF || ix >= SEQ_GENERATOR_POOL_SIZE ) return NULL;
  if( !pool[ix].in_use ) return NULL;
  return &pool[ix];
}


s32 SEQ_GENERATOR_Engage(u8 track, u8 instrument, u8 par_layer)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -2;
  if( instrument >= SEQ_GENERATOR_INSTRUMENTS ) return -2;

  // Track-type-agnostic: the only requirement is a Note par-layer to write
  // into (set on drum AND normal tracks by the seq_cc.c link scan). On a
  // normal track the caller passes instrument 0 (the single melodic line).
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->link_par_layer_note < 0 ) return -3;

  // Defensive: an out-of-range par_layer (e.g. the UI's 255 fallback when the
  // cursor wasn't on a Note layer) collapses to the track's linked Note layer,
  // which is valid here (link >= 0 checked above).
  if( par_layer >= SEQ_PAR_NumLayersGet(track) )
    par_layer = (u8)tcc->link_par_layer_note;

  u8 ix = pool_index[track][instrument];
  if( ix != 0xFF && ix < SEQ_GENERATOR_POOL_SIZE && pool[ix].in_use ) {
    // Re-engage: keep loop content; adopt the (possibly new) cursor target so
    // cursor still wins; flip engaged on; force a rewrite so the current
    // (possibly UNDO-restored or paused-out-of-sync) source matches. NOT armed:
    // matches the pre-consolidation "first-ENGAGE only" undo so a resync
    // double-tap doesn't consume the user's last undoable gesture.
    pool[ix].par_layer = par_layer;
    pool[ix].engaged = 1;
    write_loop_to_source(&pool[ix]);
    return 0;
  }

  seq_generator_t *g = alloc_slot();
  if( g == NULL ) return -1;  // pool full — arms nothing (prior undo intact)

  // Arm the unified undo net now that a NEW slot is committed: after the
  // pool-full return (so a failed ENGAGE doesn't clobber the prior gesture's
  // undo) and BEFORE g is marked in_use / its track is set, so the snapshot's
  // generators EXCLUDE the one we're about to add (UNDO then restores the
  // pre-ENGAGE source AND removes this seeded generator). Same placement as the
  // pre-consolidation undo_slot arm.
  SEQ_CORE_JournalArm(track);

  g->track          = track;
  g->instrument     = instrument;
  g->par_layer      = par_layer;
  g->range_min      = SEQ_GENERATOR_DEFAULT_RANGE_MIN;
  g->range_max      = SEQ_GENERATOR_DEFAULT_RANGE_MAX;
  g->mutation_rate  = SEQ_GENERATOR_DEFAULT_RATE;
  g->mutation_depth = SEQ_GENERATOR_DEFAULT_DEPTH;
  g->contour_shape  = SEQ_GENERATOR_DEFAULT_CONTOUR;
  g->engaged        = 1;
  g->in_use         = 1;
  // Per-track-RNG keystone (2026-06-19): mint this slot's xorshift seed fresh
  // from the global RNG so each ENGAGE still produces a fresh Turing line (no
  // feel regression vs the old global-RNG seed_loop), then let the slot's own
  // stream carry the wander deterministically + seekably. |1 forces non-zero —
  // a 0 state self-substitutes 0xdeadbabe and would alias every un-minted slot
  // onto one identical stream (the load path guards the same way in SlotSet).
  g->seed = SEQ_RANDOM_Gen(0) | 1;
  // alloc_slot returned a memset-clean slot, so locks[] is already 0; no
  // explicit clear needed (slots are also memset-cleared at free).
  seed_loop(g);

  // Phase H — auto-anchor the seeded loop as the slot's initial frozen
  // identity. SNAP returns here until the user re-anchors. Also stamp
  // mult[] to MULT_DEFAULT for every step (memset gave us 0x00 = MUTE
  // which would silently freeze the loop — wrong default).
  memcpy(g->anchor, g->loop, SEQ_GENERATOR_LOOP_LEN);
  g->anchor_valid = 1;
  memset(g->mult, SEQ_GENERATOR_MULT_PACKED_DEFAULT, SEQ_GENERATOR_MULT_BYTES);

  pool_index[track][instrument] = (u8)(g - pool);

  write_loop_to_source(g);
  return 0;
}


s32 SEQ_GENERATOR_Disengage(u8 track, u8 instrument)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( instrument >= SEQ_GENERATOR_INSTRUMENTS ) return -1;
  u8 ix = pool_index[track][instrument];
  if( ix == 0xFF || ix >= SEQ_GENERATOR_POOL_SIZE ) return -1;
  pool[ix].engaged = 0;
  return 0;
}


s32 SEQ_GENERATOR_Bounce(u8 track, u8 instrument)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( instrument >= SEQ_GENERATOR_INSTRUMENTS ) return -1;
  u8 ix = pool_index[track][instrument];
  if( ix == 0xFF || ix >= SEQ_GENERATOR_POOL_SIZE ) return -1;

  // Source already holds the last transcribed loop — nothing to write here.
  // Clear the slot (loop discarded) and the sparse index so a subsequent
  // ENGAGE allocates fresh and reroll-seeds. The undo slot is intentionally
  // left intact: BOUNCE freezes-and-disengages but the user can still UNDO
  // back to pre-engagement (§3 live-safety net).
  memset(&pool[ix], 0, sizeof(pool[ix]));
  pool_index[track][instrument] = 0xFF;
  return 0;
}


// The generator page's GP2 UNDO. Routed to the unified journal: the full-track
// restore puts the pre-ENGAGE source back AND restores the pre-ENGAGE
// generators (removing the seeded one), so the old explicit par-memcpy +
// disengage-loop is subsumed. Undo-only here (the global SELECT+CLEAR toggle
// owns redo). Returns 0 / -1 to keep the GP2 caller's contract.
s32 SEQ_GENERATOR_Undo(void)
{
  return (SEQ_CORE_JournalUndo() >= 0) ? 0 : -1;
}


// Drop the undo snapshot. Called by the disk-load paths (SEQ_PATTERN_Load /
// SEQ_PATTERN_SnapshotRead): after a load replaces a track's state from disk,
// the pre-load snapshot is stale — an UNDO would otherwise clobber the
// freshly-loaded track with pre-load bytes. Now invalidates the whole journal
// (which subsumes the old generator slot). BOUNCE deliberately does NOT call
// this (§3 live-safety net).
s32 SEQ_GENERATOR_UndoInvalidate(void)
{
  return SEQ_CORE_JournalInvalidate();
}


/////////////////////////////////////////////////////////////////////////////
// FEARLESS SWITCHING Stage B — persistence primitives (gen state is slot
// content; see seq_generator.h for the contracts).
/////////////////////////////////////////////////////////////////////////////

s32 SEQ_GENERATOR_TrackClear(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  u8 i;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    if( pool[i].in_use && pool[i].track == track )
      memset(&pool[i], 0, sizeof(pool[i]));
  }
  memset(pool_index[track], 0xFF, SEQ_GENERATOR_INSTRUMENTS);
  return 0;
}


s32 SEQ_GENERATOR_SlotSet(u8 track, u8 instrument, const seq_generator_t *src)
{
  if( src == NULL ) return -2;
  if( track >= SEQ_CORE_NUM_TRACKS ) return -2;
  if( instrument >= SEQ_GENERATOR_INSTRUMENTS ) return -2;

  // Same defensive collapse as Engage: a par_layer the just-loaded geometry
  // doesn't have falls back to the track's linked Note layer.
  u8 par_layer = src->par_layer;
  if( par_layer >= SEQ_PAR_NumLayersGet(track) ) {
    seq_cc_trk_t *tcc = &seq_cc_trk[track];
    if( tcc->link_par_layer_note < 0 ) return -3;
    par_layer = (u8)tcc->link_par_layer_note;
  }

  seq_generator_t *g;
  u8 ix = pool_index[track][instrument];
  if( ix != 0xFF && ix < SEQ_GENERATOR_POOL_SIZE && pool[ix].in_use ) {
    g = &pool[ix];
  } else {
    g = alloc_slot();
    if( g == NULL ) return -1;
    pool_index[track][instrument] = (u8)(g - pool);
  }

  *g = *src;
  g->in_use     = 1;
  g->track      = track;
  g->instrument = instrument;
  g->par_layer  = par_layer;
  // Per-track-RNG keystone: a slot loaded from a bank file carries seed==0 (the
  // on-disk format predates the seed field), which would alias every loaded
  // generator onto the single 0xdeadbabe stream. Mint a fresh non-zero seed in
  // that case. A slot restored from a live RAM snapshot (track undo / capture
  // trample) already carries a real non-zero seed, so its wander resumes exactly.
  if( !g->seed )
    g->seed = SEQ_RANDOM_Gen(0) | 1;
  return 0;
}


u8 SEQ_GENERATOR_TrackSnapshot(u8 track, seq_generator_t *buf, u8 max)
{
  if( track >= SEQ_CORE_NUM_TRACKS || buf == NULL ) return 0;
  u8 count = 0;
  u8 instr;
  for(instr=0; instr<SEQ_GENERATOR_INSTRUMENTS && count<max; ++instr) {
    seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
    if( g != NULL )
      buf[count++] = *g;
  }
  return count;
}


s32 SEQ_GENERATOR_TrackRestore(u8 track, const seq_generator_t *buf, u8 count)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  SEQ_GENERATOR_TrackClear(track);
  s32 status = 0;
  u8 i;
  for(i=0; i<count; ++i)
    status |= SEQ_GENERATOR_SlotSet(track, buf[i].instrument, &buf[i]);
  return status;
}


void SEQ_GENERATOR_ForceRewrite(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return;
  u8 i;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    seq_generator_t *g = &pool[i];
    if( g->in_use && g->engaged && g->track == track )
      write_loop_to_source(g);
  }
}


u8 SEQ_GENERATOR_Roll(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0;
  u8 i, n = 0;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    seq_generator_t *g = &pool[i];
    if( !g->in_use || !g->engaged || g->track != track )
      continue;
    roll_loop(g);
    write_loop_to_source(g);
    ++n;
  }
  return n;
}


s32 SEQ_GENERATOR_LockToggle(u8 track, u8 instrument, u8 step)
{
  if( step >= SEQ_GENERATOR_LOOP_LEN ) return -1;
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instrument);
  if( g == NULL ) return -1;
  u8 next = SEQ_GENERATOR_LockGet(g, step) ? 0 : 1;
  SEQ_GENERATOR_LockSet(g, step, next);
  return next;
}


s32 SEQ_GENERATOR_Anchor(u8 track, u8 instrument)
{
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instrument);
  if( g == NULL ) return -1;
  memcpy(g->anchor, g->loop, SEQ_GENERATOR_LOOP_LEN);
  g->anchor_valid = 1;
  return 0;
}


s32 SEQ_GENERATOR_Snap(u8 track, u8 instrument)
{
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instrument);
  if( g == NULL ) return -1;
  if( !g->anchor_valid ) return -2;
  memcpy(g->loop, g->anchor, SEQ_GENERATOR_LOOP_LEN);
  // Rewrite source so the snap is audible immediately, not on the next wrap.
  // SNAP works whether or not the slot is currently engaged — both halves
  // make sense (engaged + snap = pull back to identity during play;
  // disengaged + snap = restore the loop for inspection / re-engage).
  write_loop_to_source(g);
  return 0;
}


s32 SEQ_GENERATOR_MultCycle(u8 track, u8 instrument, u8 step)
{
  if( step >= SEQ_GENERATOR_LOOP_LEN ) return -1;
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instrument);
  if( g == NULL ) return -1;
  u8 cur = SEQ_GENERATOR_MultGet(g, step);
  // Wrap reserved codes (≥ NUM) back to 0 on the next press so a stale
  // pattern load (or future-codes set by something else) recovers cleanly.
  u8 next = (cur + 1) >= SEQ_GENERATOR_MULT_NUM ? 0 : (u8)(cur + 1);
  SEQ_GENERATOR_MultSet(g, step, next);
  return next;
}


s32 SEQ_GENERATOR_ForceMutate(u8 track, u8 instrument)
{
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instrument);
  if( g == NULL ) return -1;
  mutate_loop(g);
  write_loop_to_source(g);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Tick prologue — called from SEQ_CORE_Tick BEFORE SEQ_CORE_RenderTracks.
/////////////////////////////////////////////////////////////////////////////
// CAPTURE re-sim: when set (!= 0xff), only this track's generators auto-mutate.
// The re-sim drives the engine with export_track=src, but round-0 LOOPBACK tracks
// still advance and would otherwise wander (corrupting their pool slots, which the
// re-sim snapshot doesn't cover). Gating mutation to the captured track keeps the
// borrow non-destructive for everything else.
static u8 resim_only_track = 0xff;
void SEQ_GENERATOR_ReSimOnlyTrackSet(u8 track) { resim_only_track = track; }

void SEQ_GENERATOR_Tick(void)
{
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    if( resim_only_track != 0xff && track != resim_only_track )
      continue; // re-sim: only the captured track wanders
    u8 cur = seq_core_trk[track].step;
    u8 prev = last_seen_step[track];
    last_seen_step[track] = cur;

    // Fire on track-wrap to step 0. 0xFF sentinel (post-init) also fires:
    // makes the first measure after start carry generator mutation.
    if( cur != 0 ) continue;
    if( prev == 0 ) continue; // same step as last tick — no wrap event

    // Track just wrapped. Mutate + rewrite every engaged generator on it —
    // UNLESS FREEZE (the master mutation switch) is engaged, in which case the
    // engaged loops hold static (the design's frozen organism; reversible).
    // last_seen_step above still updates so wrap-tracking stays correct, and
    // deliberate ROLL / ForceMutate gestures are NOT gated by FREEZE.
    if( seq_core_state.FREEZE )
      continue;
    // Flag the auto-mutate window so the per-step SEQ_PAR_Set writes mark the
    // group dirty (FEARLESS writeback) WITHOUT marking phrase-drift — ambient
    // wandering is not a deliberate edit (PHRASES drift LED). Cleared right
    // after so a concurrent UI edit isn't misclassified (window is µs/measure).
    seq_generator_in_automutate = 1;
    u8 i;
    for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
      seq_generator_t *g = &pool[i];
      if( !g->in_use || !g->engaged || g->track != track )
        continue;
      mutate_loop(g);
      write_loop_to_source(g);
    }
    seq_generator_in_automutate = 0;
  }
}
