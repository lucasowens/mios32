// $Id$
/*
 * Pitch Generator Page (drum-track Note par-layer reroll, throwaway POC).
 *
 * §8 step 2: get a generative pitched drum line into the ears. Single gesture:
 * GP1 fills the active drum's Note par-layer with random pitches in [C2, C6].
 * No params, no seed, no contour, no lock — disposable; the spine (§8 step 5)
 * replaces this entire page.
 *
 * ==========================================================================
 */

#include <mios32.h>
#include "seq_lcd.h"
#include "seq_ui.h"

#include "seq_core.h"
#include "seq_cc.h"
#include "seq_par.h"
#include "seq_random.h"


#define PITCH_MIN 36   // C2
#define PITCH_MAX 84   // C6


/////////////////////////////////////////////////////////////////////////////
// Reroll the active drum's Note par-layer with random pitches.
// Returns 0 on success, -1 if track isn't drum mode, -2 if no Note layer.
/////////////////////////////////////////////////////////////////////////////
static s32 Reroll(void)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  if( tcc->event_mode != SEQ_EVENT_MODE_Drum )
    return -1;
  if( tcc->link_par_layer_note < 0 )
    return -2;

  u8 instrument = ui_selected_instrument;
  u8 par_layer = tcc->link_par_layer_note;
  int num_steps = SEQ_PAR_NumStepsGet(track);

  int step;
  for(step=0; step<num_steps; ++step) {
    u8 v = (u8)SEQ_RANDOM_Gen_Range(PITCH_MIN, PITCH_MAX);
    SEQ_PAR_Set(track, step, par_layer, instrument, v);
  }

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Local LED handler function
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  *gp_leds = (1 << 0); // GP1 hint
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Local encoder callback function
/////////////////////////////////////////////////////////////////////////////
static s32 Encoder_Handler(seq_ui_encoder_t encoder, s32 incrementer)
{
  return -1; // no encoder bindings in POC
}


/////////////////////////////////////////////////////////////////////////////
// Local button callback function
/////////////////////////////////////////////////////////////////////////////
static s32 Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( depressed ) return 0;

  if( button == SEQ_UI_BUTTON_GP1 ) {
    s32 r = Reroll();
    if( r == -1 )
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "needs drum-mode track");
    else if( r == -2 )
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "assign Note par-layer");
    else
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "rerolled");
    return 1;
  }

  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Local Display Handler function
// IN: <high_prio>: if set, a high-priority LCD update is requested
/////////////////////////////////////////////////////////////////////////////
static s32 LCD_Handler(u8 high_prio)
{
  if( high_prio )
    return 0;

  u8 track = SEQ_UI_VisibleTrackGet();
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  // layout (2x40, dual LCD shown as 80-char rows):
  // 00000000001111111111222222222233333333330000000000111111111122222222223333333333
  // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  // <--------------------------------------><-------------------------------------->
  //   PITCH GEN (POC)                          GP1 = REROLL active drum
  //   Trk1.D03  NoteLayer:D  Range C2-C6        Note layer assigned, ready

  SEQ_LCD_CursorSet(0, 0);
  SEQ_LCD_PrintString("  PITCH GEN (POC)                       ");
  SEQ_LCD_PrintString("   GP1 = REROLL active drum             ");

  SEQ_LCD_CursorSet(0, 1);
  SEQ_LCD_PrintFormattedString("  Trk%d.D%02d  ", track+1, ui_selected_instrument+1);

  if( tcc->event_mode != SEQ_EVENT_MODE_Drum ) {
    SEQ_LCD_PrintString("(non-drum track)             ");
    SEQ_LCD_PrintString("   needs drum-mode track                ");
  } else if( tcc->link_par_layer_note < 0 ) {
    SEQ_LCD_PrintString("NoteLayer:-  Range C2-C6     ");
    SEQ_LCD_PrintString("   assign Note in PAR-ASG first         ");
  } else {
    SEQ_LCD_PrintFormattedString("NoteLayer:%c  Range C2-C6     ", 'A' + tcc->link_par_layer_note);
    SEQ_LCD_PrintString("   ready                                ");
  }

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_TRKPITCHGEN_Init(u32 mode)
{
  SEQ_UI_InstallButtonCallback(Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);

  return 0; // no error
}
