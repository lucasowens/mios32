// $Id$
/*
 * New Robotize Functions by Borfo (Rob K.)
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

#include "seq_robotize.h"
#include "seq_core.h"
#include "seq_cc.h"
#include "seq_layer.h"
#include "seq_trg.h"
#include "seq_random.h"
#include "seq_scale.h"


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_ROBOTIZE_Init(u32 mode)
{
  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Internal: draw a fresh non-zero u32 from the global PRNG.
/////////////////////////////////////////////////////////////////////////////
static u32 SEQ_ROBOTIZE_FreshAnchor(void)
{
  u32 v = SEQ_RANDOM_Gen(0);
  if( !v )
    v = 0xdeadbabe; // xorshift32 can't accept zero
  return v;
}


/////////////////////////////////////////////////////////////////////////////
// Reseed: fill the active palette (palette_length slots) with INDEPENDENT
// random values. Slots outside the active palette are left alone, so
// shrinking palette_length and growing again reveals previous content.
// Note: this is semantically different from freeze - freeze preserves the
// natural PRNG continuity across the captured window (musical, related bars),
// reseed gives independent random per-bar content (scattered, free variety).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_ROBOTIZE_Reseed(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  u8 palette = tcc->robotize_palette_length;
  if( palette == 0 || palette > 16 ) palette = 16;

  u8 i;
  for(i=0; i<palette; ++i)
    tcc->robotize_bar_anchors[i] = SEQ_ROBOTIZE_FreshAnchor();

  // immediate effect: rewind working state to the loop's current head
  u8 head_idx = (tcc->robotize_loop_start + tcc->robotize_loop_rotate) % palette;
  seq_core_trk[track].robotize_seed_state = tcc->robotize_bar_anchors[head_idx];
  seq_core_trk[track].robotize_loop_phase = 0;
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// RerollBar: replace a single bar's anchor with a fresh random. Other bars
// remain locked. This is the per-bar molding tool.
// bar_idx is 0..15 (slot in the anchor array, not a song bar).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_ROBOTIZE_RerollBar(u8 track, u8 bar_idx)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;
  if( bar_idx >= 16 )
    return -2;

  seq_cc_trk[track].robotize_bar_anchors[bar_idx] = SEQ_ROBOTIZE_FreshAnchor();

  // if we just modified the bar that's currently playing (phase == bar_idx
  // and loop is active), rewind state to the new anchor so the change is
  // audible immediately rather than next time that bar comes round
  seq_core_trk_t *t = &seq_core_trk[track];
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->robotize_loop_cycles && t->robotize_loop_phase == bar_idx )
    t->robotize_seed_state = tcc->robotize_bar_anchors[bar_idx];

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Internal helper: copy K snapshots ending at the current musical measure
// into bar_anchors[start..start+K-1] (wrapping mod palette_length). Other
// slots are left untouched.
/////////////////////////////////////////////////////////////////////////////
static void SEQ_ROBOTIZE_PopulateAnchorsFromWindow(u8 track, u8 K)
{
  seq_core_trk_t *t = &seq_core_trk[track];
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  u8 palette = tcc->robotize_palette_length;
  if( palette == 0 || palette > 16 ) palette = 16;
  u8 start = tcc->robotize_loop_start;

  // window covers measures [measure_ctr - K + 1 .. measure_ctr]
  u32 base = t->robotize_measure_ctr;
  u8 i;
  for(i=0; i<K; ++i) {
    u8 src = (base + 16 - K + 1 + i) & 0x0f;
    u32 v = t->robotize_seed_snapshots[src];
    if( !v )
      v = 0xdeadbabe;
    u8 dest = (start + i) % palette;
    tcc->robotize_bar_anchors[dest] = v;
  }
}


/////////////////////////////////////////////////////////////////////////////
// Freeze (jump-immediate): tag the K bars ending at the current bar as the
// loop. The K consecutive snapshots are copied into bar_anchors starting at
// `loop_start` (preserving natural PRNG continuity across the captured
// window), playback jumps to the new head immediately, and the loop begins.
// If K is 0 the track's existing loop_cycles is used.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_ROBOTIZE_Freeze(u8 track, u8 K)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_core_trk_t *t = &seq_core_trk[track];

  if( K == 0 ) K = tcc->robotize_loop_cycles;
  if( K == 0 ) return -2;
  if( K > 16 ) K = 16;

  SEQ_ROBOTIZE_PopulateAnchorsFromWindow(track, K);
  tcc->robotize_loop_cycles = K;

  // jump now: state rewinds immediately to the loop head (start + rotate)
  u8 palette = tcc->robotize_palette_length;
  if( palette == 0 || palette > 16 ) palette = 16;
  u8 head_idx = (tcc->robotize_loop_start + tcc->robotize_loop_rotate) % palette;
  t->robotize_seed_state = tcc->robotize_bar_anchors[head_idx];
  t->robotize_loop_phase = 0;
  t->robotize_pending_resync = 0;
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Freeze (quantized): like Freeze, but defer the state restore to the next
// bar boundary so the current bar finishes naturally. Musically seamless.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_ROBOTIZE_FreezeQuantized(u8 track, u8 K)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_core_trk_t *t = &seq_core_trk[track];

  if( K == 0 ) K = tcc->robotize_loop_cycles;
  if( K == 0 ) return -2;
  if( K > 16 ) K = 16;

  SEQ_ROBOTIZE_PopulateAnchorsFromWindow(track, K);
  tcc->robotize_loop_cycles = K;

  // defer: next ++t->bar handler does the restore at phase 0
  t->robotize_pending_resync = 1;
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// checks robotizing probabilities - overall probability is multiplied with individual probability
// returns true if the item should be robotized, or false otherwise
// PRNG state is supplied by caller (per-track xorshift32) so robotize can
// have a deterministic, restorable stream independent of humanize/LFO.
/////////////////////////////////////////////////////////////////////////////
u8 SEQ_ROBOTIZE_Check_Probabilities(u32 *seed_state, u8 prob1, u8 prob2, u32 randoms[], u8 *big_randoms_used)
{
	u8 returnbit = 0;
	if( prob1 && prob2 ) { //robotize event is possible
 	  u8 randindex = (*big_randoms_used) / 3;
	  if ((u8)( (*big_randoms_used) % 3 ) == 0){
		  randoms[randindex] = SEQ_RANDOM_GenXorshift(seed_state); // get fresh random number if needed
	  }

	  u16 testvar = ( prob1 + 1) * ( prob2 + 1); // multiply the two probabilities together to get 10 bit probability number
	  u16 big_random = ( (randoms[randindex]) >> ( ( (*big_randoms_used) % 3 ) * 10 ) ) & ( ( 1 << 10 ) - 1 ) ; // extract a new big random number from the longer 32 bit random number
	  *big_randoms_used += 1; // increment counter
	  if ( big_random <= testvar ) returnbit = 1; // we're a go for robotizing!
	}
	return returnbit;
}


/////////////////////////////////////////////////////////////////////////////
// modifies a MIDI event depending on selected robotize parameters
/////////////////////////////////////////////////////////////////////////////
seq_robotize_flags_t SEQ_ROBOTIZE_Event(u8 track, u8 step, seq_layer_evnt_t *e)
{
  seq_robotize_flags_t returnflags;
  returnflags.ALL = 0;//NOFX, +Echo, +Sustain and +duplicate flags
  
  
  
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  //exit if there's no chance of robotizing anything
  if( ! tcc->robotize_active || tcc->robotize_probability == 0 ) // other checks - not necessary in most cases || ( !tcc->robotize_vel && !tcc->robotize_len && !tcc->robotize_note && !tcc->robotize_oct && !tcc->robotize_skip_probability && !tcc->robotize_sustain_probability && !tcc->robotize_nofx_probability && !tcc->robotize_echo_probability && !tcc->robotize_duplicate_probability ) )
    return returnflags; // nothing to do

  // mask gates which of the 16 steps-per-bar are eligible to robotize.
  // direct field read; SEQ_CC_Get() walks a switch and is wasteful in the hot path.
  u16 robotize_mask = ((u16)tcc->robotize_mask2 << 8) | tcc->robotize_mask1;
  if( !(robotize_mask & (1 << (step & 0x0f))) )
    return returnflags;

  // per-track PRNG state: lets robotize be deterministic / restorable per loop
  u32 *seed_state = &seq_core_trk[track].robotize_seed_state;

//initialize random number array, counter, and range variables
u32 randoms[3] = {0} ; //array of 32 bit random numbers.  Only fills if and as needed, to minimize calls to seq_random_gen.  Use array rather than overwriting the same var, so that the old rands are kept in case we want to reuse them in future improvements to the robotizer
u8 big_randoms_used = 0; //keeps track of how many 10 bit random numbers we've pulled so far
u8 range = 0; //range - reused in several of the robotizers

/*
if ( tcc->robotize_X && tcc->robotize_X_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_X_probability, randoms, &big_randoms_used) ) {
//ROBOTIZING CODE GOES HERE
}

//debug with
MIOS32_MIDI_SendDebugMessage("random: %d ", randoms[0]);
* MIOS32_MIDI_SendDebugString("string")
*/

	//NOTE ROBOTIZER
	// when FORCE_SCALE is on, shift by scale degrees instead of semitones so
	// the param means "wander N steps within the scale." Halve the random
	// range in scale mode (use note as range, not 2*note) so 0..15 maps to
	// roughly +/-0..7 scale degrees uniformly - avoids clamp-induced
	// clustering at +/-7. The octave param stays responsible for bigger jumps.
	if ( tcc->robotize_note && tcc->robotize_note_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_note_probability, randoms, &big_randoms_used) ) {
		if( tcc->trkmode_flags.FORCE_SCALE ) {
			u8 srange = tcc->robotize_note; // half of the chromatic 2*note
			s8 delta = (s8)(srange/2) - (s8)SEQ_RANDOM_GenRangeXorshift(seed_state, 0, srange);
			u8 scale, root_sel, root;
			SEQ_CORE_FTS_GetScaleAndRoot(track, step, 0, tcc, &scale, &root_sel, &root);
			s32 walked = SEQ_SCALE_WalkScale(e->midi_package.note, scale, root, delta);
			e->midi_package.note = (walked < 0) ? e->midi_package.note : (u8)walked;
		} else {
			// chromatic semitone shift (original behaviour)
			range = tcc->robotize_note * 2;
			s16 value = e->midi_package.note + ((range/2) - (s16)SEQ_RANDOM_GenRangeXorshift(seed_state, 0, range));
			value = SEQ_CORE_TrimNote(value, 0, 127);
			e->midi_package.note = value;
		}
	}
		

	//OCTAVE ROBOTIZER - shift by octaves - cumulative with notes
	if ( tcc->robotize_oct && tcc->robotize_oct_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_oct_probability, randoms, &big_randoms_used) ) {
		range = tcc->robotize_oct * 2;
		s16 value = e->midi_package.note + (((range/2) - (s16)SEQ_RANDOM_GenRangeXorshift(seed_state, 0, range))*12);

		// ensure that note is in the 0..127 range
		value = SEQ_CORE_TrimNote(value, 0, 127);

		e->midi_package.note = value;
	}


	//VELOCITY ROBOTIZER - change note velocity
	if ( tcc->robotize_vel && tcc->robotize_vel_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_vel_probability, randoms, &big_randoms_used) ) {
		s16 randnum = SEQ_RANDOM_GenRangeXorshift( seed_state, 0, tcc->robotize_vel * 2) - tcc->robotize_vel;
		s16 vel = e->midi_package.velocity;
		s16 value = randnum + vel;

		// soft-clip: scale randnum by remaining headroom toward the bound.
		// (the original integer-divided a smaller number by 127, so this never fired.)
		if( value > 127 )
		  value = vel + ((s16)(127 - vel) * randnum) / 127;
		else if( value < 0 )
		  value = vel + (vel * randnum) / 127;

		if( value < 0 )
		  value = 0;
		else if( value > 127 )
		  value = 127;
		e->midi_package.velocity = value;
	}


	//GATELENGTH ROBOTIZER - change note duration
	if ( tcc->robotize_len && tcc->robotize_len_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_len_probability, randoms, &big_randoms_used) ) {
		s16 value = e->len + ((tcc->robotize_len/2) - (s16)SEQ_RANDOM_GenRangeXorshift(seed_state, 0, tcc->robotize_len));
		if( value < 1 )
		  value = 1;
		else if( value > 95 )
		  value = 95;
		e->len = value;
	}


	// NOTE SKIP ROBOTIZER
	if ( tcc->robotize_skip_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_skip_probability, randoms, &big_randoms_used) ) {
		e->midi_package.velocity = 0;// play with zero velocity
	}


	// SUSTAIN ROBOTIZER
	if ( tcc->robotize_sustain_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_sustain_probability, randoms, &big_randoms_used) ) {
		//set sustain flag
		returnflags.SUSTAIN = 1;
	}


	// NOFX ROBOTIZER
	if ( tcc->robotize_nofx_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_nofx_probability, randoms, &big_randoms_used) ) {
		//set NOFX flag
		returnflags.NOFX = 1;
	}


	// +ECHO ROBOTIZER
	if ( tcc->robotize_echo_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_echo_probability, randoms, &big_randoms_used) ) {
		//set +ECHO flag
		returnflags.ECHO = 1;
	}


	// +DUPLICATE ROBOTIZER
	if ( tcc->robotize_duplicate_probability && SEQ_ROBOTIZE_Check_Probabilities( seed_state, tcc->robotize_probability, tcc->robotize_duplicate_probability, randoms, &big_randoms_used) ) {
		//set +DUPLICATE flag
		returnflags.DUPLICATE = 1;
	}


  return returnflags;
}

