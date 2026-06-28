// $Id$
/*
 * Unified Capture page (fork) — "pick where it lands, set how much to grab"
 * (design §9 2026-06-27 / plan 2026-06-27-unified-capture; UI tuned on the real
 * midiphy V4+ LH panel — see doc/MBSEQV4_HARDWARE_GLOSSARY.md).
 *
 *   SOURCE  = the LIVE active track — it actively tracks "which track you are
 *             on" (SEQ_UI_VisibleTrackGet). You change the source the normal way
 *             (off this page, then SONG back in); the B-row does NOT move it.
 *   B-row   = the DESTINATION track (cap_dst_track) — a page-local pick that
 *             leaves the active track (= source) alone. Defaults to the source.
 *   GP row  = the DESTINATION pattern: GP1-8 = letter A-H, GP9-16 = number 1-8.
 *             Pressing a number COMMITS ("grabs").
 *   datawheel (= < / >) = GRAB, the one dial: "Save" at the pass-through detent,
 *             then 1..K frozen bars.
 *
 * GRAB decides the engine:
 *   - Save (cap_grab == 0)  -> SEQ_CORE_CopyTrackLiveToSlot: deposit the LIVING
 *                              source (full CC + source par/trg + generators);
 *                              recall regenerates it. Works any time.
 *   - N bars (cap_grab > 0) -> SEQ_CORE_CaptureSpanToSlotTrack: freeze the last
 *                              N bars of what actually played. Only the ring's
 *                              recording track is grabbable, and the ring follows
 *                              the visible track — which here IS the source — so
 *                              CaptureMaxK(source) just works (no pinning).
 *
 * The deposit preserves the destination slot's other 3 tracks and never switches
 * what's playing. Entered (latched, toggling) from the repurposed SONG button.
 *
 * ==========================================================================
 *  Licensed for personal non-commercial use only. All other rights reserved.
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>
#include <string.h>
#include "seq_lcd.h"
#include "seq_ui.h"
#include "seq_core.h"
#include "seq_pattern.h"
#include "seq_bpm.h"


/////////////////////////////////////////////////////////////////////////////
// Local variables (the deposit DRAFT — reset on entry)
/////////////////////////////////////////////////////////////////////////////

static u8 cap_dst_track;    // destination track (B-row); source = the live visible track
static u8 cap_grab;         // 0 = Save (living), 1..K = capture that many bars
static u8 cap_letter;       // dst pattern letter A..H (0..7), or 0xff = current
static u8 cap_num;          // dst pattern number 1..8 (0..7), or 0xff = current
static u8 cap_fit_mode;     // SEQ_CORE_CAP_FIT_FILL / _LOOP — how the grab fills the dst canvas (GP1 encoder)


/////////////////////////////////////////////////////////////////////////////
// Local helpers
/////////////////////////////////////////////////////////////////////////////

static u8 SrcTrack(void) { return SEQ_UI_VisibleTrackGet(); }      // live active track = source
static u8 DstGroup(void) { return cap_dst_track / SEQ_CORE_NUM_TRACKS_PER_GROUP; }
static u8 ResolveLetter(void){ return (cap_letter != 0xff) ? cap_letter : (u8)(seq_pattern[DstGroup()].group); }
static u8 ResolveNum(void)   { return (cap_num    != 0xff) ? cap_num    : (u8)(seq_pattern[DstGroup()].num); }

static void ClampGrab(void)
{
  u8 maxk = SEQ_CORE_CaptureMaxK(SrcTrack());
  if( cap_grab > maxk ) cap_grab = maxk;
}

// print exactly 40 chars (one LCD half), space-padded (the seam rule).
static void Print40(const char *s)
{
  char buf[41];
  int i = 0;
  while( i < 40 && s[i] ) { buf[i] = s[i]; ++i; }
  while( i < 40 ) buf[i++] = ' ';
  buf[40] = 0;
  SEQ_LCD_PrintString(buf);
}

static void CaptureCommitMsg(s32 status, u8 grab, u8 dst_track, u8 letter, u8 num)
{
  char l1[16], l2[24];
  if( status < 0 ) {
    sprintf(l1, "grab failed");
    sprintf(l2, "  A%c%d.T%d (err %d)", 'A' + (letter & 0x07), (num & 0x07) + 1, dst_track + 1, (int)status);
  } else if( grab == 0 ) {
    sprintf(l1, "SAVED (live)");
    sprintf(l2, "  >A%c%d.T%d", 'A' + (letter & 0x07), (num & 0x07) + 1, dst_track + 1);
  } else {
    sprintf(l1, "CAPTURED %db", grab);
    sprintf(l2, "  >A%c%d.T%d", 'A' + (letter & 0x07), (num & 0x07) + 1, dst_track + 1);
  }
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1500, l1, l2);
}

// The Pat-number press commits the chosen grab.
static void Commit(void)
{
  u8 src        = SrcTrack();
  u8 dst        = cap_dst_track;
  u8 dst_group  = dst / SEQ_CORE_NUM_TRACKS_PER_GROUP;
  u8 dst_bank   = seq_pattern[dst_group].bank;   // dedicated bank = the dst group's own
  u8 letter     = ResolveLetter();
  u8 num        = ResolveNum();
  u8 dst_pattern = (u8)(((letter & 0x07) << 3) | (num & 0x07)); // lower=0 (upper A-H)

  s32 r;
  if( cap_grab == 0 ) {
    r = SEQ_CORE_CopyTrackLiveToSlot(src, dst, dst_bank, dst_pattern);
  } else {
    u8 maxk = SEQ_CORE_CaptureMaxK(src);
    if( maxk == 0 ) {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1500, "CAPTURE: nothing", "  to grab (no ring)");
      return;
    }
    u8 k = (cap_grab > maxk) ? maxk : cap_grab;
    r = SEQ_CORE_CaptureSpanToSlotTrack(src, dst, dst_bank, dst_pattern, k, cap_fit_mode);
  }

  if( r >= 0 ) {
    // LIVE: always load the just-written slot track into the LIVE dst track so
    // the capture plays immediately — same track or a different one (bar-aligned;
    // the source + other live tracks keep going). The looper move: perform on one
    // track, stamp it onto another, carry on. (Provisional "always audible" per
    // by-ear — it overwrites the live dst track with the deposit regardless of
    // which pattern slot it was stored in.)
    SEQ_CORE_LoadTrackFromSlot(dst, dst_bank, dst_pattern, (u8)(dst % SEQ_CORE_NUM_TRACKS_PER_GROUP));
  }
  CaptureCommitMsg(r, cap_grab, dst, letter, num);
}


/////////////////////////////////////////////////////////////////////////////
// LED handler — the GP row shows the current pattern letter (1-8) + number
// (9-16). The B-row (destination track) is lit by the global TRACKS sel-view,
// special-cased to cap_dst_track (SEQ_UI_CAPTURE_DstTrackGet).
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  if( ui_cursor_flash )
    return 0;
  *gp_leds = (u16)((1 << (ResolveLetter() & 0x07)) | (1 << (8 + (ResolveNum() & 0x07))));
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Encoder handler — the datawheel (and < / > = Up/Down) is the GRAB dial.
/////////////////////////////////////////////////////////////////////////////
static s32 Encoder_Handler(seq_ui_encoder_t encoder, s32 incrementer)
{
  // GP1 encoder = the Fit toggle: CW -> LOOP (loops as grabbed, drifts), CCW -> FILL
  // (tiles to fill the canvas, grid-locked). Applies to N-bar grabs; ignored for Save.
  if( encoder == SEQ_UI_ENCODER_GP1 ) {
    u8 m = (incrementer > 0) ? SEQ_CORE_CAP_FIT_LOOP : SEQ_CORE_CAP_FIT_FILL;
    if( m == cap_fit_mode ) return 0;
    cap_fit_mode = m;
    return 1;
  }

  if( encoder != SEQ_UI_ENCODER_Datawheel )
    return -1; // other GP encoders unused here

  u8 maxk = SEQ_CORE_CaptureMaxK(SrcTrack());
  int g = (int)cap_grab + incrementer;
  if( g < 0 ) g = 0;
  if( g > maxk ) g = maxk;
  if( (u8)g == cap_grab ) return 0;
  cap_grab = (u8)g;
  return 1;
}


/////////////////////////////////////////////////////////////////////////////
// Button handler — GP row picks the pattern (number press commits); < / > nudge
// the GRAB dial.
/////////////////////////////////////////////////////////////////////////////
static s32 Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( depressed ) return 0;

  if( button >= SEQ_UI_BUTTON_GP1 && button <= SEQ_UI_BUTTON_GP16 ) {
    u8 gp = (u8)(button - SEQ_UI_BUTTON_GP1); // 0..15
    if( gp < 8 ) {
      cap_letter = gp;            // pick letter (stash)
    } else {
      cap_num = (u8)(gp - 8);     // pick number -> GRAB / commit
      Commit();
    }
    return 1;
  }

  switch( button ) {
  case SEQ_UI_BUTTON_Up:
    return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, 1);
  case SEQ_UI_BUTTON_Down:
    return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, -1);
  }
  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Display handler
/////////////////////////////////////////////////////////////////////////////
static s32 LCD_Handler(u8 high_prio)
{
  int i;
  ClampGrab();
  u8 maxk = SEQ_CORE_CaptureMaxK(SrcTrack());   // grabbable depth of the source, right now

  // ---- line 0, LCD-L: GRAB readout + live availability bar (drawn EVERY cycle,
  //      incl. high_prio, so it fills as the ring records — even in Save mode).
  //      Solid (char 3) = bars you're taking; thin (char 1) = available headroom;
  //      blank = nothing yet. Right-aligned to the seam. HBars charset (Init).
  char gv[6];
  if( cap_grab == 0 ) strcpy(gv, "Save");
  else sprintf(gv, "%db", cap_grab);
  SEQ_LCD_CursorSet(0, 0);
  SEQ_LCD_PrintString("Grab:");                                  // cols 0-4
  SEQ_LCD_PrintString(gv);
  for(i = (int)strlen(gv); i < 4; ++i) SEQ_LCD_PrintChar(' ');   // pad value to 4 (cols 5-8)
  SEQ_LCD_PrintChar(' ');                                        // col 9
  SEQ_LCD_PrintString("Fit:");                                   // cols 10-13 (GP1 encoder toggles)
  SEQ_LCD_PrintString( cap_grab == 0 ? "--  "
                       : (cap_fit_mode == SEQ_CORE_CAP_FIT_LOOP ? "LOOP" : "FILL") ); // cols 14-17
  SEQ_LCD_PrintSpaces(4);   // gap pushes the bar to the right edge of LCD-L (cols 18-21)
  SEQ_LCD_PrintChar('[');
  for(i = 0; i < 16; ++i)
    SEQ_LCD_PrintChar( (i < cap_grab) ? 3 : ((i < maxk) ? 1 : ' ') );
  SEQ_LCD_PrintChar(']');   // right-aligned: ']' lands on col 39 (the seam)

  if( high_prio )
    return 0;   // only the bar needs the frequent refresh; the rest is static

  // ---- line 0, LCD-R: source + destination + pattern (static, low-prio) ----
  u8 letter = ResolveLetter() & 0x07;
  u8 num    = ResolveNum() & 0x07;
  char r0[44];
  sprintf(r0, "Src:T%d  Dst:T%d  Pat:%c%d", SrcTrack() + 1, cap_dst_track + 1, 'A' + letter, num + 1);
  SEQ_LCD_CursorSet(40, 0);
  Print40(r0);

  // ---- line 1: GP-row picker — letters | numbers (8-cell halves) ----
  SEQ_LCD_CursorSet(0, 1);
  for(i=0; i<8; ++i) {                 // LCD-L: letters A..H
    u8 sel = (i == letter);
    SEQ_LCD_PrintChar(sel ? '>' : ' ');
    SEQ_LCD_PrintChar('A' + i);
    SEQ_LCD_PrintChar(sel ? '<' : ' ');
    SEQ_LCD_PrintSpaces(2);
  }
  for(i=0; i<8; ++i) {                 // LCD-R: numbers 1..8 (press = grab)
    u8 sel = (i == num);
    SEQ_LCD_PrintChar(sel ? '>' : ' ');
    SEQ_LCD_PrintFormattedString("%d", i + 1);
    SEQ_LCD_PrintChar(sel ? '<' : ' ');
    SEQ_LCD_PrintSpaces(2);
  }

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// B-row destination access (used by the DirectTrack intercept + the select-row
// LED special-case in seq_ui.c).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CAPTURE_SetDstTrack(u8 track)
{
  if( track < SEQ_CORE_NUM_TRACKS ) {
    cap_dst_track = track;
    seq_ui_display_update_req = 1;
  }
  return 0;
}
u8 SEQ_UI_CAPTURE_DstTrackGet(void) { return cap_dst_track; }


/////////////////////////////////////////////////////////////////////////////
// Entry — repurposed SONG button. Defaults the destination to the current
// (source) track, resets the draft, points the B-row at track-select, then
// latches the page. Source = the live visible track, so it's whatever track you
// were on; change it off this page and SONG back in.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CAPTURE_Enter(void)
{
  cap_dst_track = SEQ_UI_VisibleTrackGet();   // default dst = the source track (same-track)
  cap_grab      = 0;                          // Save (pass-through)
  cap_fit_mode  = SEQ_CORE_CAP_FIT_FILL;      // default: grid-locked fill
  cap_letter    = 0xff;                       // current dst-group letter
  cap_num       = 0xff;                       // current dst-group number
  seq_ui_sel_view = SEQ_UI_SEL_VIEW_TRACKS;   // B-row = destination track picker
  SEQ_UI_PageSet(SEQ_UI_PAGE_CAPTURE);
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CAPTURE_Init(u32 mode)
{
  SEQ_UI_InstallButtonCallback(Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);
  SEQ_LCD_InitSpecialChars(SEQ_LCD_CHARSET_HBars); // GRAB availability bar (char 3=full, 1=thin)
  return 0;
}
