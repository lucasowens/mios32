// $Id$
/*
 * Turing-Machine-style shift register GENERATOR (per-track).
 *
 * One-shot generator (like the Euclidean generator): given a seed + params,
 * simulates a TM-style 16-bit shift register across the track and WRITES
 * the resulting note / velocity / gate values directly into the track's
 * parameter and trigger layers. After running, the track plays normally
 * from those layers and the user can hand-edit individual steps.
 *
 * Inspired by Music Thing Modular's Turing Machine, Tiptop Noisering, and
 * the family of LFSR-based sequence generators:
 *
 *   probability=0   -> bit never flipped     -> LOCKED loop
 *   probability=64  -> bit flipped ~50% time -> CHAOS
 *   probability=127 -> bit always flipped    -> INVERSE-LOCKED loop
 *
 * tm_length controls how many bits of the register loop; shorter = tighter
 * rhythmic period that drifts against the track length.
 *
 * The seed is `tcc->tm_register` so the simulation is fully deterministic
 * from a single u16. Repeated Generate calls with the same params give
 * the same output. `tm_random` / `tm_flip` lets the user explore the seed
 * space without losing repeatability.
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
#include "tasks.h"

#include "seq_turing.h"
#include "seq_core.h"
#include "seq_cc.h"
#include "seq_layer.h"
#include "seq_par.h"
#include "seq_trg.h"
#include "seq_random.h"


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_TURING_Init(u32 mode)
{
  u8 t;
  for(t=0; t<SEQ_CORE_NUM_TRACKS; ++t) {
    // seed each register with a different non-zero default so they don't
    // start identical across tracks (xorshift hates zero)
    seq_core_trk[t].tm_register = 0xACE0u + t;
  }
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Deterministic register evolution: shift left by 1 within the loop window,
// probabilistically flip the recycled top bit using a deterministic
// xorshift32 seeded from the initial register value.
/////////////////////////////////////////////////////////////////////////////
static inline u16 turing_evolve(u16 reg, u8 length, u8 prob_127, u32 *flip_rng)
{
  if( length < 1 ) length = 1;
  if( length > 16 ) length = 16;
  u16 loop_mask = (length >= 16) ? 0xFFFF : ((1u << length) - 1);

  // probabilistic flip via deterministic xorshift32
  u32 r = *flip_rng;
  r ^= r << 13;
  r ^= r >> 17;
  r ^= r << 5;
  *flip_rng = r;

  u8 top = (reg >> (length - 1)) & 1;
  if( (r & 0x7f) < prob_127 )
    top ^= 1;

  u16 looped = ((reg << 1) & loop_mask) | top;
  return (reg & ~loop_mask) | looped;
}


/////////////////////////////////////////////////////////////////////////////
// One-shot Generate: simulates the TM across every step of the track and
// writes outputs into the layers. After this returns, the track plays
// normally and the user can edit any step by hand.
//
// Output mapping (read from the register at each step):
//   - Note:     bits 0..6   -> [tm_note_base .. tm_note_base+tm_note_range]
//   - Velocity: bits 7..13  -> 0..127 (min 1 to keep note alive)
//   - Gate:     bit  14     -> 1 = gate on, 0 = gate off
//
// Each output target is written only if its enable bit is set in tm_mode.
// CC output writes into a CC parameter layer if any par layer is currently
// assigned to CC (looks for SEQ_PAR_Type_CC).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_TURING_Generate(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( !tcc->tm_mode )
    return -2; // nothing enabled to write

  u8 event_mode = tcc->event_mode;
  u8 instrument = 0;
  (void)event_mode; // drum-mode handling deferred

  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 length = tcc->tm_length;
  u8 prob = tcc->tm_probability;
  u8 note_base = tcc->tm_note_base;
  u8 note_range = tcc->tm_note_range;

  // simulate from a LOCAL copy so the persistent register (the seed) is
  // untouched - same seed + params always produces the same output
  u16 reg = seq_core_trk[track].tm_register;
  if( reg == 0 ) reg = 0xACE0;
  u32 flip_rng = ((u32)reg << 16) | (u32)reg | 1; // never zero

  // resolve target par layers. If link_par_layer_* is -1 (no par layer of the
  // appropriate type assigned to the track), fall back to par layer 0 so the
  // user sees content land somewhere - they can re-route after via TRGASG.
  s8 note_layer = (tcc->link_par_layer_note >= 0) ? tcc->link_par_layer_note : 0;
  s8 vel_layer  = (tcc->link_par_layer_velocity >= 0) ? tcc->link_par_layer_velocity : -1;
  // velocity falls back to -1 (skip) instead of 0 because 0 would clobber
  // the note layer we just wrote to

  portENTER_CRITICAL();

  u16 step;
  for(step=0; step<num_steps; ++step) {
    // evolve register first so step 0's output is from the first shift
    // (matches classic TM "clock pulse causes shift, then output reads")
    reg = turing_evolve(reg, length, prob, &flip_rng);

    // note output
    if( (tcc->tm_mode & SEQ_TM_OUT_NOTE) && note_layer >= 0 ) {
      u8 raw = reg & 0x7f;
      u8 note = (note_range == 0) ? note_base
                : note_base + ((u16)raw * (u16)note_range) / 128;
      if( note > 127 ) note = 127;
      SEQ_PAR_Set(track, step, note_layer, instrument, note);
    }

    // velocity output
    if( (tcc->tm_mode & SEQ_TM_OUT_VEL) && vel_layer >= 0 ) {
      u8 v = (reg >> 7) & 0x7f;
      if( v < 1 ) v = 1;
      SEQ_PAR_Set(track, step, vel_layer, instrument, v);
    }

    // gate trigger output - always works regardless of par layer linkage
    if( tcc->tm_mode & SEQ_TM_OUT_GATE ) {
      u8 g = (reg >> 14) & 1;
      SEQ_TRG_GateSet(track, step, instrument, g);
    }
  }

  portEXIT_CRITICAL();

  SEQ_CORE_CancelSustainedNotes(track);

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// External helpers
/////////////////////////////////////////////////////////////////////////////
u16 SEQ_TURING_RegisterGet(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return 0;
  return seq_core_trk[track].tm_register;
}

s32 SEQ_TURING_RegisterRandomize(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  u32 r = SEQ_RANDOM_Gen(0);
  seq_core_trk[track].tm_register = (u16)(r & 0xFFFF);
  if( seq_core_trk[track].tm_register == 0 )
    seq_core_trk[track].tm_register = 0xACE0;
  return 0;
}

s32 SEQ_TURING_BitFlip(u8 track, u8 bit_idx)
{
  if( track >= SEQ_CORE_NUM_TRACKS ) return -1;
  if( bit_idx >= 16 ) return -2;
  seq_core_trk[track].tm_register ^= (1u << bit_idx);
  return 0;
}
