// $Id$
/*
 * Phase E — generator workflow.
 * See seq_generator.h for the model. This file implements:
 *   - the cap-64 pool + per-(track, instrument) sparse lookup
 *   - one-deep global auto-undo
 *   - per-measure-boundary mutate-then-rewrite (Turing loop → source par)
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

static u8 reroll_pitch(u8 lo, u8 hi)
{
  if( lo > hi ) {
    u8 t = lo;
    lo = hi;
    hi = t;
  }
  if( lo == 0 )
    lo = 1; // 0 is the drum lay_const fallback marker — never roll into it
  if( hi > 127 ) hi = 127;
  if( lo > hi ) lo = hi;
  return (u8)SEQ_RANDOM_Gen_Range(lo, hi);
}


// Mutate the loop in place: each cell with prob (rate/255) gets rerolled.
static void mutate_loop(seq_generator_t *g)
{
  if( g->mutation_rate == 0 )
    return; // pass-through (§2.3 sweep contract)

  u8 i;
  for(i=0; i<SEQ_GENERATOR_LOOP_LEN; ++i) {
    // SEQ_RANDOM_Gen_Range(0, 254) gives a uniform u8 in [0,254]; comparing
    // against rate*2 maps rate 0..127 → ~0..100% reroll probability per cell.
    u32 r = SEQ_RANDOM_Gen_Range(0, 254);
    if( r < (u32)(g->mutation_rate * 2) ) {
      g->loop[i] = reroll_pitch(g->range_min, g->range_max);
    }
  }
}


// Transcribe loop[] → source par-layer for the generator's (track, instr,
// note-layer) target. Tiles when num_p_steps > LOOP_LEN.
static void write_loop_to_source(const seq_generator_t *g)
{
  u8 track = g->track;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  if( tcc->event_mode != SEQ_EVENT_MODE_Drum ) return;
  if( tcc->link_par_layer_note < 0 ) return;

  u8  par_layer  = (u8)tcc->link_par_layer_note;
  s32 num_p_steps_s = SEQ_PAR_NumStepsGet(track);
  if( num_p_steps_s <= 0 ) return;
  u16 num_p_steps = (u16)num_p_steps_s;

  u16 step;
  for(step=0; step<num_p_steps; ++step) {
    u8 v = g->loop[step & (SEQ_GENERATOR_LOOP_LEN - 1)];
    SEQ_PAR_Set(track, step, par_layer, g->instrument, v);
  }

  seq_render_dirty[track] = 1;
}


// Seed loop[] with an initial reroll across the range (called once at first
// ENGAGE so the engaged generator is immediately audible without waiting for
// the next measure).
static void seed_loop(seq_generator_t *g)
{
  u8 i;
  for(i=0; i<SEQ_GENERATOR_LOOP_LEN; ++i)
    g->loop[i] = reroll_pitch(g->range_min, g->range_max);
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

  g->track         = track;
  g->instrument    = instrument;
  g->range_min     = SEQ_GENERATOR_DEFAULT_RANGE_MIN;
  g->range_max     = SEQ_GENERATOR_DEFAULT_RANGE_MAX;
  g->mutation_rate = SEQ_GENERATOR_DEFAULT_RATE;
  g->engaged       = 1;
  g->in_use        = 1;
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
