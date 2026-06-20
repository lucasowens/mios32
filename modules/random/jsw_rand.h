/*
 * Header file for Portable "Mersenne Twister" rand() functions
 * ==========================================================================
 *
 * By Julienne Walker (happyfrosty@hotmail.com)
 * License: Public Domain
 * 
 * ==========================================================================
 */
 
#ifndef JSW_RAND_H
#define JSW_RAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Seed the RNG. Must be called first */
extern void          jsw_seed ( unsigned long s );

/* Return a 32-bit random number */
extern unsigned long jsw_rand ( void );

/* Seed with current system time */
extern unsigned      jsw_time_seed();

/* Save / restore the full internal MT state (x[N] + next). Used by retroactive
 * CAPTURE to drive the engine through an offline re-simulation and then restore
 * the global RNG so live playback is non-destructively unchanged. The buffer is
 * JSW_RAND_STATE_WORDS unsigned longs (kept in sync with N via a compile check
 * in jsw_rand.c). */
#define JSW_RAND_STATE_WORDS 18  /* N(17) + next */
extern void          jsw_rand_state_save ( unsigned long *buf );
extern void          jsw_rand_state_restore ( const unsigned long *buf );

#ifdef __cplusplus
}
#endif

#endif
