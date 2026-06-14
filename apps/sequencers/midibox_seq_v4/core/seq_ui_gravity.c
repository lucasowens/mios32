// $Id$
/*
 * Tension Workbench — GRAVITY cockpit page (design doc §5; plan 2026-06-09 §3)
 *
 * The performance face of the GRAVITY field. One bipolar dial (GRAVITY, global,
 * −64..+63, center 0 = pass-through) pulls the room toward the chord root/scale
 * or pushes it into structured dissonance; per-track GRIP sets how strongly each
 * track is held; SHADE moves the global scale along a parallel-mode brightness
 * ladder; RESOLVE returns GRAVITY to the detent.
 *
 *   GP1 enc = GRAVITY (global)   GP2 enc = SHADE (brightness ladder)
 *   GP3 enc = GRIP (visible trk) GP4 enc = track select
 *   GP8 btn = RESOLVE            GP16 btn = toggle to FX_SCALE (harmonic sibling)
 *   datawheel / Up / Down = selected item
 *
 * Harmonic sibling of ROBOLOOP — structurally cloned from seq_ui_robomold.c.
 * Physical GP allocation is provisional (plan §3/§A3: decided at the panel).
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
#include <string.h>
#include "tasks.h"
#include "seq_lcd.h"
#include "seq_ui.h"
#include "seq_cc.h"
#include "seq_core.h"
#include "seq_file.h"
#include "seq_file_c.h"


/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

#define NUM_OF_ITEMS    4
#define ITEM_GRAVITY    0
#define ITEM_SHADE      1
#define ITEM_GRIP       2
#define ITEM_GXTY       3


/////////////////////////////////////////////////////////////////////////////
// SHADE ladder — parallel modes, brightest → darkest, root fixed (plan §3).
// Indices into seq_scale_table: Lydian=15, Ionian=12, Mixolydian=16, Dorian=13,
// Aeolian=17, Phrygian=14, Locrian=18 (verified against seq_scale.c).
/////////////////////////////////////////////////////////////////////////////
static const u8   shade_ladder[7] = { 15, 12, 16, 13, 17, 14, 18 };
static const char shade_names[7][4] = { "Lyd", "Ion", "Mix", "Dor", "Aeo", "Phr", "Loc" };


static const char *zone_name(s8 g)
{
  if( g == 0 ) return "DETENT";
  if( g < 0 )  return (g >= -24) ? "SCALE" : (g >= -48) ? "CHORD" : "DRONE";
  return (g <= 24) ? "LEAN" : (g <= 48) ? "RUB" : "SLIP";
}

static const char *shade_label(void)
{
  u8 i;
  for(i=0; i<7; ++i)
    if( shade_ladder[i] == seq_core_global_scale )
      return shade_names[i];
  return "-- ";  // global scale is off the brightness ladder
}

// Bipolar GRAVITY meter (encoders have no felt detent — this is the positional
// feedback). 27 cells: center '|' = the detent (home); fill grows LEFT as you
// pull, RIGHT as you push, length ∝ |GRAVITY|. Zone boundaries are tick marks
// (':' on the empty track, '+' once you've swept past them) at ±24/±48 — so you
// see how deep you are AND which character-zone you're in. buf must be >= 28.
#define TENSION_METER_CELLS   27
#define TENSION_METER_CENTER  13
#define TENSION_METER_PERSIDE 13
static void tension_meter(s8 g, char *buf)
{
  int i;
  for(i = 0; i < TENSION_METER_CELLS; ++i)
    buf[i] = '.';
  buf[TENSION_METER_CENTER] = '|';

  // zone-boundary ticks: ±24/±48 map to ~5 / ~10 cells from center
  const int tick_off[2] = { 5, 10 };
  for(i = 0; i < 2; ++i) {
    buf[TENSION_METER_CENTER - tick_off[i]] = ':';
    buf[TENSION_METER_CENTER + tick_off[i]] = ':';
  }

  int mag = (g < 0) ? -(int)g : (int)g;            // 0..64
  int cur = (mag * TENSION_METER_PERSIDE + 32) / 64;  // rounded cells from center
  if( cur > TENSION_METER_PERSIDE ) cur = TENSION_METER_PERSIDE;

  if( g < 0 ) {
    for(i = TENSION_METER_CENTER - cur; i < TENSION_METER_CENTER; ++i)
      if( i >= 0 ) buf[i] = (buf[i] == ':') ? '+' : '#';
  } else if( g > 0 ) {
    for(i = TENSION_METER_CENTER + 1; i <= TENSION_METER_CENTER + cur; ++i)
      if( i < TENSION_METER_CELLS ) buf[i] = (buf[i] == ':') ? '+' : '#';
  }
  buf[TENSION_METER_CELLS] = 0;
}


/////////////////////////////////////////////////////////////////////////////
// Local LED handler function
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  if( ui_cursor_flash )
    return 0;

  switch( ui_selected_item ) {
    case ITEM_GRAVITY: *gp_leds = 0x0001; break;
    case ITEM_SHADE:   *gp_leds = 0x0002; break;
    case ITEM_GRIP:    *gp_leds = 0x0004; break;
    case ITEM_GXTY:    *gp_leds = 0x0008; break;
  }
  *gp_leds |= 0x0080; // GP8 — RESOLVE button hint

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Local encoder callback function
/////////////////////////////////////////////////////////////////////////////
static s32 Encoder_Handler(seq_ui_encoder_t encoder, s32 incrementer)
{
  switch( encoder ) {
    case SEQ_UI_ENCODER_GP1: ui_selected_item = ITEM_GRAVITY; break;
    case SEQ_UI_ENCODER_GP2: ui_selected_item = ITEM_SHADE;   break;
    case SEQ_UI_ENCODER_GP3: ui_selected_item = ITEM_GRIP;    break;
    case SEQ_UI_ENCODER_GP4: ui_selected_item = ITEM_GXTY;    break;

    case SEQ_UI_ENCODER_GP5:
    case SEQ_UI_ENCODER_GP6:
    case SEQ_UI_ENCODER_GP7:
    case SEQ_UI_ENCODER_GP8:
    case SEQ_UI_ENCODER_GP9:
    case SEQ_UI_ENCODER_GP10:
    case SEQ_UI_ENCODER_GP11:
    case SEQ_UI_ENCODER_GP12:
    case SEQ_UI_ENCODER_GP13:
    case SEQ_UI_ENCODER_GP14:
    case SEQ_UI_ENCODER_GP15:
    case SEQ_UI_ENCODER_GP16:
      return 0;

    default:
      break;
  }

  switch( ui_selected_item ) {
    case ITEM_GRAVITY: {
      s32 g = (s32)seq_core_tension_gravity + incrementer;
      if( g < -64 ) g = -64;
      if( g >  63 ) g =  63;
      if( (s8)g == seq_core_tension_gravity )
        return 0;
      SEQ_CORE_TensionResolveCancel();  // a manual turn aborts an in-flight RESOLVE
      SEQ_CORE_TensionGravitySet((s8)g);
      return 1;
    }

    case ITEM_SHADE: {
      int pos = -1, i;
      for(i=0; i<7; ++i)
        if( shade_ladder[i] == seq_core_global_scale ) { pos = i; break; }
      if( pos < 0 )
        pos = (incrementer > 0) ? 0 : 6;  // jump onto the ladder from off-list
      else {
        pos += (incrementer > 0) ? 1 : -1;
        if( pos < 0 ) pos = 0;
        if( pos > 6 ) pos = 6;
      }
      if( shade_ladder[pos] == seq_core_global_scale )
        return 0;
      seq_core_global_scale = shade_ladder[pos];
      ui_store_file_required = 1;       // SHADE is performance state → config file
      SEQ_CORE_RenderDirtySetAll();     // scale move re-renders FTS + tension L3
      return 1;
    }

    case ITEM_GRIP:
      // per-track GRIP; SEQ_CC_Set → TensionSlotSync arms/updates the slot
      return SEQ_UI_CC_Inc(SEQ_CC_TENSION_GRIP, 0, 127, incrementer);

    case ITEM_GXTY:
      return SEQ_UI_GxTyInc(incrementer);
  }

  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Local button callback function
/////////////////////////////////////////////////////////////////////////////
static s32 Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( depressed ) return 0;

  switch( button ) {
    // GP8: RESOLVE — bar-quantized ramp of GRAVITY to the detent, landing on the
    // next downbeat (instant when stopped). §3 cadence.
    case SEQ_UI_BUTTON_GP8:
      SEQ_CORE_TensionResolve();
      SEQ_UI_Msg_Track("RESOLVE -> the One");
      return 1;

    case SEQ_UI_BUTTON_GP1: ui_selected_item = ITEM_GRAVITY; return 1;
    case SEQ_UI_BUTTON_GP2: ui_selected_item = ITEM_SHADE;   return 1;
    case SEQ_UI_BUTTON_GP3: ui_selected_item = ITEM_GRIP;    return 1;
    case SEQ_UI_BUTTON_GP4: ui_selected_item = ITEM_GXTY;    return 1;

    case SEQ_UI_BUTTON_GP5:
    case SEQ_UI_BUTTON_GP6:
    case SEQ_UI_BUTTON_GP7:
    case SEQ_UI_BUTTON_GP9:
    case SEQ_UI_BUTTON_GP10:
    case SEQ_UI_BUTTON_GP11:
    case SEQ_UI_BUTTON_GP12:
    case SEQ_UI_BUTTON_GP13:
    case SEQ_UI_BUTTON_GP14:
    case SEQ_UI_BUTTON_GP15:
      return 0;

    // GP16: toggle to the harmonic-sibling page (FX_SCALE), mirroring the
    // FX_ROBOTIZE ↔ ROBOLOOP pattern.
    case SEQ_UI_BUTTON_GP16:
      SEQ_UI_PageSet(SEQ_UI_PAGE_FX_SCALE);
      return 1;

    case SEQ_UI_BUTTON_Select:
    case SEQ_UI_BUTTON_Right:
      if( ++ui_selected_item >= NUM_OF_ITEMS )
        ui_selected_item = 0;
      return 1;

    case SEQ_UI_BUTTON_Left:
      if( ui_selected_item == 0 )
        ui_selected_item = NUM_OF_ITEMS - 1;
      else
        --ui_selected_item;
      return 1;

    case SEQ_UI_BUTTON_Up:
      return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, 1);

    case SEQ_UI_BUTTON_Down:
      return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, -1);

    default:
      break;
  }

  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Local Display Handler function
//
// TWO physically separate 2x40 LCDs — strings must never cross col 40.
//
//   Cols 0..39 (LCD 1)                        Cols 40..79 (LCD 2)
//   Row 0: |Grav Shad Grip Trk.          Reslv|Field zone: DRONE   grav -60   |
//   Row 1: | -60 Aeo  127  G1T1          (snap)|oOO#............  vis G1T1 g127 |
//          GP1   GP2  GP3  GP4 ...   GP8       16-char per-track grip bar
/////////////////////////////////////////////////////////////////////////////
static s32 LCD_Handler(u8 high_prio)
{
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  s8 g = seq_core_tension_gravity;

  ///////////////////////////////////////////////////////////////////////////
  // LCD 1 - Row 0: labels (40 chars).
  SEQ_LCD_CursorSet(0, 0);
  SEQ_LCD_PrintString("Grav Shad Grip Trk.");  // 19
  SEQ_LCD_PrintSpaces(16);                      // 35
  SEQ_LCD_PrintString("Reslv");                 // 40

  ///////////////////////////////////////////////////////////////////////////
  // LCD 1 - Row 1: values (40 chars, 5 per GP slot).
  SEQ_LCD_CursorSet(0, 1);

  // GP1: GRAVITY (signed). The reduced LCD printf has no '+' flag, so render the
  // sign as %c + magnitude as %2d (the codebase idiom).
  if( ui_selected_item == ITEM_GRAVITY && ui_cursor_flash )
    SEQ_LCD_PrintSpaces(5);
  else
    SEQ_LCD_PrintFormattedString("%c%2d  ", (g < 0) ? '-' : '+', (g < 0) ? -(int)g : (int)g);

  // GP2: SHADE (mode name)
  if( ui_selected_item == ITEM_SHADE && ui_cursor_flash )
    SEQ_LCD_PrintSpaces(5);
  else
    SEQ_LCD_PrintFormattedString("%-4s ", shade_label());

  // GP3: GRIP (visible track)
  if( ui_selected_item == ITEM_GRIP && ui_cursor_flash )
    SEQ_LCD_PrintSpaces(5);
  else
    SEQ_LCD_PrintFormattedString("%3d  ", seq_cc_trk[visible_track].tension_grip);

  // GP4: track
  if( ui_selected_item == ITEM_GXTY && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintGxTy(ui_selected_group, ui_selected_tracks);  // 4
    SEQ_LCD_PrintSpaces(1);
  }

  SEQ_LCD_PrintSpaces(15);  // GP5..7
  SEQ_LCD_PrintSpaces(5);   // GP8 (RESOLVE — label in row 0 is the cue)

  ///////////////////////////////////////////////////////////////////////////
  // LCD 2 - Row 0: zone + value + the bipolar GRAVITY meter (40 chars).
  SEQ_LCD_CursorSet(40, 0);
  {
    char meter[TENSION_METER_CELLS + 1];
    tension_meter(g, meter);
    SEQ_LCD_PrintFormattedString("%-6s %c%2d ", zone_name(g),
                                 (g < 0) ? '-' : '+', (g < 0) ? -(int)g : (int)g);  // 11
    SEQ_LCD_PrintString(meter);                                    // +27 = 38
    SEQ_LCD_PrintSpaces(2);                                        // 40
  }

  ///////////////////////////////////////////////////////////////////////////
  // LCD 2 - Row 1: 16-char per-track GRIP bar + visible-track read-out.
  SEQ_LCD_CursorSet(40, 1);
  {
    char bar[17];
    u8 i;
    for(i=0; i<16; ++i) {
      u8 gp = seq_cc_trk[i].tension_grip;
      bar[i] = gp ? (gp < 43 ? 'o' : (gp < 86 ? 'O' : '#')) : '.';
    }
    bar[16] = 0;
    SEQ_LCD_PrintString(bar);   // 16
    SEQ_LCD_PrintSpaces(2);     // 18
    SEQ_LCD_PrintString("vis ");// 22
    SEQ_LCD_PrintGxTy(ui_selected_group, ui_selected_tracks);             // 26
    SEQ_LCD_PrintFormattedString(" g%3d", seq_cc_trk[visible_track].tension_grip); // 31
    SEQ_LCD_PrintSpaces(9);     // 40
  }

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Exit: persist SHADE (the global scale) to the session config file, mirroring
// every other store-file page (fx_scale, trkgrv, ...). Without this the page
// set ui_store_file_required but nothing wrote it, so SHADE was silently lost
// on the next PageSet (PageSet zeroes the flag).
/////////////////////////////////////////////////////////////////////////////
static s32 EXIT_Handler(void)
{
  s32 status = 0;

  if( ui_store_file_required ) {
    MUTEX_SDCARD_TAKE;
    if( (status=SEQ_FILE_C_Write(seq_file_session_name)) < 0 )
      SEQ_UI_SDCardErrMsg(2000, status);
    MUTEX_SDCARD_GIVE;

    ui_store_file_required = 0;
  }

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_GRAVITY_Init(u32 mode)
{
  SEQ_UI_InstallButtonCallback(Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);
  SEQ_UI_InstallExitCallback(EXIT_Handler);

  return 0;
}
