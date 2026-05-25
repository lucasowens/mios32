// $Id$
/*
 * Phase E — generator workflow.
 * Phase G — §8 step 6 polish (per-step LOCK, ROLL, depth, contour).
 * See seq_generator.h for the model. This file implements:
 *   - the cap-64 pool + per-(track, instrument) sparse lookup
 *   - one-deep global auto-undo
 *   - per-measure-boundary mutate-then-rewrite (Turing loop → source par)
 *   - phase G: lock-aware mutation, depth-controlled perturb vs reroll,
 *     contour-biased reroll distribution, on-demand ROLL gesture
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
#include "seq_layer.h"
#include "seq_random.h"


#ifndef CCM_SECTION
#define CCM_SECTION
#endif


/////////////////////////////////////////////////////////////////////////////
// Local state — CCMRAM
/////////////////////////////////////////////////////////////////////////////

static seq_generator_t CCM_SECTION pool[SEQ_GENERATOR_POOL_SIZE];

// Sparse map: pool index per (track, instrument), 0xFF = unallocated.
static u8 CCM_SECTION pool_index[SEQ_CORE_NUM_TRACKS][SEQ_GENERATOR_INSTRUMENTS];

// One-deep global auto-undo: snapshot of the entire par buffer for whichever
// track most recently saw a first-time ENGAGE. Restored by SEQ_GENERATOR_Undo.
// (CCM_SECTION trails the variable name — placed between type and name it
// applies to the struct *type*, which GCC ignores; placed after the name it
// applies to the variable, which is what we want.)
typedef struct {
  u8 valid;
  u8 track;
  u8 par_snapshot[SEQ_PAR_MAX_BYTES];
} seq_generator_undo_t;

static seq_generator_undo_t CCM_SECTION undo_slot;


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
static u8 reroll_pitch(u8 lo, u8 hi, u8 contour)
{
  normalize_range(&lo, &hi);
  u32 span = (u32)(hi - lo) + 1;  // ≥1

  switch( contour ) {
    case SEQ_GENERATOR_CONTOUR_LOW_BIAS: {
      // min of two uniforms ⇒ density falls toward hi — concentrates near lo.
      u32 a = SEQ_RANDOM_Gen_Range(0, span - 1);
      u32 b = SEQ_RANDOM_Gen_Range(0, span - 1);
      return (u8)(lo + (a < b ? a : b));
    }
    case SEQ_GENERATOR_CONTOUR_HIGH_BIAS: {
      u32 a = SEQ_RANDOM_Gen_Range(0, span - 1);
      u32 b = SEQ_RANDOM_Gen_Range(0, span - 1);
      return (u8)(lo + (a > b ? a : b));
    }
    case SEQ_GENERATOR_CONTOUR_TRIANGLE: {
      // sum of two uniforms over half-span ⇒ triangular, peak at mid-range.
      u32 half = (span + 1) / 2;
      u32 a = SEQ_RANDOM_Gen_Range(0, half - 1);
      u32 b = SEQ_RANDOM_Gen_Range(0, half - 1);
      u32 v = a + b;
      if( v >= span ) v = span - 1;
      return (u8)(lo + v);
    }
    default: // SEQ_GENERATOR_CONTOUR_UNIFORM
      return (u8)SEQ_RANDOM_Gen_Range(lo, hi);
  }
}


// Perturb an existing value by ±depth semitones, clamped to [lo..hi].
// depth==0 returns existing unchanged; the caller is responsible for not
// calling perturb_pitch when depth==0 (early-out keeps the hot path clean).
static u8 perturb_pitch(u8 existing, u8 lo, u8 hi, u8 depth)
{
  normalize_range(&lo, &hi);
  if( existing < lo ) existing = lo;
  if( existing > hi ) existing = hi;

  // Symmetric ±depth window; SEQ_RANDOM_Gen_Range with 2*depth+1 buckets.
  u32 d = depth;
  u32 bucket = SEQ_RANDOM_Gen_Range(0, 2*d);
  s32 delta = (s32)bucket - (s32)d;
  s32 v = (s32)existing + delta;
  if( v < (s32)lo ) v = lo;
  if( v > (s32)hi ) v = hi;
  return (u8)v;
}


// Mutate the loop in place. Phase G upgrades phase E's pure-reroll path:
//   - locked steps are never touched (LOCK survives any mutation)
//   - rate gates "is this step touched at all?" (per-step probability)
//   - depth selects perturb-vs-reroll: 0 = no-op, 127 = full reroll
//     (phase E behavior), in between = perturb by ±depth around existing
//   - contour biases the reroll path only
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

    // SEQ_RANDOM_Gen_Range(0, 254) gives a uniform u8 in [0,254]; comparing
    // against rate*2 maps rate 0..127 → ~0..100% touch probability per cell.
    u32 r = SEQ_RANDOM_Gen_Range(0, 254);
    if( r >= (u32)(g->mutation_rate * 2) )
      continue;

    if( g->mutation_depth >= 127 ) {
      g->loop[i] = reroll_pitch(g->range_min, g->range_max, g->contour_shape);
    } else {
      g->loop[i] = perturb_pitch(g->loop[i], g->range_min, g->range_max,
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
    g->loop[i] = reroll_pitch(g->range_min, g->range_max, g->contour_shape);
  }
}


// Transcribe loop[] → Note par-layer of the *target* (track, instrument)
// drum slot. Tiles when num_p_steps > LOOP_LEN. Sets render-dirty on the
// target track. Returns 0 on success, -1 if the target isn't drum-mode /
// missing a Note par-layer (or has zero steps).
static s32 write_loop_to(const seq_generator_t *g, u8 dst_track, u8 dst_instr)
{
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( dst_instr >= SEQ_GENERATOR_INSTRUMENTS ) return -1;

  seq_cc_trk_t *tcc = &seq_cc_trk[dst_track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum ) return -1;
  if( tcc->link_par_layer_note < 0 ) return -1;

  u8  par_layer = (u8)tcc->link_par_layer_note;
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


// Convenience wrapper: write to the gen's own (track, instrument) — the
// engage / measure-boundary path. Failures here are by construction the
// "track misconfigured" case which the engage path already screens for.
static void write_loop_to_source(const seq_generator_t *g)
{
  (void)write_loop_to(g, g->track, g->instrument);
}


// Seed loop[] with an initial reroll across the range (called once at first
// ENGAGE so the engaged generator is immediately audible without waiting for
// the next measure). Honors contour; locks aren't relevant — fresh slot.
static void seed_loop(seq_generator_t *g)
{
  u8 i;
  for(i=0; i<SEQ_GENERATOR_LOOP_LEN; ++i)
    g->loop[i] = reroll_pitch(g->range_min, g->range_max, g->contour_shape);
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
  memset(&undo_slot, 0, sizeof(undo_slot));
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


seq_generator_t *SEQ_GENERATOR_Get(u8 track, u8 instrument)
{
  if( track >= SEQ_CORE_NUM_TRACKS || instrument >= SEQ_GENERATOR_INSTRUMENTS )
    return NULL;
  u8 ix = pool_index[track][instrument];
  if( ix == 0xFF || ix >= SEQ_GENERATOR_POOL_SIZE ) return NULL;
  if( !pool[ix].in_use ) return NULL;
  return &pool[ix];
}


s32 SEQ_GENERATOR_Engage(u8 track, u8 instrument)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -2;
  if( instrument >= SEQ_GENERATOR_INSTRUMENTS ) return -2;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum ) return -2;
  if( tcc->link_par_layer_note < 0 ) return -3;

  u8 ix = pool_index[track][instrument];
  if( ix != 0xFF && ix < SEQ_GENERATOR_POOL_SIZE && pool[ix].in_use ) {
    // Re-engage: keep loop content; flip engaged on; force a rewrite so the
    // current (possibly UNDO-restored or paused-out-of-sync) source matches.
    pool[ix].engaged = 1;
    write_loop_to_source(&pool[ix]);
    return 0;
  }

  seq_generator_t *g = alloc_slot();
  if( g == NULL ) return -1;

  g->track          = track;
  g->instrument     = instrument;
  g->range_min      = SEQ_GENERATOR_DEFAULT_RANGE_MIN;
  g->range_max      = SEQ_GENERATOR_DEFAULT_RANGE_MAX;
  g->mutation_rate  = SEQ_GENERATOR_DEFAULT_RATE;
  g->mutation_depth = SEQ_GENERATOR_DEFAULT_DEPTH;
  g->contour_shape  = SEQ_GENERATOR_DEFAULT_CONTOUR;
  g->engaged        = 1;
  g->in_use         = 1;
  // alloc_slot returned a memset-clean slot, so locks[] is already 0; no
  // explicit clear needed (slots are also memset-cleared at free).
  seed_loop(g);

  pool_index[track][instrument] = (u8)(g - pool);

  // Snapshot the track's par-buffer for one-deep auto-undo BEFORE we write.
  undo_slot.track = track;
  memcpy(undo_slot.par_snapshot, seq_par_layer_value[track], SEQ_PAR_MAX_BYTES);
  undo_slot.valid = 1;

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


s32 SEQ_GENERATOR_BounceRelocate(u8 src_track, u8 src_instr,
                                 u8 dst_track, u8 dst_instr)
{
  if( src_track >= SEQ_CORE_NUM_TRACKS ) return -2;
  if( src_instr >= SEQ_GENERATOR_INSTRUMENTS ) return -2;
  if( dst_track >= SEQ_CORE_NUM_TRACKS ) return -2;
  if( dst_instr >= SEQ_GENERATOR_INSTRUMENTS ) return -2;

  u8 ix = pool_index[src_track][src_instr];
  if( ix == 0xFF || ix >= SEQ_GENERATOR_POOL_SIZE ) return -1;
  if( !pool[ix].engaged ) return -1;

  // The §3 destination semantic with whole-track surgical undo: relocate is
  // only safe when the global undo slot still describes the gen's track.
  // Otherwise we'd restore stale state on src; refuse instead and let the
  // user UNDO+ENGAGE again.
  if( !undo_slot.valid || undo_slot.track != src_track )
    return -3;

  // Order is critical: restore src par-buffer FIRST (whole-track from undo),
  // THEN transcribe the gen's loop into dst. If dst lives on the same track
  // as src the restore-then-write order means dst lands with the loop after
  // the original drum content is back in place; if dst lives on a different
  // track entirely, the order is academic but harmless.
  seq_generator_t *g = &pool[ix];

  // Write target validation before mutating anything (so a bad dst doesn't
  // leave src half-restored).
  {
    seq_cc_trk_t *dtcc = &seq_cc_trk[dst_track];
    if( dtcc->event_mode != SEQ_EVENT_MODE_Drum ) return -2;
    if( dtcc->link_par_layer_note < 0 ) return -2;
    s32 nps = SEQ_PAR_NumStepsGet(dst_track);
    if( nps <= 0 ) return -2;
  }

  memcpy(seq_par_layer_value[src_track], undo_slot.par_snapshot, SEQ_PAR_MAX_BYTES);
  seq_render_dirty[src_track] = 1;

  // Transcribe to dst. If dst==src track, this writes after the restore so
  // dst_instr ends up holding the bounced loop (the rest of src track is
  // back to pre-engage).
  if( write_loop_to(g, dst_track, dst_instr) < 0 ) {
    // Validation passed above, so this should not fire — but if it does,
    // src has already been restored and we'd best not leave the gen in a
    // weird state. Free the slot and undo, report success-ish: src is
    // restored, dst write failed silently.
    memset(&pool[ix], 0, sizeof(pool[ix]));
    pool_index[src_track][src_instr] = 0xFF;
    undo_slot.valid = 0;
    return -2;
  }

  // Disengage every gen on src track (the whole-track restore wiped out the
  // material their loops were transcribing into; the relocated gen is freed
  // entirely; other gens would otherwise immediately re-transcribe stale
  // loop contents into the restored buffer on the next measure boundary).
  // Trade-off documented in the header: this is the "live-safety not
  // transactional history" property — single-snapshot undo can't preserve
  // independent gens on the same track. Workflow: bounce-relocate is a
  // one-gen-per-track gesture.
  u8 i;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    if( pool[i].in_use && pool[i].track == src_track )
      pool[i].engaged = 0;
  }

  // Free the relocated gen's slot.
  memset(&pool[ix], 0, sizeof(pool[ix]));
  pool_index[src_track][src_instr] = 0xFF;
  undo_slot.valid = 0;

  if( dst_track != src_track )
    seq_render_dirty[dst_track] = 1;
  return 0;
}


s32 SEQ_GENERATOR_FindEngagedOnTrack(u8 track, u8 *out_instr)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0;
  u8 i;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    if( pool[i].in_use && pool[i].engaged && pool[i].track == track ) {
      if( out_instr ) *out_instr = pool[i].instrument;
      return 1;
    }
  }
  return 0;
}


// Phase F.3 ENGAGE auto-jump support. "Empty drum slot" = no engaged gen
// AND every step of the drum's Note par-layer is 0. Scans instruments in
// ascending order; the §3 "first empty legal layer" rule. Returns 1 +
// writes out_instr if found, 0 otherwise. Caller is responsible for the
// track-mode + Note-layer gating (a non-drum track or a track without a
// Note par-layer assignment trivially has no legal empty drum — returns 0).
s32 SEQ_GENERATOR_FindFirstEmptyDrum(u8 track, u8 *out_instr)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum ) return 0;
  if( tcc->link_par_layer_note < 0 ) return 0;

  u8  par_layer    = (u8)tcc->link_par_layer_note;
  s32 num_p_steps_s = SEQ_PAR_NumStepsGet(track);
  if( num_p_steps_s <= 0 ) return 0;
  u16 num_p_steps  = (u16)num_p_steps_s;

  u8 instr;
  for(instr=0; instr<SEQ_GENERATOR_INSTRUMENTS; ++instr) {
    if( SEQ_GENERATOR_IsEngaged(track, instr) ) continue;
    u16 step;
    u8 empty = 1;
    for(step=0; step<num_p_steps; ++step) {
      if( SEQ_PAR_Get(track, step, par_layer, instr) != 0 ) { empty = 0; break; }
    }
    if( empty ) {
      if( out_instr ) *out_instr = instr;
      return 1;
    }
  }
  return 0;
}


s32 SEQ_GENERATOR_Undo(void)
{
  if( !undo_slot.valid ) return -1;

  u8 track = undo_slot.track;
  memcpy(seq_par_layer_value[track], undo_slot.par_snapshot, SEQ_PAR_MAX_BYTES);
  seq_render_dirty[track] = 1;

  // Disengage every generator on this track so the restored source isn't
  // overwritten on the next measure.
  u8 i;
  for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
    if( pool[i].in_use && pool[i].track == track )
      pool[i].engaged = 0;
  }

  undo_slot.valid = 0;
  return 0;
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


/////////////////////////////////////////////////////////////////////////////
// Tick prologue — called from SEQ_CORE_Tick BEFORE SEQ_CORE_RenderTracks.
/////////////////////////////////////////////////////////////////////////////
void SEQ_GENERATOR_Tick(void)
{
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    u8 cur = seq_core_trk[track].step;
    u8 prev = last_seen_step[track];
    last_seen_step[track] = cur;

    // Fire on track-wrap to step 0. 0xFF sentinel (post-init) also fires:
    // makes the first measure after start carry generator mutation.
    if( cur != 0 ) continue;
    if( prev == 0 ) continue; // same step as last tick — no wrap event

    // Track just wrapped. Mutate + rewrite every engaged generator on it.
    u8 i;
    for(i=0; i<SEQ_GENERATOR_POOL_SIZE; ++i) {
      seq_generator_t *g = &pool[i];
      if( !g->in_use || !g->engaged || g->track != track )
        continue;
      mutate_loop(g);
      write_loop_to_source(g);
    }
  }
}
