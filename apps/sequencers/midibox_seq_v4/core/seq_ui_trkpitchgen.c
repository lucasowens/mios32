// $Id$
/*
 * Pitch Generator page — phase E spine form.
 *
 * GP1 ENGAGE toggle (LED reflects engaged state).
 * GP2 UNDO (restores pre-engage par snapshot, disengages every gen on that
 *     track).
 * GP3 Range min   (encoder; clamped to <= range_max).
 * GP4 Range max   (encoder; clamped to >= range_min).
 * GP5 Mutation rate 0..127 (encoder; the §5 journey dial, sweepable live).
 * GP6..GP8 reserved for §8 step 6 (LOCK / MULT / ROLL / SNAP).
 *
 * Target = active drum on the visible track: (track = SEQ_UI_VisibleTrackGet,
 *           instrument = ui_selected_instrument). Drum-mode + Note par-layer
 *           assignment required, else ENGAGE refuses with a guidance message.
 *
 * ==========================================================================
 */

#include <mios32.h>
#include "seq_lcd.h"
#include "seq_ui.h"

#include "seq_core.h"
#include "seq_cc.h"
#include "seq_layer.h"
#include "seq_par.h"
#include "seq_generator.h"


/////////////////////////////////////////////////////////////////////////////
// LED handler — GP1 lit while engaged.
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  if( SEQ_GENERATOR_IsEngaged(track, ui_selected_instrument) )
    *gp_leds = (1 << 0);
  else
    *gp_leds = 0;
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Range / rate encoder dispatch.
/////////////////////////////////////////////////////////////////////////////
static s32 Encoder_Handler(seq_ui_encoder_t encoder, s32 incrementer)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  seq_generator_t *g = SEQ_GENERATOR_Get(track, ui_selected_instrument);

  // No allocated slot ⇒ nothing to tune yet. Encoders no-op until ENGAGE.
  if( g == NULL ) return -1;

  switch( encoder ) {
    case SEQ_UI_ENCODER_GP3: {
      u8 v = g->range_min;
      s32 r = SEQ_UI_Var8_Inc(&v, 1, 127, incrementer);
      if( v > g->range_max ) v = g->range_max;
      g->range_min = v;
      return r;
    }
    case SEQ_UI_ENCODER_GP4: {
      u8 v = g->range_max;
      s32 r = SEQ_UI_Var8_Inc(&v, 1, 127, incrementer);
      if( v < g->range_min ) v = g->range_min;
      g->range_max = v;
      return r;
    }
    case SEQ_UI_ENCODER_GP5: {
      u8 v = g->mutation_rate;
      s32 r = SEQ_UI_Var8_Inc(&v, 0, 127, incrementer);
      g->mutation_rate = v;
      return r;
    }
    default:
      break;
  }
  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Buttons — GP1 ENGAGE/DISENGAGE toggle, GP2 UNDO.
/////////////////////////////////////////////////////////////////////////////
static s32 Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( depressed ) return 0;

  u8 track = SEQ_UI_VisibleTrackGet();
  u8 instr = ui_selected_instrument;

  if( button == SEQ_UI_BUTTON_GP1 ) {
    if( SEQ_GENERATOR_IsEngaged(track, instr) ) {
      SEQ_GENERATOR_Disengage(track, instr);
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "DISENGAGED");
    } else {
      s32 r = SEQ_GENERATOR_Engage(track, instr);
      switch( r ) {
        case 0:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "ENGAGED");
          break;
        case -1:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "pool full (64/64)");
          break;
        case -2:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "needs drum-mode track");
          break;
        case -3:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "assign Note par-layer");
          break;
        default:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "ENGAGE failed");
      }
    }
    return 1;
  }

  if( button == SEQ_UI_BUTTON_GP2 ) {
    if( SEQ_GENERATOR_Undo() == 0 )
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "UNDO restored");
    else
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1500, "Pitch Gen:", "no snapshot");
    return 1;
  }

  if( button >= SEQ_UI_BUTTON_GP3 && button <= SEQ_UI_BUTTON_GP8 ) {
    return Encoder_Handler((seq_ui_encoder_t)button, 0);
  }

  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// LCD layout (2 lines x 40 chars per LCD, dual = 80 wide):
//
// 00000000001111111111222222222233333333330000000000111111111122222222223333333333
// 01234567890123456789012345678901234567890123456789012345678901234567890123456789
// <--------------------------------------><-------------------------------------->
// PITCH GEN  Trk 1.D 3   state:ENGAGED      GP1=ENGAGE GP2=UNDO  pool: 1/64
// RMin:C 2  RMax:C 6  Rate:  8              (drum-mode + Note par-layer required)
/////////////////////////////////////////////////////////////////////////////
static s32 LCD_Handler(u8 high_prio)
{
  if( high_prio )
    return 0;

  u8 track = SEQ_UI_VisibleTrackGet();
  u8 instr = ui_selected_instrument;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  u8 engaged = SEQ_GENERATOR_IsEngaged(track, instr);

  // Row 0
  SEQ_LCD_CursorSet(0, 0);
  SEQ_LCD_PrintFormattedString("PITCH GEN  Trk%2d.D%2d   ", track+1, instr+1);
  if( engaged )       SEQ_LCD_PrintString("state:ENGAGED     ");
  else if( g != NULL ) SEQ_LCD_PrintString("state:disengaged  ");
  else                 SEQ_LCD_PrintString("state:--          ");

  SEQ_LCD_CursorSet(40, 0);
  SEQ_LCD_PrintString("GP1=ENGAGE GP2=UNDO                     ");

  // Row 1
  SEQ_LCD_CursorSet(0, 1);
  if( g != NULL ) {
    SEQ_LCD_PrintString("RMin:");
    SEQ_LCD_PrintNote(g->range_min);
    SEQ_LCD_PrintString(" RMax:");
    SEQ_LCD_PrintNote(g->range_max);
    SEQ_LCD_PrintFormattedString(" Rate:%3d            ", g->mutation_rate);
  } else {
    SEQ_LCD_PrintString("RMin:--   RMax:--   Rate:---            ");
  }

  SEQ_LCD_CursorSet(40, 1);
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum )
    SEQ_LCD_PrintString("(needs drum-mode track)                 ");
  else if( tcc->link_par_layer_note < 0 )
    SEQ_LCD_PrintString("(assign Note in PAR-ASG first)          ");
  else if( g == NULL )
    SEQ_LCD_PrintString("press GP1 to ENGAGE                     ");
  else
    SEQ_LCD_PrintString("GP3/4=range  GP5=rate  GP2=UNDO         ");

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Init
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_TRKPITCHGEN_Init(u32 mode)
{
  SEQ_UI_InstallButtonCallback(Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);

  return 0;
}
