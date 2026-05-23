// $Id$
/*
 * Robotize Loop / Mold page
 *
 * Controls the per-track robotize PRNG loop:
 *   - loop length in musical measures (0..16)
 *   - master-sync (re-align loop to song guide track wrap)
 *   - one-shot actions: reseed, freeze (jump-now), freeze (quantized)
 *   - per-measure reroll: SELECT-held + GP1..16 rerolls the corresponding
 *     measure anchor while leaving the others locked
 *
 * Sibling to FX_ROBOTIZE in the page list. Lives at SEQ_UI_PAGE_ROBOLOOP.
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
#include "seq_lcd.h"
#include "seq_ui.h"
#include "seq_cc.h"
#include "seq_core.h"
#include "seq_robotize.h"
#include "seq_capture.h"
#include "seq_pattern.h"


/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

#define NUM_OF_ITEMS         6
#define ITEM_GXTY            0
#define ITEM_PALETTE_LENGTH  1
#define ITEM_LOOP_START      2
#define ITEM_LOOP_CYCLES     3
#define ITEM_LOOP_ROTATE     4
#define ITEM_ACTIONS_CURSOR  5  // placeholder so Up/Down doesn't get stuck on an action button

// bounce dialog modes
#define DIALOG_IDLE          0
#define DIALOG_PICK_SLOT     1  // user picks destination via GP1..GP8

static u8 bounce_dialog = DIALOG_IDLE;
static u8 bounce_num_measures = 1;  // bounce length in measures (1..16)


/////////////////////////////////////////////////////////////////////////////
// Local LED handler function
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  u8 visible_track = SEQ_UI_VisibleTrackGet();

  // SELECT-held: show in-window anchors lit, currently-playing one flashing
  if( seq_ui_button_state.SELECT_PRESSED ) {
    u8 loop    = seq_cc_trk[visible_track].robotize_loop_cycles;
    u8 palette = seq_cc_trk[visible_track].robotize_palette_length;
    if( palette == 0 || palette > 16 ) palette = 16;
    u8 start   = seq_cc_trk[visible_track].robotize_loop_start;
    u8 rotate  = seq_cc_trk[visible_track].robotize_loop_rotate;
    u8 phase   = seq_core_trk[visible_track].robotize_loop_phase;

    u16 mask = 0;
    u8 j;
    for(j=0; j<loop; ++j)
      mask |= (1 << ((start + j) % palette));

    // flash the playing anchor
    if( loop && !ui_cursor_flash ) {
      u8 playing_idx = (start + ((rotate + phase) % loop)) % palette;
      mask &= ~(1 << playing_idx);
    }
    *gp_leds = mask;
    return 0;
  }

  if( ui_cursor_flash )
    return 0;

  switch( ui_selected_item ) {
    case ITEM_GXTY:           *gp_leds = 0x0001; break;
    case ITEM_PALETTE_LENGTH: *gp_leds = 0x0002; break;
    case ITEM_LOOP_START:     *gp_leds = 0x0004; break;
    case ITEM_LOOP_CYCLES:    *gp_leds = 0x0008; break;
    case ITEM_LOOP_ROTATE:    *gp_leds = 0x0010; break;
  }

  // also light the action-button positions as a hint
  *gp_leds |= 0x00E0; // GP6,7,8 - reseed/freeze/frzq
  *gp_leds |= 0x0100; // GP9 - bounce

  // when picker dialog active, also flash GP1..8 hint
  if( bounce_dialog == DIALOG_PICK_SLOT && ui_cursor_flash )
    *gp_leds |= 0x00FF;

  return 0;
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
  // SELECT-held: encoders are inactive in reroll mode (rerolling on rotation
  // would be chaotic - require explicit button press)
  if( seq_ui_button_state.SELECT_PRESSED )
    return 0;

  switch( encoder ) {
    case SEQ_UI_ENCODER_GP1: ui_selected_item = ITEM_GXTY;           break;
    case SEQ_UI_ENCODER_GP2: ui_selected_item = ITEM_PALETTE_LENGTH; break;
    case SEQ_UI_ENCODER_GP3: ui_selected_item = ITEM_LOOP_START;     break;
    case SEQ_UI_ENCODER_GP4: ui_selected_item = ITEM_LOOP_CYCLES;    break;
    case SEQ_UI_ENCODER_GP5: ui_selected_item = ITEM_LOOP_ROTATE;    break;

    // GP6..8 are action buttons; encoders don't change them
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
    case ITEM_GXTY:
      return SEQ_UI_GxTyInc(incrementer);

    case ITEM_PALETTE_LENGTH:
      return SEQ_UI_CC_Inc(SEQ_CC_ROBOTIZE_PALETTE_LENGTH, 1, 16, incrementer);

    case ITEM_LOOP_START: {
      s32 r = SEQ_UI_CC_Inc(SEQ_CC_ROBOTIZE_LOOP_START, 0, 15, incrementer);
      if( r > 0 ) {
	// snap playback to the new window's head so the change is audible
	// immediately and start *feels* like sliding the window
	u8 t_idx = SEQ_UI_VisibleTrackGet();
	seq_cc_trk_t *tcc = &seq_cc_trk[t_idx];
	seq_core_trk_t *t = &seq_core_trk[t_idx];
	t->robotize_loop_phase = 0;
	if( tcc->robotize_loop_cycles ) {
	  u8 palette = tcc->robotize_palette_length;
	  if( palette == 0 || palette > 16 ) palette = 16;
	  u8 head_idx = (tcc->robotize_loop_start + tcc->robotize_loop_rotate) % palette;
	  t->robotize_seed_state = tcc->robotize_bar_anchors[head_idx];
	}
      }
      return r;
    }

    case ITEM_LOOP_CYCLES: {
      s32 r = SEQ_UI_CC_Inc(SEQ_CC_ROBOTIZE_LOOP_CYCLES, 0, 16, incrementer);
      if( r > 0 ) {
	// if the new window is shorter than current phase, snap phase back to 0
	u8 t_idx = SEQ_UI_VisibleTrackGet();
	seq_cc_trk_t *tcc = &seq_cc_trk[t_idx];
	seq_core_trk_t *t = &seq_core_trk[t_idx];
	if( tcc->robotize_loop_cycles && t->robotize_loop_phase >= tcc->robotize_loop_cycles )
	  t->robotize_loop_phase = 0;
      }
      return r;
    }

    case ITEM_LOOP_ROTATE:
      // rotate is meant for live in-window cycling - don't disturb phase
      return SEQ_UI_CC_Inc(SEQ_CC_ROBOTIZE_LOOP_ROTATE, 0, 15, incrementer);
  }

  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Bounce action: capture last `bounce_num_measures` of emitted MIDI on the
// visible track and write into `dst_pattern_idx` (0..7) within the current
// group's current bank. Source pattern is preserved.
/////////////////////////////////////////////////////////////////////////////
static s32 DoBounce(u8 visible_track, s32 dst_pattern_idx)
{
  u8 src_group = visible_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;

  if( dst_pattern_idx < 0 ) {
    // pick next-free slot, skipping the currently playing pattern
    dst_pattern_idx = SEQ_CAPTURE_FindNextFreeSlot(src_group, seq_pattern[src_group].pattern);
    if( dst_pattern_idx < 0 ) {
      // no free slot - open picker so user can choose explicitly
      bounce_dialog = DIALOG_PICK_SLOT;
      SEQ_UI_Msg_Track("pick slot 1-8");
      return -1;
    }
  }

  seq_pattern_t dst = seq_pattern[src_group];
  dst.pattern = dst_pattern_idx & 0x07;

  s32 status = SEQ_CAPTURE_CommitToSlot(visible_track, src_group, dst, bounce_num_measures);
  if( status < 0 ) {
    SEQ_UI_Msg_Track("bounce failed");
    return status;
  }

  char msg[16];
  // groups are A/B/C/D; pattern displayed 1-indexed for the user
  msg[0] = '>';
  msg[1] = 'A' + src_group;
  msg[2] = '1' + (dst_pattern_idx & 0x07);
  msg[3] = ' ';
  msg[4] = 'O';
  msg[5] = 'K';
  msg[6] = 0;
  SEQ_UI_Msg_Track(msg);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Local button callback function
/////////////////////////////////////////////////////////////////////////////
static s32 Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( depressed ) return 0; // ignore release

  u8 visible_track = SEQ_UI_VisibleTrackGet();

  // pick-slot dialog: consume GP1..GP8 as destination selection; any other
  // button cancels the dialog. SELECT-reroll path is suppressed here so the
  // user can pick without triggering the underlying reroll.
  if( bounce_dialog == DIALOG_PICK_SLOT ) {
    if( button <= SEQ_UI_BUTTON_GP8 ) {
      u8 dst = (u8)button;
      bounce_dialog = DIALOG_IDLE;
      DoBounce(visible_track, dst);
      return 1;
    }
    bounce_dialog = DIALOG_IDLE;
    SEQ_UI_Msg_Track("cancelled");
    return 1;
  }

  // SELECT-held: GP9 = open the bounce destination picker; other GPs
  // continue to reroll the corresponding measure anchor.
  if( seq_ui_button_state.SELECT_PRESSED ) {
    if( button == SEQ_UI_BUTTON_GP9 ) {
      bounce_dialog = DIALOG_PICK_SLOT;
      SEQ_UI_Msg_Track("pick slot 1-8");
      return 1;
    }
    if( button <= SEQ_UI_BUTTON_GP16 ) {
      u8 measure_idx = (u8)button; // GP1=0 ... GP16=15
      SEQ_ROBOTIZE_RerollBar(visible_track, measure_idx);
      SEQ_UI_Msg_Track("measure rerolled");
      return 1;
    }
  }

  // Normal mode: action buttons + item-select
  switch( button ) {
    case SEQ_UI_BUTTON_GP6: {
      SEQ_ROBOTIZE_Reseed(visible_track);
      SEQ_UI_Msg_Track("palette reseeded");
      return 1;
    }
    case SEQ_UI_BUTTON_GP7: {
      s32 r = SEQ_ROBOTIZE_Freeze(visible_track, 0);
      if( r == -2 )
        SEQ_UI_Msg_Track("set Loop>0 first");
      else
        SEQ_UI_Msg_Track("frozen (jump-now)");
      return 1;
    }
    case SEQ_UI_BUTTON_GP8: {
      s32 r = SEQ_ROBOTIZE_FreezeQuantized(visible_track, 0);
      if( r == -2 )
        SEQ_UI_Msg_Track("set Loop>0 first");
      else
        SEQ_UI_Msg_Track("frozen (quantized)");
      return 1;
    }

    case SEQ_UI_BUTTON_GP1: ui_selected_item = ITEM_GXTY;           return 1;
    case SEQ_UI_BUTTON_GP2: ui_selected_item = ITEM_PALETTE_LENGTH; return 1;
    case SEQ_UI_BUTTON_GP3: ui_selected_item = ITEM_LOOP_START;     return 1;
    case SEQ_UI_BUTTON_GP4: ui_selected_item = ITEM_LOOP_CYCLES;    return 1;
    case SEQ_UI_BUTTON_GP5: ui_selected_item = ITEM_LOOP_ROTATE;    return 1;

    case SEQ_UI_BUTTON_GP9: {
      // BOUNCE: capture last N measures into the next-free pattern slot.
      // If no free slot, DoBounce will open the pick-slot dialog.
      DoBounce(visible_track, -1);
      return 1;
    }

    case SEQ_UI_BUTTON_GP10:
    case SEQ_UI_BUTTON_GP11:
    case SEQ_UI_BUTTON_GP12:
    case SEQ_UI_BUTTON_GP13:
    case SEQ_UI_BUTTON_GP14:
    case SEQ_UI_BUTTON_GP15:
      return 0;

    // GP16: toggle to the FX_ROBOTIZE sibling (CC-state of the Robo page).
    // SELECT+GP16 is reroll bar 15, handled above.
    case SEQ_UI_BUTTON_GP16:
      SEQ_UI_PageSet(SEQ_UI_PAGE_FX_ROBOTIZE);
      return 1;

    case SEQ_UI_BUTTON_Select:
    case SEQ_UI_BUTTON_Right:
      if( ++ui_selected_item >= ITEM_ACTIONS_CURSOR )
        ui_selected_item = 0;
      return 1;

    case SEQ_UI_BUTTON_Left:
      if( ui_selected_item == 0 )
        ui_selected_item = ITEM_ACTIONS_CURSOR - 1;
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
// IMPORTANT: this hardware has TWO physically separate 2x40 LCDs with a gap.
// Strings must NEVER cross the col-40 boundary, or content will be split
// across the gap and look broken. Each LCD is filled with exactly 40 chars
// per row, independently.
//
// Layout:
//
//   Cols 0..39 (LCD 1)                       Cols 40..79 (LCD 2)
//   Row 0: |Trk. Loop Sync      Resd Frz  FrzQ     |Loop Anchors  (* now, # in loop)        |
//   Row 1: |G1T1   4   on                          |*###############      SPhase  0/ 4     |
//          GP1   GP2  GP3   GP4   GP5   GP6   GP7  GP8
//          5ch   5ch  5ch   5ch   5ch   5ch   5ch  5ch  (40 chars exactly)
/////////////////////////////////////////////////////////////////////////////
static s32 LCD_Handler(u8 high_prio)
{
  u8 visible_track = SEQ_UI_VisibleTrackGet();

  ///////////////////////////////////////////////////////////////////////////
  // LCD 1 - Row 0: labels (40 chars). 8 GP slots x 5 chars each.
  SEQ_LCD_CursorSet(0, 0);
  //                   "12345123451234512345123451234512345123451"
  SEQ_LCD_PrintString("Trk. Len. Strt Loop Rot. Resd Frz  FrzQ ");

  ///////////////////////////////////////////////////////////////////////////
  // LCD 1 - Row 1: values (40 chars exactly)
  SEQ_LCD_CursorSet(0, 1);

  // GP1: track (5 chars)
  if( ui_selected_item == ITEM_GXTY && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintGxTy(ui_selected_group, ui_selected_tracks); // 4 chars
    SEQ_LCD_PrintSpaces(1);
  }

  // GP2: palette length (5 chars)
  if( ui_selected_item == ITEM_PALETTE_LENGTH && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintFormattedString(" %2d  ", seq_cc_trk[visible_track].robotize_palette_length);
  }

  // GP3: loop start (5 chars)
  if( ui_selected_item == ITEM_LOOP_START && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintFormattedString(" %2d  ", seq_cc_trk[visible_track].robotize_loop_start);
  }

  // GP4: loop cycles (5 chars) - "off" or number
  if( ui_selected_item == ITEM_LOOP_CYCLES && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    u8 loop = seq_cc_trk[visible_track].robotize_loop_cycles;
    if( loop == 0 )
      SEQ_LCD_PrintString(" off ");
    else
      SEQ_LCD_PrintFormattedString(" %2d  ", loop);
  }

  // GP5: rotate (5 chars)
  if( ui_selected_item == ITEM_LOOP_ROTATE && ui_cursor_flash ) {
    SEQ_LCD_PrintSpaces(5);
  } else {
    SEQ_LCD_PrintFormattedString(" %2d  ", seq_cc_trk[visible_track].robotize_loop_rotate);
  }

  // GP6/7/8: action buttons (5 chars each, blank — labels in row 0 are the cue)
  SEQ_LCD_PrintSpaces(15);
  // total row 1 chars on LCD 1: 5*8 = 40 - good

  ///////////////////////////////////////////////////////////////////////////
  // LCD 2 - Row 0: anchor grid header (40 chars), or dialog prompt
  SEQ_LCD_CursorSet(40, 0);
  if( bounce_dialog == DIALOG_PICK_SLOT ) {
    //                   "1234512345123451234512345123451234512345"
    SEQ_LCD_PrintString("PICK SLOT GP1-8  (any other GP cancels) ");
  } else {
    //                   "1234512345123451234512345123451234512345"
    SEQ_LCD_PrintString("Anchors: * play, # loop, + palette   Bnc");
  }

  ///////////////////////////////////////////////////////////////////////////
  // LCD 2 - Row 1: 16-char grid + phase indicator (40 chars)
  SEQ_LCD_CursorSet(40, 1);
  {
    u8 loop = seq_cc_trk[visible_track].robotize_loop_cycles;
    u8 phase = seq_core_trk[visible_track].robotize_loop_phase;
    u8 palette = seq_cc_trk[visible_track].robotize_palette_length;
    if( palette == 0 || palette > 16 ) palette = 16;
    u8 start = seq_cc_trk[visible_track].robotize_loop_start;
    u8 rotate = seq_cc_trk[visible_track].robotize_loop_rotate;

    // compute which anchor indices are inside the current loop window
    u16 in_window = 0;
    u8 j;
    for(j=0; j<loop; ++j)
      in_window |= (1 << ((start + j) % palette));

    // currently-playing anchor
    u8 playing_idx = 255;
    if( loop )
      playing_idx = (start + ((rotate + phase) % loop)) % palette;

    char grid[17];
    u8 i;
    for(i=0; i<16; ++i) {
      if( i == playing_idx )                grid[i] = '*';
      else if( in_window & (1 << i) )       grid[i] = '#';
      else if( i < palette )                grid[i] = '+';
      else                                  grid[i] = '.';
    }
    grid[16] = 0;
    SEQ_LCD_PrintString(grid); // 16 chars
    SEQ_LCD_PrintSpaces(6);    // 6 chars padding
    SEQ_LCD_PrintChar(seq_cc_trk[visible_track].robotize_sync_to_master ? 'S' : ' '); // 1 char sync flag
    if( loop )
      SEQ_LCD_PrintFormattedString("Phase %2d/%2d   ", phase, loop); // 14 chars
    else
      SEQ_LCD_PrintString("Phase --/--   ");                         // 14 chars
    SEQ_LCD_PrintSpaces(3);    // 3 chars padding
    // total: 16 + 6 + 1 + 14 + 3 = 40 - good
  }

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_ROBOLOOP_Init(u32 mode)
{
  SEQ_UI_InstallButtonCallback(Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);

  // capture tap is always armed at boot (SEQ_CAPTURE_Init), so we don't
  // need to arm here. The ROBOLOOP GP9 stays as a convenience shortcut.

  // reset any leftover dialog state when re-entering the page
  bounce_dialog = DIALOG_IDLE;

  return 0;
}
