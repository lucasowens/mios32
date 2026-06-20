// $Id$
/*
 * Random generator
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
#include <jsw_rand.h>

/////////////////////////////////////////////////////////////////////////////
// Local defines
/////////////////////////////////////////////////////////////////////////////

// Reentrant "rand_r" not supported by MinGW, therefore we are using rand() instead
// Now using portable jsw_rand() functions due to problems with newlib on windows/linux
#define REENTRANT 0


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

// initial seed
static unsigned int random_value = 0xdeadbabe;


/////////////////////////////////////////////////////////////////////////////
// random generator function
/////////////////////////////////////////////////////////////////////////////
u32 SEQ_RANDOM_Gen(u32 seed)
{
#if REENTRANT
  if( seed )
    random_value = seed;
	

  // MEMO: for thread safeness, we would have to pass a pointer to random_value through the SEQ_RANDOM_Gen() function
  // however, multiple threads just increase the randomness even more - therefore this approach is fine ;-)

  // TODO: combine with timer values for even more randomness
  rand_r(&random_value);

  return random_value;
#else
  if( seed )
	jsw_seed(seed);
	
  return random_value = (u32)jsw_rand();
#endif
}


/////////////////////////////////////////////////////////////////////////////
// xorshift32 with caller-supplied state.
// state must never be 0 (it produces an all-zero stream); we treat 0 as
// "uninitialized" and substitute a non-zero default so callers can leave
// the field zero-initialized.
/////////////////////////////////////////////////////////////////////////////
u32 SEQ_RANDOM_GenXorshift(u32 *state)
{
  u32 x = *state;
  if( !x )
    x = 0xdeadbabe;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

u32 SEQ_RANDOM_GenRangeXorshift(u32 *state, u32 min, u32 max)
{
  if( min == max )
    return min;
  if( min > max ) {
    u32 tmp = min; min = max; max = tmp;
  }
  return min + (SEQ_RANDOM_GenXorshift(state) % (max - min + 1));
}


/////////////////////////////////////////////////////////////////////////////
// returns random number in a given range
/////////////////////////////////////////////////////////////////////////////
u32 SEQ_RANDOM_Gen_Range(u32 min, u32 max)
{
  // values equal? -> no random number required
  if( min == max )
    return min;

  // swap min/max if reversed
  if( min > max ) {
    u32 tmp;
    tmp = min;
    min = max;
    max = tmp;
  }
  // generate new random number
#if REENTRANT
  rand_r(&random_value);
#else
  random_value = jsw_rand();
#endif

  // return result within the given range
  return min + (random_value % (max-min+1));
}


/////////////////////////////////////////////////////////////////////////////
// Global-RNG state save / restore (retroactive CAPTURE non-destructiveness).
//
// The "global RNG" is the jsw Mersenne-Twister (its real state, x[N]+next, lives
// in jsw_rand.c) plus this module's cached last value `random_value`. CAPTURE
// drives an offline re-simulation that may draw the global RNG (emission
// coin-flips: probability / humanize / random-gate / echo) and must restore it so
// live playback is byte-identical afterwards. Within the first-cut proof-span
// scope (probability=100, none of those effects) the re-sim draws it zero times,
// so this is belt-and-braces — but it makes the restore correct for ANY span.
//
// buf layout: [0] = random_value cache, [1..JSW_RAND_STATE_WORDS] = jsw state.
/////////////////////////////////////////////////////////////////////////////
void SEQ_RANDOM_StateGet(u32 *buf)
{
  buf[0] = random_value;
  jsw_rand_state_save((unsigned long *)&buf[1]);
}

void SEQ_RANDOM_StateSet(const u32 *buf)
{
  random_value = buf[0];
  jsw_rand_state_restore((const unsigned long *)&buf[1]);
}
