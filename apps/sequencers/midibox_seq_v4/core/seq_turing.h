// $Id$
/*
 * Header file for the per-track Turing-Machine-style shift register generator.
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 *
 * ==========================================================================
 */

#ifndef _SEQ_TURING_H
#define _SEQ_TURING_H

#include "seq_layer.h"

/////////////////////////////////////////////////////////////////////////////
// Output enable bits packed into tcc->tm_mode
/////////////////////////////////////////////////////////////////////////////
#define SEQ_TM_OUT_NOTE  (1 << 0)
#define SEQ_TM_OUT_VEL   (1 << 1)
#define SEQ_TM_OUT_GATE  (1 << 2)
#define SEQ_TM_OUT_CC    (1 << 3)


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_TURING_Init(u32 mode);

// One-shot generator (Euclidean-style): simulates the TM register across
// every step of the track and writes resulting note / velocity / gate values
// into the parameter and trigger layers. After this returns, the track plays
// normally from its layers - user can edit individual steps by hand.
// Returns -2 if tm_mode is 0 (no output target enabled).
extern s32 SEQ_TURING_Generate(u8 track);

// Seed manipulation - the persistent tm_register IS the deterministic seed.
extern u16 SEQ_TURING_RegisterGet(u8 track);
extern s32 SEQ_TURING_RegisterRandomize(u8 track);
extern s32 SEQ_TURING_BitFlip(u8 track, u8 bit_idx);

#endif /* _SEQ_TURING_H */
