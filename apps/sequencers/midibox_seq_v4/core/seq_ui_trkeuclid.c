// $Id$
/*
 * Euclidean Algorithm Generator + Cellular Automaton Generator
 * Inspired from:
 *   - Ruin & Wesen: http://ruinwesen.com/blog?id=216
 *   - crx: http://crx091081gb.net/?p=189
 *   - Wolfram, "A New Kind of Science" (elementary 1D CA, rules 0..255)
 *
 * ==========================================================================
 *
 *  Copyright (C) 2012 Thorsten Klose (tk@midibox.org)
 *  Copyright of "Eugen" algorithm: crx
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

#include "seq_lcd.h"
#include "seq_ui.h"

#include "seq_core.h"
#include "seq_par.h"
#include "seq_trg.h"
#include "seq_layer.h"
#include "seq_cc.h"
#include "seq_random.h"


/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

#define NUM_OF_ITEMS         11
#define ITEM_GXTY            0
#define ITEM_TRK_LENGTH      1
#define ITEM_PARTRG_SELECT   2
#define ITEM_PAR_VALUE       3
#define ITEM_DRUM_VEL_N      4
#define ITEM_DRUM_VEL_A      5
#define ITEM_RND_ACC_PROB    6
#define ITEM_GEN_TYPE        7
// ITEM_EUCLID_{LENGTH,PULSES,OFFSET} are the three "tier slots" on GP9/10/11.
// What they actually edit depends on (gen_type, page_view): when page_view==1
// they edit the type's PG2 backing fields (e.g. Poly N2/M2/Phase2, CA boundary).
#define ITEM_EUCLID_LENGTH   8
#define ITEM_EUCLID_PULSES   9
#define ITEM_EUCLID_OFFSET  10

#define GEN_TYPE_EUCLID      0
#define GEN_TYPE_CA          1
#define GEN_TYPE_POLY        2
#define GEN_TYPE_SUB         3
#define GEN_TYPE_LSYS        4
#define GEN_TYPE_NUM         5


/////////////////////////////////////////////////////////////////////////////
// Local Variables
/////////////////////////////////////////////////////////////////////////////

// pre-selections for each trigger layer separately
#define NUM_EUCLID_CFG 16
static u8 euclid_length[NUM_EUCLID_CFG] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
static u8 euclid_pulses[NUM_EUCLID_CFG] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static u8 euclid_offset[NUM_EUCLID_CFG] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
// CA per-layer state: rule (0..255), seed (1..255 bit-packed), gens (0..32),
// boundary (0=wrap, 1=zero-pad, 2=mirror)
static u8 ca_rule[NUM_EUCLID_CFG]       = {30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};
static u8 ca_seed[NUM_EUCLID_CFG]       = { 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1};
static u8 ca_gens[NUM_EUCLID_CFG]       = { 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8};
static u8 ca_boundary[NUM_EUCLID_CFG]   = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
#define CA_BND_WRAP   0
#define CA_BND_ZERO   1
#define CA_BND_MIRROR 2
#define CA_BND_NUM    3
// Polyrhythm per-layer: n (pulses), m (cycle length), phase (0..m-1).
// Optional 2nd layer OR'd onto the first: n2/m2/phase2. n2=0 disables it.
static u8 poly_n[NUM_EUCLID_CFG]        = { 3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3};
static u8 poly_m[NUM_EUCLID_CFG]        = { 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8};
static u8 poly_phase[NUM_EUCLID_CFG]    = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static u8 poly_n2[NUM_EUCLID_CFG]       = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static u8 poly_m2[NUM_EUCLID_CFG]       = { 4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4};
static u8 poly_phase2[NUM_EUCLID_CFG]   = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
// Recursive subdivide per-layer: depth (1..8), probability (0..100), seed (1..255)
static u8 sub_depth[NUM_EUCLID_CFG]     = { 4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4};
static u8 sub_prob[NUM_EUCLID_CFG]      = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
static u8 sub_seed[NUM_EUCLID_CFG]      = { 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1};
// L-system per-layer: preset (0..7), iterations (0..6), seed (1..255)
static u8 lsys_preset[NUM_EUCLID_CFG]   = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static u8 lsys_iter[NUM_EUCLID_CFG]     = { 3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3};
static u8 lsys_seed[NUM_EUCLID_CFG]     = { 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1};
static u8 gen_type[NUM_EUCLID_CFG]      = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
// Sub-page toggle for GP9/10/11 (0=primary params, 1=secondary). GP16 flips it.
// Persists per trigger layer, like the rest of the per-layer state.
static u8 page_view[NUM_EUCLID_CFG]     = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
static u8 rnd_acc_probability = 50;
static u8 rnd_acc_n = 100; // only for non-drum layers
static u8 rnd_acc_a = 127; // only for non-drum layers
static u8 par_val[16] = { // used parameter layer values
  0x3c, 0x3c, 0x3c, 0x3c,
  0x3c, 0x3c, 0x3c, 0x3c,
  0x3c, 0x3c, 0x3c, 0x3c,
  0x3c, 0x3c, 0x3c, 0x3c,
};


/////////////////////////////////////////////////////////////////////////////
// Local Prototypes
/////////////////////////////////////////////////////////////////////////////

static u8 GenNumPages(u8 type);
static s32 EuclidGenerator(u8 track, u16 steps, u16 pulses, u16 offset);
static s32 CAGenerator(u8 track, u8 rule, u8 seed, u8 gens, u8 boundary);
static s32 PolyrhythmGenerator(u8 track, u8 n, u8 m, u8 phase, u8 n2, u8 m2, u8 phase2);
static s32 SubdivideGenerator(u8 track, u8 depth, u8 prob, u8 seed);
static s32 LSystemGenerator(u8 track, u8 preset, u8 iter, u8 seed);
static s32 WriteGateAtStep(u8 track, u16 step, u8 gate, u8 modify_par_layer, u8 instrument);
static s32 RunActiveGenerator(u8 track, u8 layer);
static s32 AccentGenerator(u8 track, u16 steps);
static s32 ReGenAccent(u8 track, u8 prev_rnd_acc_n, u8 prev_rnd_acc_a);


/////////////////////////////////////////////////////////////////////////////
// Local LED handler function
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  if( ui_cursor_flash ) // if flashing flag active: no LED flag set
    return 0;

  switch( ui_selected_item ) {
    case ITEM_GXTY:          *gp_leds = 0x0001; break;
    case ITEM_TRK_LENGTH:    *gp_leds = 0x0006; break;
    case ITEM_PARTRG_SELECT: *gp_leds = 0x0008; break;
    case ITEM_PAR_VALUE:     *gp_leds = 0x0010; break;
    case ITEM_DRUM_VEL_N:    *gp_leds = 0x0020; break;
    case ITEM_DRUM_VEL_A:    *gp_leds = 0x0040; break;
    case ITEM_RND_ACC_PROB:  *gp_leds = 0x0080; break;
    case ITEM_EUCLID_LENGTH: *gp_leds = 0x0100; break;
    case ITEM_EUCLID_PULSES: *gp_leds = 0x0200; break;
    case ITEM_EUCLID_OFFSET: *gp_leds = 0x0400; break;
    case ITEM_GEN_TYPE:      *gp_leds = 0x0800; break;
  }

  // GP16 indicator: lit when we're on PG2. Only the types that have a PG2
  // light it; for others the button is a no-op and the LED stays dark.
  {
    u8 visible_track = SEQ_UI_VisibleTrackGet();
    u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
    u8 layer = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : ui_selected_trg_layer;
    if( GenNumPages(gen_type[layer]) > 1 && page_view[layer] > 0 ) {
      *gp_leds |= 0x8000;
    }
  }

  return 0; // no error
}



/////////////////////////////////////////////////////////////////////////////
// Local encoder callback function
// Should return:
//   1 if value has been changed
//   0 if value hasn't been changed
//  -1 if invalid or unsupported encoder
/////////////////////////////////////////////////////////////////////////////
static s32 Encoder_Handler(seq_ui_encoder_t encoder, s32 incrementer)
{
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
  u8 euclid_layer = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : ui_selected_trg_layer;
  u16 num_steps = SEQ_TRG_NumStepsGet(visible_track);

  switch( encoder ) {
    case SEQ_UI_ENCODER_GP1:
      ui_selected_item = ITEM_GXTY;
      break;

    case SEQ_UI_ENCODER_GP2:
    case SEQ_UI_ENCODER_GP3:
      ui_selected_item = ITEM_TRK_LENGTH;
      break;

    case SEQ_UI_ENCODER_GP4:
      ui_selected_item = ITEM_PARTRG_SELECT;
      break;

    case SEQ_UI_ENCODER_GP5:
      ui_selected_item = ITEM_PAR_VALUE;
      break;

    case SEQ_UI_ENCODER_GP6:
      ui_selected_item = ITEM_DRUM_VEL_N;
      break;

    case SEQ_UI_ENCODER_GP7:
      ui_selected_item = ITEM_DRUM_VEL_A;
      break;

    case SEQ_UI_ENCODER_GP8:
      ui_selected_item = ITEM_RND_ACC_PROB;
      break;

    case SEQ_UI_ENCODER_GP9:
      ui_selected_item = ITEM_EUCLID_LENGTH;
      break;

    case SEQ_UI_ENCODER_GP10:
      ui_selected_item = ITEM_EUCLID_PULSES;
      break;

    case SEQ_UI_ENCODER_GP11:
      ui_selected_item = ITEM_EUCLID_OFFSET;
      break;

    case SEQ_UI_ENCODER_GP12:
      ui_selected_item = ITEM_GEN_TYPE;
      break;

    case SEQ_UI_ENCODER_GP13:
    case SEQ_UI_ENCODER_GP14:
    case SEQ_UI_ENCODER_GP15:
    case SEQ_UI_ENCODER_GP16:
      return 0; // no encoder function
  }

  switch( ui_selected_item ) {
  case ITEM_GXTY: return SEQ_UI_GxTyInc(incrementer);

  case ITEM_TRK_LENGTH: {    
    if( SEQ_UI_CC_Inc(SEQ_CC_LENGTH, 0, num_steps-1, incrementer) >= 1 ) {
      if( seq_cc_trk[visible_track].clkdiv.SYNCH_TO_MEASURE && 
	  (int)SEQ_CC_Get(visible_track, SEQ_CC_LENGTH) > (int)seq_core_steps_per_measure ) {
	char buffer[20];
	sprintf(buffer, "active for %d steps", (int)seq_core_steps_per_measure+1);
	SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Synch-to-Measure is", buffer);
      }
      return 1;
    }
  } break;

  case ITEM_PARTRG_SELECT: {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      u8 num_drums = SEQ_TRG_NumInstrumentsGet(visible_track);
      return SEQ_UI_Var8_Inc(&ui_selected_instrument, 0, num_drums-1, incrementer);
    } else {
      u8 num_p_layers = SEQ_PAR_NumLayersGet(visible_track);
      return SEQ_UI_Var8_Inc(&ui_selected_par_layer, 0, num_p_layers-1, incrementer);
    }
  } break;

  case ITEM_PAR_VALUE: {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      return SEQ_UI_CC_Inc(SEQ_CC_LAY_CONST_A1 + ui_selected_instrument, 0, 127, incrementer);
    } else {
      if( SEQ_UI_Var8_Inc(&par_val[ui_selected_par_layer], 0, 127, incrementer) > 0 ) {
	RunActiveGenerator(visible_track, euclid_layer);
	return 1;
      }
      return 0;
    }
  } break;

  case ITEM_DRUM_VEL_N: {
    if( event_mode == SEQ_EVENT_MODE_Drum && !SEQ_CC_TrackHasVelocityParLayer(visible_track) ) {
      return SEQ_UI_CC_Inc(SEQ_CC_LAY_CONST_B1 + ui_selected_instrument, 0, 127, incrementer);
    } else {
      u8 prev_rnd_acc_n = rnd_acc_n;
      if( SEQ_UI_Var8_Inc(&rnd_acc_n, 1, rnd_acc_a-1, incrementer) >= 1 ) {
	// re-generate accent
	ReGenAccent(visible_track, prev_rnd_acc_n, rnd_acc_a);
      }
      return 1;
    }
  } break;

  case ITEM_RND_ACC_PROB: {
    incrementer *= 10; // more comfortable to do this in bigger steps...
    SEQ_UI_Var8_Inc(&rnd_acc_probability, 0, 100, incrementer);

    // generate new pattern
    AccentGenerator(visible_track, (int)euclid_length[euclid_layer]+1);

    return 1;
  } break;

  case ITEM_DRUM_VEL_A: {
    if( event_mode == SEQ_EVENT_MODE_Drum && !SEQ_CC_TrackHasVelocityParLayer(visible_track) ) {
      return SEQ_UI_CC_Inc(SEQ_CC_LAY_CONST_C1 + ui_selected_instrument, 0, 127, incrementer);
    } else {
      u8 prev_rnd_acc_a = rnd_acc_a;
      if( SEQ_UI_Var8_Inc(&rnd_acc_a, rnd_acc_n+1, 127, incrementer) >= 1 ) {
	// re-generate accent
	ReGenAccent(visible_track, rnd_acc_n, prev_rnd_acc_a);
      }
      return 1;
    }
  } break;

  case ITEM_EUCLID_LENGTH:
  case ITEM_EUCLID_OFFSET:
  case ITEM_EUCLID_PULSES: {
    // GP9/10/11 are re-skinned per generator type. Each type names what those
    // three encoders mean; we mutate the right backing field, then re-run.
    u16 max_offset = (num_steps > 255) ? 255 : num_steps;
    u8 pv = page_view[euclid_layer];
    switch( gen_type[euclid_layer] ) {
      case GEN_TYPE_CA:
        if( pv == 0 ) {
          if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&ca_rule[euclid_layer], 0, 255, incrementer);
          else if( ui_selected_item == ITEM_EUCLID_PULSES ) SEQ_UI_Var8_Inc(&ca_seed[euclid_layer], 1, 255, incrementer);
          else                                              SEQ_UI_Var8_Inc(&ca_gens[euclid_layer], 0, 32, incrementer);
        } else {
          // PG2: only boundary mode is meaningful (GP9). GP10/11 are unused
          // on PG2 for CA — silently ignore so the encoders don't surprise.
          if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&ca_boundary[euclid_layer], 0, CA_BND_NUM-1, incrementer);
          else                                              return 0;
        }
        break;
      case GEN_TYPE_POLY:
        if( pv == 0 ) {
          // GP9=N, GP10=M, GP11=phase. Clamp phase against m to keep it valid.
          if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&poly_n[euclid_layer], 0, poly_m[euclid_layer], incrementer);
          else if( ui_selected_item == ITEM_EUCLID_PULSES ) {
            SEQ_UI_Var8_Inc(&poly_m[euclid_layer], 1, (num_steps > 255) ? 255 : num_steps, incrementer);
            if( poly_n[euclid_layer] > poly_m[euclid_layer] ) poly_n[euclid_layer] = poly_m[euclid_layer];
            if( poly_phase[euclid_layer] >= poly_m[euclid_layer] ) poly_phase[euclid_layer] = poly_m[euclid_layer]-1;
          }
          else                                              SEQ_UI_Var8_Inc(&poly_phase[euclid_layer], 0, poly_m[euclid_layer]-1, incrementer);
        } else {
          // PG2: 2nd polyrhythm layer. n2=0 disables it.
          if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&poly_n2[euclid_layer], 0, poly_m2[euclid_layer], incrementer);
          else if( ui_selected_item == ITEM_EUCLID_PULSES ) {
            SEQ_UI_Var8_Inc(&poly_m2[euclid_layer], 1, (num_steps > 255) ? 255 : num_steps, incrementer);
            if( poly_n2[euclid_layer] > poly_m2[euclid_layer] ) poly_n2[euclid_layer] = poly_m2[euclid_layer];
            if( poly_phase2[euclid_layer] >= poly_m2[euclid_layer] ) poly_phase2[euclid_layer] = poly_m2[euclid_layer]-1;
          }
          else                                              SEQ_UI_Var8_Inc(&poly_phase2[euclid_layer], 0, poly_m2[euclid_layer]-1, incrementer);
        }
        break;
      case GEN_TYPE_SUB:
        // GP9=depth, GP10=prob, GP11=seed
        if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&sub_depth[euclid_layer], 1, 8, incrementer);
        else if( ui_selected_item == ITEM_EUCLID_PULSES ) SEQ_UI_Var8_Inc(&sub_prob[euclid_layer], 0, 100, incrementer);
        else                                              SEQ_UI_Var8_Inc(&sub_seed[euclid_layer], 1, 255, incrementer);
        break;
      case GEN_TYPE_LSYS:
        // GP9=preset, GP10=iter, GP11=seed
        if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&lsys_preset[euclid_layer], 0, 7, incrementer);
        else if( ui_selected_item == ITEM_EUCLID_PULSES ) SEQ_UI_Var8_Inc(&lsys_iter[euclid_layer], 0, 6, incrementer);
        else                                              SEQ_UI_Var8_Inc(&lsys_seed[euclid_layer], 1, 255, incrementer);
        break;
      case GEN_TYPE_EUCLID:
      default:
        if( ui_selected_item == ITEM_EUCLID_LENGTH )      SEQ_UI_Var8_Inc(&euclid_length[euclid_layer], 0, num_steps-1, incrementer);
        else if( ui_selected_item == ITEM_EUCLID_OFFSET ) SEQ_UI_Var8_Inc(&euclid_offset[euclid_layer], 0, max_offset, incrementer);
        else                                              SEQ_UI_Var8_Inc(&euclid_pulses[euclid_layer], 0, max_offset, incrementer);
        break;
    }

    RunActiveGenerator(visible_track, euclid_layer);

    // +accent if drum track or first par layer. For non-euclid generators
    // we span the whole track length; euclid still uses its cycle length.
    if( event_mode == SEQ_EVENT_MODE_Drum || ui_selected_par_layer == 0 ) {
      u16 accent_len = (gen_type[euclid_layer] == GEN_TYPE_EUCLID)
        ? (u16)((int)euclid_length[euclid_layer]+1) : num_steps;
      AccentGenerator(visible_track, accent_len);
    }

    return 1;
  } break;

  case ITEM_GEN_TYPE: {
    if( SEQ_UI_Var8_Inc(&gen_type[euclid_layer], 0, GEN_TYPE_NUM-1, incrementer) >= 1 ) {
      // re-run the now-active generator so the user sees the swap immediately
      RunActiveGenerator(visible_track, euclid_layer);
      if( event_mode == SEQ_EVENT_MODE_Drum || ui_selected_par_layer == 0 ) {
        u16 accent_len = (gen_type[euclid_layer] == GEN_TYPE_EUCLID)
          ? (u16)((int)euclid_length[euclid_layer]+1) : num_steps;
        AccentGenerator(visible_track, accent_len);
      }
      return 1;
    }
    return 0;
  } break;

  }

  return -1; // invalid or unsupported encoder
}


/////////////////////////////////////////////////////////////////////////////
// Local button callback function
// Should return:
//   1 if value has been changed
//   0 if value hasn't been changed
//  -1 if invalid or unsupported button
/////////////////////////////////////////////////////////////////////////////
static u8 GenNumPages(u8 type)
{
  // Number of sub-pages (1 = no toggle, GP16 is a no-op for that type).
  // Keep in sync with the LCD label/value switches: every page index in
  // 0..GenNumPages(type)-1 must render meaningful labels and values.
  switch( type ) {
    case GEN_TYPE_POLY: return 2;
    case GEN_TYPE_CA:   return 2;
    default:            return 1;
  }
}

static s32 Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( depressed ) return 0; // ignore when button depressed

  // GP16: cycle the GP9/10/11 sub-page for the active layer. Multi-press
  // advances 0 → 1 → ... → N-1 → 0. Types with only one page (N=1) are no-ops.
  if( button == SEQ_UI_BUTTON_GP16 ) {
    u8 visible_track = SEQ_UI_VisibleTrackGet();
    u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
    u8 euclid_layer = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : ui_selected_trg_layer;
    u8 n = GenNumPages(gen_type[euclid_layer]);
    if( n > 1 ) {
      page_view[euclid_layer] = (page_view[euclid_layer] + 1) % n;
      return 1;
    }
    return 0;
  }

#if 0
  // leads to: comparison is always true due to limited range of data type
  if( button >= SEQ_UI_BUTTON_GP1 && button <= SEQ_UI_BUTTON_GP16 ) {
#else
  if( button <= SEQ_UI_BUTTON_GP16 ) {
#endif
    // re-use encoder handler - only select UI item, don't increment, flags will be toggled
    return Encoder_Handler((int)button, 0);
  }

  switch( button ) {
    case SEQ_UI_BUTTON_Select:
    case SEQ_UI_BUTTON_Right:
      if( depressed ) return -1;
      if( ++ui_selected_item >= NUM_OF_ITEMS )
	ui_selected_item = 0;
      return 1; // value always changed

    case SEQ_UI_BUTTON_Left:
      if( depressed ) return -1;
      if( ui_selected_item == 0 )
	ui_selected_item = NUM_OF_ITEMS-1;
      else
	--ui_selected_item;
      return 1; // value always changed

    case SEQ_UI_BUTTON_Up:
      if( depressed ) return -1;
      return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, 1);

    case SEQ_UI_BUTTON_Down:
      if( depressed ) return -1;
      return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, -1);
  }

  return -1; // invalid or unsupported button
}


/////////////////////////////////////////////////////////////////////////////
// Local Display Handler function
// IN: <high_prio>: if set, a high-priority LCD update is requested
/////////////////////////////////////////////////////////////////////////////
static s32 LCD_Handler(u8 high_prio)
{
  if( high_prio )
    return 0; // there are no high-priority updates

  // layout:
  // 00000000001111111111222222222233333333330000000000111111111122222222223333333333
  // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  // <--------------------------------------><-------------------------------------->
  // Trk. TrkLength Drum Note VelN VelA RndA Len Pulses Offs. Typ *.**.**.**.**.*.*.*.
  // G3T2   64/128   CH   F#1  100  127  50%  20   12     0  EUC

  // 00000000001111111111222222222233333333330000000000111111111122222222223333333333
  // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  // <--------------------------------------><-------------------------------------->
  // Trk. TrkLength ParA Val. VelN VelA RndA Rule Seed  Gens. Typ *..*..*..*..*.*.*..*
  // G1T1   64/128  Note Orig  100  127  50%   30   1    8    CA

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
  u8 euclid_layer = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : ui_selected_trg_layer;
  u8 active_type = gen_type[euclid_layer];
  u8 npages = GenNumPages(active_type);
  u8 pv = page_view[euclid_layer];
  // Clamp pv if the user switched to a type with fewer pages while page_view
  // was higher; we don't reset the stored value (the user might switch back).
  if( pv >= npages ) pv = 0;


  ///////////////////////////////////////////////////////////////////////////
  SEQ_LCD_CursorSet(0, 0);
  SEQ_LCD_PrintString("Trk. TrkLength ");
  if( event_mode == SEQ_EVENT_MODE_Drum ) {
    SEQ_LCD_PrintString("Drum Note VelN VelA RndA ");
  } else {
    SEQ_LCD_PrintFormattedString("Par%c", 'A'+ui_selected_par_layer);
    SEQ_LCD_PrintString(" Val. VelN VelA RndA ");
  }
  // Right-LCD header tail must be exactly 20 chars (cols 40..59).
  if( pv == 1 ) {
    switch( active_type ) {
      case GEN_TYPE_CA:   SEQ_LCD_PrintString("Bound.           Typ"); break;
      case GEN_TYPE_POLY: SEQ_LCD_PrintString(" N2  Cyc2  Phs2  Typ"); break;
      default:            SEQ_LCD_PrintString("                 Typ"); break; // unreachable
    }
  } else {
    switch( active_type ) {
      case GEN_TYPE_CA:   SEQ_LCD_PrintString("Rule Seed  Gens. Typ"); break;
      case GEN_TYPE_POLY: SEQ_LCD_PrintString(" N   Cycle Phase Typ"); break;
      case GEN_TYPE_SUB:  SEQ_LCD_PrintString("Depth Prob. Seed Typ"); break;
      case GEN_TYPE_LSYS: SEQ_LCD_PrintString("Preset Iter Seed Typ"); break;
      case GEN_TYPE_EUCLID:
      default:            SEQ_LCD_PrintString("Len Pulses Offs. Typ"); break;
    }
  }

  ///////////////////////////////////////////////////////////////////////////
  {
    u16 len = SEQ_CC_Get(visible_track, SEQ_CC_LENGTH)+1;
    u8 max_trigger = (len > 20) ? 20 : len;
    seq_cc_trk_t *tcc = &seq_cc_trk[visible_track];    

    int i;
    for(i=0; i<max_trigger; ++i) {
      u8 gate = SEQ_TRG_GateGet(visible_track, i, ui_selected_instrument);
      u8 gate_disabled = 0;
      if( event_mode != SEQ_EVENT_MODE_Drum && ui_selected_par_layer > 0 ) {
	if( !gate ) {
	  gate_disabled = SEQ_PAR_Get(visible_track, i, ui_selected_par_layer, ui_selected_instrument) > 0;
	} else {
	  gate = SEQ_PAR_Get(visible_track, i, ui_selected_par_layer, ui_selected_instrument) > 0;
	}
      }

      if( gate_disabled ) {
	SEQ_LCD_PrintChar('-');
      } else {
	u8 accent = !gate ? 0 : SEQ_TRG_AccentGet(visible_track, i, ui_selected_instrument);

	if( gate && !accent && tcc->link_par_layer_velocity >= 0 ) {
	  u8 value = SEQ_PAR_Get(visible_track, i, tcc->link_par_layer_velocity, ui_selected_instrument);
	  if( rnd_acc_n <= rnd_acc_a ) {
	    if( value >= rnd_acc_a )
	      accent |= 1;
	  } else {
	    if( value <= rnd_acc_a )
	      accent |= 1;
	  }
	}
	SEQ_LCD_PrintChar(gate | (accent << 1));
      }
    }

    for(;i<20; ++i) {
      SEQ_LCD_PrintChar(' ');
    }
  }
  

  ///////////////////////////////////////////////////////////////////////////
  SEQ_LCD_CursorSet(0, 1);

  if( ui_selected_item == ITEM_GXTY && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintGxTy(ui_selected_group, ui_selected_tracks);
    SEQ_LCD_PrintSpaces(1);
  }

  ///////////////////////////////////////////////////////////////////////////
  if( ui_selected_item == ITEM_TRK_LENGTH && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(10);
  } else {
    u16 num_steps = SEQ_TRG_NumStepsGet(visible_track);
    u16 len = SEQ_CC_Get(visible_track, SEQ_CC_LENGTH)+1;

    if( len > num_steps )
      SEQ_LCD_PrintFormattedString(" !!!/%3d  ", num_steps);
    else
      SEQ_LCD_PrintFormattedString(" %3d/%3d  ", len, num_steps);
  }

  ///////////////////////////////////////////////////////////////////////////
  if( ui_selected_item == ITEM_PARTRG_SELECT && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      SEQ_LCD_PrintTrackDrum(visible_track, ui_selected_instrument, (char *)seq_core_trk[visible_track].name);
    } else {
      char str_buffer[6];
      SEQ_PAR_AssignedTypeStr(visible_track, ui_selected_par_layer, ui_selected_instrument, str_buffer);
      SEQ_LCD_PrintString(str_buffer);
    }
  }

  /////////////////////////////////////////////////////////////////////////
  if( ui_selected_item == ITEM_PAR_VALUE && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      SEQ_LCD_PrintSpaces(1);
      SEQ_LCD_PrintNote(SEQ_CC_Get(visible_track, SEQ_CC_LAY_CONST_A1 + ui_selected_instrument));
      SEQ_LCD_PrintSpaces(1);
    } else {
      if( ui_selected_par_layer == 0 ) {
	SEQ_LCD_PrintString("Orig ");
      } else {
	SEQ_LCD_PrintLayerValue(visible_track, ui_selected_par_layer, par_val[ui_selected_par_layer]);
	SEQ_LCD_PrintSpaces(1);
      }
    }
  }
  SEQ_LCD_PrintSpaces(1);

  /////////////////////////////////////////////////////////////////////////
  if( ui_selected_item == ITEM_DRUM_VEL_N && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(4);
  } else {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      SEQ_LCD_PrintFormattedString("%3d ", SEQ_CC_Get(visible_track, SEQ_CC_LAY_CONST_B1 + ui_selected_instrument));
    } else {
      SEQ_LCD_PrintFormattedString("%3d ", rnd_acc_n);
    }
  }

  /////////////////////////////////////////////////////////////////////////
  if( ui_selected_item == ITEM_DRUM_VEL_A && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
    SEQ_LCD_PrintFormattedString(" %3d ", SEQ_CC_Get(visible_track, SEQ_CC_LAY_CONST_C1 + ui_selected_instrument));
    } else {
      SEQ_LCD_PrintFormattedString(" %3d ", rnd_acc_a);
    }
  }

  /////////////////////////////////////////////////////////////////////////
  if( ui_selected_item == ITEM_RND_ACC_PROB && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintFormattedString("%3d%% ", rnd_acc_probability);
  }

  /////////////////////////////////////////////////////////////////////////
  // GP9: 4 cols
  if( ui_selected_item == ITEM_EUCLID_LENGTH && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(4);
  } else if( pv == 1 ) {
    switch( active_type ) {
      case GEN_TYPE_CA: {
        // 4-char boundary-mode label
        const char *bnd;
        switch( ca_boundary[euclid_layer] ) {
          case CA_BND_ZERO:   bnd = "Zero"; break;
          case CA_BND_MIRROR: bnd = "Mirr"; break;
          case CA_BND_WRAP:
          default:            bnd = "Wrap"; break;
        }
        SEQ_LCD_PrintString(bnd);
      } break;
      case GEN_TYPE_POLY: SEQ_LCD_PrintFormattedString("%3d ", poly_n2[euclid_layer]); break;
      default:            SEQ_LCD_PrintSpaces(4); break;
    }
  } else {
    switch( active_type ) {
      case GEN_TYPE_CA:   SEQ_LCD_PrintFormattedString("%3d ", ca_rule[euclid_layer]); break;
      case GEN_TYPE_POLY: SEQ_LCD_PrintFormattedString("%3d ", poly_n[euclid_layer]); break;
      case GEN_TYPE_SUB:  SEQ_LCD_PrintFormattedString("%3d ", sub_depth[euclid_layer]); break;
      case GEN_TYPE_LSYS: SEQ_LCD_PrintFormattedString("%3d ", lsys_preset[euclid_layer]); break;
      case GEN_TYPE_EUCLID:
      default:            SEQ_LCD_PrintFormattedString("%3d ", (int)euclid_length[euclid_layer]+1); break;
    }
  }

  /////////////////////////////////////////////////////////////////////////
  // GP10: 6 cols
  if( ui_selected_item == ITEM_EUCLID_PULSES && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(6);
  } else if( pv == 1 ) {
    switch( active_type ) {
      case GEN_TYPE_POLY: SEQ_LCD_PrintFormattedString(" %3d  ", poly_m2[euclid_layer]); break;
      case GEN_TYPE_CA:   // PG2 CA has no GP10 param
      default:            SEQ_LCD_PrintSpaces(6); break;
    }
  } else {
    switch( active_type ) {
      case GEN_TYPE_CA:   SEQ_LCD_PrintFormattedString(" %3d  ", ca_seed[euclid_layer]); break;
      case GEN_TYPE_POLY: SEQ_LCD_PrintFormattedString(" %3d  ", poly_m[euclid_layer]); break;
      case GEN_TYPE_SUB:  SEQ_LCD_PrintFormattedString(" %3d  ", sub_prob[euclid_layer]); break;
      case GEN_TYPE_LSYS: SEQ_LCD_PrintFormattedString(" %3d  ", lsys_iter[euclid_layer]); break;
      case GEN_TYPE_EUCLID:
      default:            SEQ_LCD_PrintFormattedString(" %3d  ", euclid_pulses[euclid_layer]); break;
    }
  }

  /////////////////////////////////////////////////////////////////////////
  // GP11: 6 cols
  if( ui_selected_item == ITEM_EUCLID_OFFSET && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(6);
  } else if( pv == 1 ) {
    switch( active_type ) {
      case GEN_TYPE_POLY: SEQ_LCD_PrintFormattedString(" %3d  ", poly_phase2[euclid_layer]); break;
      case GEN_TYPE_CA:
      default:            SEQ_LCD_PrintSpaces(6); break;
    }
  } else {
    switch( active_type ) {
      case GEN_TYPE_CA:   SEQ_LCD_PrintFormattedString(" %3d  ", ca_gens[euclid_layer]); break;
      case GEN_TYPE_POLY: SEQ_LCD_PrintFormattedString(" %3d  ", poly_phase[euclid_layer]); break;
      case GEN_TYPE_SUB:  SEQ_LCD_PrintFormattedString(" %3d  ", sub_seed[euclid_layer]); break;
      case GEN_TYPE_LSYS: SEQ_LCD_PrintFormattedString(" %3d  ", lsys_seed[euclid_layer]); break;
      case GEN_TYPE_EUCLID:
      default:            SEQ_LCD_PrintFormattedString(" %3d  ", euclid_offset[euclid_layer]); break;
    }
  }

  /////////////////////////////////////////////////////////////////////////
  // GP12: Type tag -- 4 cols
  if( ui_selected_item == ITEM_GEN_TYPE && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(4);
  } else {
    const char *tag;
    switch( active_type ) {
      case GEN_TYPE_CA:   tag = "  CA"; break;
      case GEN_TYPE_POLY: tag = "POLY"; break;
      case GEN_TYPE_SUB:  tag = " SUB"; break;
      case GEN_TYPE_LSYS: tag = "LSYS"; break;
      case GEN_TYPE_EUCLID:
      default:            tag = " EUC"; break;
    }
    SEQ_LCD_PrintString(tag);
  }

  /////////////////////////////////////////////////////////////////////////
  // GP13-15: unused. GP16: page indicator (only when type has >1 page).
  // Format "n/N" so users see at a glance how many sub-pages exist and which
  // one they're on. Cycles forward through pages on each GP16 press.
  if( npages > 1 ) {
    SEQ_LCD_PrintSpaces(16);
    SEQ_LCD_PrintFormattedString("%d/%d", pv+1, npages);
    SEQ_LCD_PrintSpaces(11);
  } else {
    SEQ_LCD_PrintSpaces(30);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_TRKEUCLID_Init(u32 mode)
{
  // install callback routines
  SEQ_UI_InstallButtonCallback(Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);

  SEQ_LCD_InitSpecialChars(SEQ_LCD_CHARSET_DrumSymbolsBig);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// The Euclid Generator
// Algorithm from crx (http://crx091081gb.net/?p=189)
/////////////////////////////////////////////////////////////////////////////
static s32 EuclidGenerator(u8 track, u16 steps, u16 pulses, u16 offset)
{
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;
  u8 modify_par_layer = event_mode != SEQ_EVENT_MODE_Drum && ui_selected_par_layer > 0;


  // to simplify code
#define EUCLID_SET_GATE(xstep, xgate) if( modify_par_layer ) {		\
                                        SEQ_PAR_Set(track, xstep, ui_selected_par_layer, instrument, (xgate) ? par_val[ui_selected_par_layer] : 0); \
                                      } else { \
                                        SEQ_TRG_GateSet(track, xstep, instrument, xgate); \
                                      }

  if( pulses >= steps ) {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      EUCLID_SET_GATE(step, 1);
    }
  } else if( pulses == 0 ) {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      EUCLID_SET_GATE(step, 0);
    }
  } else if( steps == 1 ) {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      EUCLID_SET_GATE(step, (pulses && step == offset) ? 1 : 0);
    }
  } else {
    int pauses = steps - pulses;

    if( pauses >= pulses ) { // first case: more pauses than pulses
      int per_pulse = pauses / pulses;
      int remainder = pauses % pulses;

      int loop_offset; // fill whole track with repeating pattern
      for(loop_offset=0; loop_offset<num_steps; loop_offset += steps) {
	u16 step = offset;
	u16 processed_steps = 0;

	int i;
	for(i=0; i<pulses; ++i) {
	  EUCLID_SET_GATE(loop_offset + step, 1);
	  if( ++processed_steps >= steps )
	    break;
	  step = (step + 1) % steps;
	  
	  int j;
	  for(j=0; j<per_pulse; ++j) {
	    EUCLID_SET_GATE(loop_offset + step, 0);
	    if( ++processed_steps >= steps )
	      break;
	    step = (step + 1) % steps;
	  }

	  if( processed_steps >= steps )
	    break;

	  if( i < remainder ) {
	    EUCLID_SET_GATE(loop_offset + step, 0);
	    if( ++processed_steps >= steps )
	      break;
	    step = (step + 1) % steps;
	  }

	  if( processed_steps >= steps )
	    break;
	}
      }
    } else { // second case: more pulses than pauses
      int per_pause = (pulses-pauses) / pauses;
      int remainder = (pulses-pauses) % pauses;

      int loop_offset; // fill whole track with repeating pattern
      for(loop_offset=0; loop_offset<num_steps; loop_offset += steps) {
	u16 step = offset;
	u16 processed_steps = 0;

	int i;
	for(i=0; i<pauses; ++i) {
	  EUCLID_SET_GATE(loop_offset + step, 1);
	  if( ++processed_steps >= steps )
	    break;
	  step = (step + 1) % steps;

	  EUCLID_SET_GATE(loop_offset + step, 0);
	  if( ++processed_steps >= steps )
	    break;
	  step = (step + 1) % steps;

	  int j;
	  for(j=0; j<per_pause; ++j) {
	    EUCLID_SET_GATE(loop_offset + step, 1);
	    if( ++processed_steps >= steps )
	      break;
	    step = (step + 1) % steps;
	  }

	  if( processed_steps >= steps )
	    break;

	  if( i < remainder ) {
	    EUCLID_SET_GATE(loop_offset + step, 1);
	    if( ++processed_steps >= steps )
	      break;
	    step = (step + 1) % steps;
	  }

	  if( processed_steps >= steps )
	    break;
	}
      }
    }
  }

  SEQ_CORE_CancelSustainedNotes(track);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Cellular Automaton Generator (elementary 1D Wolfram CA)
//
// rule:     0..255, Wolfram numbering. Each bit selects output for one of the
//           eight 3-neighbor input patterns (LCR -> 4*L + 2*C + R).
// seed:     1..255, lower 8 bits seed the first 8 cells of generation 0; the
//           rest start zero.
// gens:     0..32 generations to advance; 0 = the seed row itself.
// boundary: CA_BND_WRAP / CA_BND_ZERO / CA_BND_MIRROR. Determines the
//           neighbors at cell indices 0 and cells-1.
/////////////////////////////////////////////////////////////////////////////
static s32 CAGenerator(u8 track, u8 rule, u8 seed, u8 gens, u8 boundary)
{
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;
  u8 modify_par_layer = event_mode != SEQ_EVENT_MODE_Drum && ui_selected_par_layer > 0;

  if( num_steps == 0 )
    return 0;

  // Two row buffers sized to the max we expect. SEQ_TRG_NumStepsGet returns
  // at most 256 for the largest trigger layer config.
  #define CA_MAX_CELLS 256
  static u8 row_a[CA_MAX_CELLS];
  static u8 row_b[CA_MAX_CELLS];

  u16 cells = (num_steps > CA_MAX_CELLS) ? CA_MAX_CELLS : num_steps;

  // Seed: lower 8 bits of `seed` populate cells [0..min(8,cells)).
  {
    u16 i;
    for(i=0; i<cells; ++i)
      row_a[i] = 0;
    u16 seed_bits = (cells < 8) ? cells : 8;
    for(i=0; i<seed_bits; ++i)
      row_a[i] = (seed >> i) & 1;
  }

  u8 *cur = row_a;
  u8 *nxt = row_b;

  {
    u8 g;
    for(g=0; g<gens; ++g) {
      u16 i;
      for(i=0; i<cells; ++i) {
        u8 l, r;
        if( i == 0 ) {
          switch( boundary ) {
            case CA_BND_ZERO:   l = 0; break;
            case CA_BND_MIRROR: l = cur[0]; break;  // mirror: neighbor = self
            case CA_BND_WRAP:
            default:            l = cur[cells-1]; break;
          }
        } else l = cur[i-1];
        if( i == cells-1 ) {
          switch( boundary ) {
            case CA_BND_ZERO:   r = 0; break;
            case CA_BND_MIRROR: r = cur[cells-1]; break;
            case CA_BND_WRAP:
            default:            r = cur[0]; break;
          }
        } else r = cur[i+1];
        u8 c = cur[i];
        u8 pattern = (l << 2) | (c << 1) | r;
        nxt[i] = (rule >> pattern) & 1;
      }
      u8 *tmp = cur; cur = nxt; nxt = tmp;
    }
  }

  // Write `cur` out to the active gate destination. Mirror EuclidGenerator's
  // write-target choice (trg layer vs par layer).
  {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      u8 gate = cur[step % cells];
      if( modify_par_layer ) {
        SEQ_PAR_Set(track, step, ui_selected_par_layer, instrument,
                    gate ? par_val[ui_selected_par_layer] : 0);
      } else {
        SEQ_TRG_GateSet(track, step, instrument, gate);
      }
    }
  }

  SEQ_CORE_CancelSustainedNotes(track);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Shared write helper: choose trg layer vs par layer the same way the
// EuclidGenerator's macro does. Returns the par-layer-fill value when the
// gate is on (par layer mode), or just sets the trg gate (trg layer mode).
/////////////////////////////////////////////////////////////////////////////
static s32 WriteGateAtStep(u8 track, u16 step, u8 gate, u8 modify_par_layer, u8 instrument)
{
  if( modify_par_layer ) {
    SEQ_PAR_Set(track, step, ui_selected_par_layer, instrument,
                gate ? par_val[ui_selected_par_layer] : 0);
  } else {
    SEQ_TRG_GateSet(track, step, instrument, gate);
  }
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Polyrhythm / Crossbeat generator
//
// n/m/phase    : primary cycle. n pulses per m steps, starting at phase.
// n2/m2/phase2 : optional 2nd cycle OR'd onto the primary. n2=0 disables it.
//
// We use the Bresenham-style "n out of m" distribution: step k of the cycle
// is a pulse iff floor((k+1)*n / m) > floor(k*n / m). The pattern tiles
// across the full track length.
/////////////////////////////////////////////////////////////////////////////
static s32 PolyrhythmGenerator(u8 track, u8 n, u8 m, u8 phase, u8 n2, u8 m2, u8 phase2)
{
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;
  u8 modify_par_layer = event_mode != SEQ_EVENT_MODE_Drum && ui_selected_par_layer > 0;

  if( num_steps == 0 || m == 0 ) return 0;
  if( n > m ) n = m;
  if( phase >= m ) phase = m - 1;

  u8 layer2_active = (n2 > 0 && m2 > 0);
  if( layer2_active ) {
    if( n2 > m2 ) n2 = m2;
    if( phase2 >= m2 ) phase2 = m2 - 1;
  }

  u16 step;
  for(step=0; step<num_steps; ++step) {
    u16 k = (step + (m - phase)) % m;
    u8 a = ((u32)(k + 1) * n) / m;
    u8 b = ((u32)k * n) / m;
    u8 gate = (a > b) ? 1 : 0;
    if( !gate && layer2_active ) {
      u16 k2 = (step + (m2 - phase2)) % m2;
      u8 a2 = ((u32)(k2 + 1) * n2) / m2;
      u8 b2 = ((u32)k2 * n2) / m2;
      gate = (a2 > b2) ? 1 : 0;
    }
    WriteGateAtStep(track, step, gate, modify_par_layer, instrument);
  }

  SEQ_CORE_CancelSustainedNotes(track);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Recursive subdivide generator ("fractal hi-hat")
//
// depth : 1..8 binary subdivision levels (2^depth cells per cycle).
// prob  : 0..100 percent that a cell stays alive at each level.
// seed  : 1..255 xorshift8 seed.
//
// We build a power-of-two array of size 2^depth. Start with one alive cell,
// then for each level split each cell into two halves; each half rolls a
// random number against `prob` (xorshift). The result tiles across the full
// track length.
/////////////////////////////////////////////////////////////////////////////
static s32 SubdivideGenerator(u8 track, u8 depth, u8 prob, u8 seed)
{
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;
  u8 modify_par_layer = event_mode != SEQ_EVENT_MODE_Drum && ui_selected_par_layer > 0;

  if( num_steps == 0 ) return 0;
  if( depth < 1 ) depth = 1;
  if( depth > 8 ) depth = 8;
  if( prob > 100 ) prob = 100;
  if( seed == 0 ) seed = 1;

  #define SUB_MAX_CELLS 256  // 2^8
  static u8 cells[SUB_MAX_CELLS];

  u16 size = 1u << depth;
  // start: a single alive cell at position 0
  cells[0] = 1;
  {
    u16 i;
    for(i=1; i<size; ++i) cells[i] = 0;
  }

  // xorshift8 PRNG: kept local so the live xorshift32 in seq_random isn't
  // perturbed by UI-driven regenerations.
  u8 rng = seed;
  #define SUB_XSHIFT8() do { rng ^= rng << 7; rng ^= rng >> 5; rng ^= rng << 3; if( rng == 0 ) rng = 1; } while(0)

  // level-by-level subdivide
  {
    u16 cur_size = 1;
    while( cur_size < size ) {
      u16 next_size = cur_size << 1;
      // walk from the right so writes don't clobber reads
      int i;
      for(i = cur_size - 1; i >= 0; --i) {
        u8 alive = cells[i];
        // left half inherits aliveness gated by prob
        SUB_XSHIFT8();
        u8 left  = alive && ((u32)rng * 100u < (u32)prob * 256u);
        SUB_XSHIFT8();
        u8 right = alive && ((u32)rng * 100u < (u32)prob * 256u);
        cells[2*i]     = left;
        cells[2*i + 1] = right;
      }
      cur_size = next_size;
    }
  }

  // write out, tiling across num_steps
  {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      u8 gate = cells[step % size];
      WriteGateAtStep(track, step, gate, modify_par_layer, instrument);
    }
  }

  SEQ_CORE_CancelSustainedNotes(track);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// L-system generator
//
// preset : 0..7 picks a (axiom, rules) pair from a small table.
// iter   : 0..6 expansion depth.
// seed   : 1..255 rotates the starting read offset of the final string,
//          giving variants without needing a rule-edit UI.
//
// Rules use a 2-symbol alphabet: '1' = gate on, '0' = gate off. Each preset
// rewrites both '1' and '0' into a short replacement string. To stay within
// a fixed buffer we cap the expanded length at L_BUF_MAX and stop expanding
// once we'd overflow.
/////////////////////////////////////////////////////////////////////////////
static s32 LSystemGenerator(u8 track, u8 preset, u8 iter, u8 seed)
{
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;
  u8 modify_par_layer = event_mode != SEQ_EVENT_MODE_Drum && ui_selected_par_layer > 0;

  if( num_steps == 0 ) return 0;

  // (axiom, rule for '1', rule for '0')
  // 8 presets, chosen for distinct gate-density / clumping textures.
  static const char * const lsys_table[8][3] = {
    { "1",   "10",     "01"     }, // 0: Thue-Morse-ish
    { "1",   "110",    "0"      }, // 1: Fibonacci-like growth
    { "10",  "101",    "010"    }, // 2: Cantor expansion
    { "1",   "1101",   "0010"   }, // 3: dense alternating
    { "10",  "1",      "10"     }, // 4: simple alternation
    { "1",   "1001",   "0110"   }, // 5: symmetric splits
    { "1",   "11010",  "00101"  }, // 6: dense complement
    { "10",  "10110",  "01001"  }, // 7: long-period mixed
  };

  if( preset >= 8 ) preset = 7;
  if( iter > 6 ) iter = 6;
  if( seed == 0 ) seed = 1;

  #define L_BUF_MAX 512
  static char buf_a[L_BUF_MAX];
  static char buf_b[L_BUF_MAX];

  // seed buffer with the axiom
  const char *axiom = lsys_table[preset][0];
  u16 len_a = 0;
  while( axiom[len_a] && len_a < L_BUF_MAX-1 ) {
    buf_a[len_a] = axiom[len_a];
    ++len_a;
  }
  buf_a[len_a] = 0;

  const char *rule_one  = lsys_table[preset][1];
  const char *rule_zero = lsys_table[preset][2];
  u16 rl1 = 0; while( rule_one[rl1]  ) ++rl1;
  u16 rl0 = 0; while( rule_zero[rl0] ) ++rl0;

  char *cur = buf_a;
  char *nxt = buf_b;
  u16 cur_len = len_a;

  // expand `iter` times, stopping early if the next pass would overflow
  {
    u8 it;
    for(it=0; it<iter; ++it) {
      // pre-check: worst-case next length
      u16 max_next = (u16)cur_len * ((rl1 > rl0) ? rl1 : rl0);
      if( max_next >= L_BUF_MAX ) break;

      u16 out = 0;
      u16 i;
      for(i=0; i<cur_len && out + ((cur[i] == '1') ? rl1 : rl0) < L_BUF_MAX; ++i) {
        const char *r = (cur[i] == '1') ? rule_one : rule_zero;
        u16 j;
        for(j=0; r[j]; ++j) nxt[out++] = r[j];
      }
      nxt[out] = 0;
      cur_len = out;
      char *t = cur; cur = nxt; nxt = t;
    }
  }

  if( cur_len == 0 ) {
    // degenerate: silence the track
    u16 step;
    for(step=0; step<num_steps; ++step)
      WriteGateAtStep(track, step, 0, modify_par_layer, instrument);
    SEQ_CORE_CancelSustainedNotes(track);
    return 0;
  }

  // seed rotates the starting read offset for variety
  u16 start = seed % cur_len;
  {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      u8 gate = (cur[(start + step) % cur_len] == '1') ? 1 : 0;
      WriteGateAtStep(track, step, gate, modify_par_layer, instrument);
    }
  }

  SEQ_CORE_CancelSustainedNotes(track);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Dispatch to whichever generator is selected for the given layer.
/////////////////////////////////////////////////////////////////////////////
static s32 RunActiveGenerator(u8 track, u8 layer)
{
  switch( gen_type[layer] ) {
    case GEN_TYPE_CA:
      return CAGenerator(track, ca_rule[layer], ca_seed[layer], ca_gens[layer], ca_boundary[layer]);
    case GEN_TYPE_POLY:
      return PolyrhythmGenerator(track, poly_n[layer], poly_m[layer], poly_phase[layer],
                                 poly_n2[layer], poly_m2[layer], poly_phase2[layer]);
    case GEN_TYPE_SUB:
      return SubdivideGenerator(track, sub_depth[layer], sub_prob[layer], sub_seed[layer]);
    case GEN_TYPE_LSYS:
      return LSystemGenerator(track, lsys_preset[layer], lsys_iter[layer], lsys_seed[layer]);
    case GEN_TYPE_EUCLID:
    default:
      return EuclidGenerator(track, (int)euclid_length[layer]+1,
                             euclid_pulses[layer], euclid_offset[layer]);
  }
}


/////////////////////////////////////////////////////////////////////////////
// Random Accent Generator
/////////////////////////////////////////////////////////////////////////////
static s32 AccentGenerator(u8 track, u16 steps)
{
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;

  int loop_offset; // fill whole track with repeating pattern
  seq_cc_trk_t *tcc = &seq_cc_trk[track];    

  for(loop_offset=0; loop_offset<num_steps; loop_offset += steps) {
    u8 step;
    for(step=0; step<steps; ++step) {
      if( loop_offset == 0 ) {
	// generate random accent
	u8 rnd = SEQ_RANDOM_Gen_Range(0, 100);
	u8 accent = rnd < rnd_acc_probability;

	if( tcc->link_par_layer_velocity >= 0 ) {
	  SEQ_PAR_Set(track, step, tcc->link_par_layer_velocity, instrument,
		      accent ? rnd_acc_a : rnd_acc_n);
	} else {
	  SEQ_TRG_AccentSet(track, step, instrument, accent);
	}
      } else {
	// copy first loop to remaining loop ranges
	if( tcc->link_par_layer_velocity >= 0 ) {
	  SEQ_PAR_Set(track, loop_offset+step, tcc->link_par_layer_velocity, instrument,
		      SEQ_PAR_Get(track, step, tcc->link_par_layer_velocity, instrument));
	} else {
	  SEQ_TRG_AccentSet(track, loop_offset+step, instrument,
			    SEQ_TRG_AccentGet(track, step, instrument));
	}
      }
    }
  }

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Call this function whenever the velocity value has been changed
/////////////////////////////////////////////////////////////////////////////
static s32 ReGenAccent(u8 track, u8 prev_rnd_acc_n, u8 prev_rnd_acc_a)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];    
  if( tcc->link_par_layer_velocity < 0 )
    return 0; // no need to re-generate

  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  u16 num_steps = SEQ_TRG_NumStepsGet(track);
  u8 instrument = (event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;

  {
    u16 step;
    for(step=0; step<num_steps; ++step) {
      // was step accented?
      u8 vel = SEQ_PAR_Get(track, step, tcc->link_par_layer_velocity, instrument);
      u8 accent = (vel >= prev_rnd_acc_a) && (vel != prev_rnd_acc_n);

      SEQ_PAR_Set(track, step, tcc->link_par_layer_velocity, instrument,
		  accent ? rnd_acc_a : rnd_acc_n);
    }
  }

  return 0; // no error
}
