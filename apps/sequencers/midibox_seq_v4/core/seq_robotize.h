// $Id$
/*
 * Header file for robotize routines
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_ROBOTIZE_H
#define _SEQ_ROBOTIZE_H

#include "seq_layer.h"

/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////
	


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////

typedef union {
  u8 ALL;
  struct {
    u8 SUSTAIN:1;
    u8 NOFX:1;
    u8 ECHO:1;
    u8 DUPLICATE:1;
  };
} seq_robotize_flags_t;

/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_ROBOTIZE_Init(u32 mode);

extern seq_robotize_flags_t SEQ_ROBOTIZE_Event(u8 track, u8 step, seq_layer_evnt_t *e);

// Fill every bar_anchor with an independent fresh random. Each bar of the
// loop becomes its own self-contained variation (scattered, free).
extern s32 SEQ_ROBOTIZE_Reseed(u8 track);

// Replace a single bar anchor (slot 0..15) with a fresh random while leaving
// the others locked. The molding tool: sculpt the loop one bar at a time.
extern s32 SEQ_ROBOTIZE_RerollBar(u8 track, u8 bar_idx);

// Tag the K bars ending at the current bar as a loop, populating
// bar_anchors[0..K-1] from the last K snapshots (preserves musical continuity).
// With K=0 the track's existing robotize_loop_cycles is used.
// Jump-immediate variant rewinds state now; quantized variant defers to the
// next bar boundary for a seamless transition.
extern s32 SEQ_ROBOTIZE_Freeze(u8 track, u8 K);
extern s32 SEQ_ROBOTIZE_FreezeQuantized(u8 track, u8 K);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////


#endif /* _SEQ_ROBOTIZE_H */
