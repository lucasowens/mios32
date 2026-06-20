// $Id$
/*
 * Header file for random generator
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_RANDOM_H
#define _SEQ_RANDOM_H


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern u32 SEQ_RANDOM_Gen(u32 seed);
extern u32 SEQ_RANDOM_Gen_Range(u32 min, u32 max);

// per-state xorshift32 — caller supplies the PRNG state pointer.
// cheaper than the global MT and lets independent users (e.g. one per track)
// keep a deterministic, seekable stream they can restore to an anchor.
extern u32 SEQ_RANDOM_GenXorshift(u32 *state);
extern u32 SEQ_RANDOM_GenRangeXorshift(u32 *state, u32 min, u32 max);

// Global-RNG (jsw MT + cached value) save/restore for retroactive CAPTURE
// non-destructiveness. buf must hold SEQ_RANDOM_STATE_WORDS u32.
#include <jsw_rand.h>
#define SEQ_RANDOM_STATE_WORDS (1 + JSW_RAND_STATE_WORDS)
extern void SEQ_RANDOM_StateGet(u32 *buf);
extern void SEQ_RANDOM_StateSet(const u32 *buf);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////

#endif /* _SEQ_RANDOM_H */
