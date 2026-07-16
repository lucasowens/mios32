// $Id$
/*
 * User Interface Routines
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
#include <blm.h>
#include <seq_midi_out.h>
#include <seq_bpm.h>
#include <blm_scalar_master.h>

#include "tasks.h"
#include "seq_ui.h"
#include "seq_lcd.h"
#include "seq_lcd_logo.h"
#include "seq_hwcfg.h"
#include "seq_lcd.h"
#include "seq_led.h"
#include "seq_blm8x8.h"
#include "seq_midply.h"
#include "seq_mixer.h"
#include "seq_live.h"
#include "seq_core.h"
#include "seq_song.h"
#include "seq_scale.h"
#include "seq_par.h"
#include "seq_layer.h"
#include "seq_cc.h"
#include "seq_record.h"
#include "seq_pattern.h"
#include "seq_midi_sysex.h"
#include "seq_midi_port.h"
#include "seq_midi_in.h"
#include "seq_blm.h"

#include "seq_groove.h"
#include "seq_lfo.h"
#include "seq_robotize.h"
#include "seq_generator.h"

#include "file.h"
#include "seq_file.h"
#include "seq_file_t.h"
#include "seq_file_g.h"
#include "seq_file_hw.h"


/////////////////////////////////////////////////////////////////////////////
// Global variables
/////////////////////////////////////////////////////////////////////////////

u8 seq_ui_display_update_req;
u8 seq_ui_display_init_req;

seq_ui_button_state_t seq_ui_button_state;

u8 ui_selected_group;
u16 ui_selected_tracks;
u8 ui_selected_par_layer;
u8 ui_selected_trg_layer;
u8 ui_selected_instrument;
u8 ui_selected_step_view;
u8 ui_selected_step;
u8 ui_selected_item;
u8 ui_selected_bookmark;
u8 ui_selected_phrase;
u8 ui_focused_proc_slot = 0; // focused rack ROW index in PROC view (0..PROC_NUM_ROWS-1; 0 = the merged Ptch row — rows carry their stack slot explicitly, row index != slot index)
u16 ui_selected_gp_buttons;

// PROC-page Groove row (G1.6): which template lane the GP row paints (0=Dly/1=Len/2=Vel),
// and whether a paint edited a custom template (so PROC-exit persists MBSEQ_G.V4).
static u8 proc_groove_paint_lane = 0;
static u8 proc_groove_dirty = 0;
// Groove paint BRUSH (2026-07-13): 0 = intensity-follow (taps paint the VPOS sentinel,
// the legacy toggle), else taps paint that literal signed offset. And the hold-step
// tracker: while a GP button is HELD on the groove face the Val encoder edits THAT
// step's cell (the EDIT-page hold-step idiom); a turn during the hold sets _turned so
// the release skips its toggle (the hold was a value edit, not a tap).
static s8 proc_groove_paint_val = 0;
static u8 proc_groove_held_step = 0xff;
static u8 proc_groove_held_turned = 0;
// Hold-vs-tap split (2026-07-13, "check a step without removing it"): a quick release
// (<350ms, the B-row double-tap threshold) is the toggle; a longer hold is a PEEK —
// the Val cell showed the step's value, the release does nothing.
static u32 proc_groove_held_t0 = 0;
// Toggle-off SHADOW ("just persist the value that was there"): a step's crafted value
// survives being toggled off (or hold+push-erased) and a re-tap RESTORES it — only
// shadowless cells paint the brush. 3 lanes x 16 steps, keyed to ONE template
// (_style; invalidated on style switch and on Clr — a cleared template must not
// resurrect old cells). UI-only, not persisted.
static s8 proc_groove_step_shadow[3][16];
static u8 proc_groove_shadow_style = 0xff;
// Set while SeedRowDefaults runs: a seed targets ONE track's untouched dial, so the
// GRV_INTN case must NOT broadcast it (a Styl 0->on on one track would otherwise
// stomp every global track's shaped intensity with the 32 seed).
static u8 proc_in_seed = 0;

// PROC-page plane (G2): which plane of the focused row is showing (0 = primary, 1 = the
// optional 2nd plane). Toggled by ‹/›; reset to 0 whenever focus changes (B-row tap).
static u8 ui_proc_plane = 0;

// PROC-page PitchGen STEPS plane (G2): which 16-step quarter of the 64-step loop the GP
// row shows/edits (0..3). UI-only, mirrors proc_groove_paint_lane.
static u8 proc_gen_step_window = 0;

u8 ui_selected_item;

u16 ui_hold_msg_ctr;
u8  ui_hold_msg_ctr_drum_edit; // 1 if a drum parameter is edited or parameter layer selection button is pushed

seq_ui_page_t ui_page;
seq_ui_page_t ui_selected_page;
seq_ui_page_t ui_stepview_prev_page;
seq_ui_page_t ui_trglayer_prev_page;
seq_ui_page_t ui_parlayer_prev_page;
seq_ui_page_t ui_inssel_prev_page;
seq_ui_page_t ui_tracksel_prev_page;
seq_ui_page_t ui_bookmarks_prev_page;
seq_ui_page_t ui_mute_prev_page;

volatile u8 ui_cursor_flash;
volatile u8 ui_cursor_flash_overrun_ctr;
u16 ui_cursor_flash_ctr;

u8 ui_edit_name_cursor;
u8 ui_edit_preset_num_category;
u8 ui_edit_preset_num_label;
u8 ui_edit_preset_num_drum;

u8 ui_seq_pause;

u8 ui_song_edit_pos;

u8 ui_store_file_required;

u8 seq_ui_backup_req;
u8 seq_ui_format_req;
u8 seq_ui_saveall_req;

u8 seq_ui_sent_cc_track;

seq_ui_options_t seq_ui_options;

// to display directories via SEQ_UI_SelectListItem() and SEQ_LCD_PrintList() -- see seq_ui_sysex.c as example
char ui_global_dir_list[80];

seq_ui_bookmark_t seq_ui_bookmarks[SEQ_UI_BOOKMARKS_NUM];

seq_ui_sel_view_t seq_ui_sel_view;

mios32_sys_time_t seq_play_timer;

// track cc modes
// 0: no CC sent on track changes
// 1: send a single CC which contains the track number as value
// 2: send CC..CC+15 depending on track number with value 127
seq_ui_track_cc_t seq_ui_track_cc = {
  .mode = 0,
  .port = USB1,
  .chn = 0,
  .cc = 100,
};


/////////////////////////////////////////////////////////////////////////////
// Local types
/////////////////////////////////////////////////////////////////////////////

typedef struct {
  u8 selected_instrument;
  u8 selected_par_layer;
  u8 selected_trg_layer;
  u8 selected_step_view;
} seq_ui_track_setup_t;


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static s32 (*ui_button_callback)(seq_ui_button_t button, s32 depressed);
static s32 (*ui_encoder_callback)(seq_ui_encoder_t encoder, s32 incrementer);
static s32 (*ui_led_callback)(u16 *gp_leds);
static s32 (*ui_lcd_callback)(u8 high_prio);
static s32 (*ui_exit_callback)(void);
static s32 (*ui_midi_in_callback)(mios32_midi_port_t port, mios32_midi_package_t p);
static s32 (*ui_delayed_action_callback)(u32 parameter);
static u32 ui_delayed_action_parameter;

static u16 ui_gp_leds;
// 2nd-color overlay for the duo-color GP LEDs (2026-07-13): rides the pos-marker
// channel (XORed with it, so the playhead inverts as it sweeps). Today only the gen
// STEPS faces set it — color 1 = triggered steps, color 2 = locks, both = the blend.
static u16 ui_gp_leds2;

#define UI_MSG_MAX_CHAR 31
static char ui_msg[2][UI_MSG_MAX_CHAR];
static u16 ui_msg_ctr;
static seq_ui_msg_type_t ui_msg_type;

static u16 ui_delayed_action_ctr;

static u8 seq_ui_track_setup_visible_track;
static seq_ui_track_setup_t seq_ui_track_setup[SEQ_CORE_NUM_TRACKS];

// PATTERN-hold capture gesture state. SOURCE = the visible track. One
// consistent operation: while PATTERN is held you pick a DESTINATION slot
// (top row) and optionally a destination TRACK within it (lower select row);
// the GP9-16 pattern press COMMITs a STATIC copy of the source into that
// track of the slot (persisted to SD, other slot tracks preserved) via
// SEQ_CORE_CaptureToSlotTrack. No jump — the source keeps playing.
//   GP1-8  (top row)  -> destination group letter (A..H); default current
//   GP9-16 (top row)  -> destination pattern number (1..8) -> COMMIT
//   select row (DirectTrack, 16 btns) -> destination track; default = source's
//                                        own track index (the select-row stash
//                                        lives in SEQ_UI_Button_DirectTrack)
static u8 pattern_held_gp_consumed;       // 1 if any capture button fired during the hold (suppress bare-tap nav)
static u8 pattern_capture_group = 0xff;   // chosen dest group letter, 0xff = current group
static u8 pattern_capture_dst_track = 0xff; // chosen dest track (select row), 0xff = default to source's track index
static char pattern_capture_status[24] = ""; // last commit result, shown in the held overlay (the popup is masked while PATTERN is held)

// Track-hold PULL gesture state (RECOMBINE — the mirror of the PATTERN-hold
// push: the push aims a destination slot, the pull aims a SOURCE slot).
// DESTINATION = the held select-row track (the transfusion target; its stock
// release-select still fires, so the cursor follows the pull). While held:
//   select row (another button) -> SOURCE column (bank = col/4, section =
//                                  col%4); default = the held track's own column
//   GP1-8  (top row)            -> source pattern letter (A..H); default = the
//                                  letter currently loaded in the column's group
//   GP9-16 (top row)            -> source pattern number (1..8) -> COMMIT:
//                                  SEQ_CORE_LoadTrackFromSlot, bar-aligned,
//                                  track undo armed by the verb
// Known cost (review by ear): while a select-row button is held, the stock
// multi-track chord-select on the OTHER select buttons is shadowed by the
// column pick.
static u8 pull_held_track = 0xff;   // select-row button currently held, 0xff = none
static u8 pull_src_column = 0xff;   // chosen source column, 0xff = held track's own
static u8 pull_letter = 0xff;       // chosen source letter, 0xff = column group's current
static char pull_status[24] = "";   // last commit result, shown in the held overlay

// Retroactive CAPTURE gesture state (UTILITY held). The north-star "play, then
// keep": after a generative take, STOP and grab the last K bars off the always-on
// ring into a fresh track. SOURCE = the ring's recording track (the visible track
// while it played; the ring invalidates on a track switch, so it's whatever was
// last recorded — SEQ_CORE_CaptureRingTrack()). While UTILITY is held:
//   select row (DirectTrack) -> DESTINATION track (stash; must differ from src).
//                               Swallowed, so the visible track does NOT change
//                               (a switch would invalidate the ring).
//   GP-n (1..16)             -> K = grab the last n LOOPS + COMMIT (a loop = the src
//                               track's full length, 1+ whole global measures; see
//                               SEQ_CORE_CaptureSpan; transport STOPPED or PLAYING).
// The GP LED row is a thermometer of the grabbable depth in loops (SEQ_CORE_CaptureMaxK).
// A bare UTILITY tap (no sub-gesture) still navigates to the Utility page.
static u8 capture_util_held;          // 1 while UTILITY held (capture armed)
static u8 capture_consumed;           // 1 if a sub-gesture fired during the hold (suppress bare-tap nav)
static u8 capture_dst_track = 0xff;   // destination track (select row), 0xff = none picked yet
static char capture_status[32] = ""; // last commit result, shown in the held overlay
static u32 capture_util_t0;           // UTILITY press timestamp (tap-vs-hold discrimination)


// PHRASES name editor (Stage B) state — declared here (ahead of its uses) so the
// gesture reset below can close a stuck editor. See the block above SEQ_UI_Button_GP.
static u8 phrase_name_edit;       // 1 while the keypad name editor owns GP row + LCD
static u8 phrase_name_edit_slot;  // which phrase (0..NUM_PHRASES-1) is being named

// POSTURE-MORPH: the ui_page the morph was armed on. The morph's GP-bar/datawheel/
// LED controls are scoped to THIS page — sel_view==PHRASE can stay latched on top
// of EDIT/other pages (and on a simplified frontpanel the PHRASE button doesn't
// even switch to SONG), where the GP row/datawheel belong to that page. Gating on
// the armed page (not a hardcoded page) releases the controls the moment you
// navigate away, regardless of how PHRASE view was entered.
static seq_ui_page_t morph_armed_page;

// Reset the transient UI gesture state — the pull/capture held-modifier statics
// and the active select-view. These are RAM-only performance state with no
// reset path of their own, so a manual session (button-pressing on hardware)
// can leave e.g. pull_held_track ARMED, which then intercepts select-row taps so
// track-select silently stops working until a PATTERN press. A harness
// RESET_STATE (a clean baseline) calls this so reset() fully normalizes UI
// state and the suite stops being perturbable by a prior hands-on session.
void SEQ_UI_GestureStateReset(void)
{
  pull_held_track = 0xff;
  pull_src_column = 0xff;
  pull_letter = 0xff;
  pull_status[0] = 0;
  pattern_held_gp_consumed = 0;
  pattern_capture_group = 0xff;
  pattern_capture_dst_track = 0xff;
  capture_util_held = 0;
  capture_consumed = 0;
  capture_dst_track = 0xff;
  capture_status[0] = 0;
  seq_ui_sel_view = SEQ_UI_SEL_VIEW_NONE;
  phrase_name_edit = 0; // close a stuck name editor (no own reset path)
  SEQ_PATTERN_PhraseMorphCancel(); // disarm any armed posture-morph (same hardening)
}


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////
static s32 SEQ_UI_Button_StepViewInc(s32 depressed);
static s32 SEQ_UI_Button_StepViewDec(s32 depressed);


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Init(u32 mode)
{
  int i;
  // clear all LEDs
  for(i=0; i<SEQ_LED_NUM_SR; ++i)
    SEQ_LED_SRSet(i, 0x00);

  // init selection variables
  ui_selected_group = 0;
  ui_selected_tracks = (1 << 0);
  ui_selected_par_layer = 0;
  ui_selected_trg_layer = 0;
  ui_selected_instrument = 0;
  ui_selected_step_view = 0;
  ui_selected_step = 0;
  ui_selected_item = 0;
  ui_selected_bookmark = 0;
  ui_selected_phrase = 0;
  ui_selected_gp_buttons = 0;

  seq_ui_options.ALL = 0;
  seq_ui_options.PRINT_TRANSPOSED_NOTES = 1;
  seq_ui_options.SELECT_UNMUTED_TRACK = 1;
  seq_ui_options.ALL_RELATIVE = 1;

  ui_hold_msg_ctr = 0;
  ui_msg_ctr = 0;
  ui_delayed_action_ctr = 0;

  ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  ui_cursor_flash = 0;

  seq_ui_button_state.ALL = 0;

  ui_seq_pause = 0;

  ui_song_edit_pos = 0;

  seq_ui_sel_view = SEQ_UI_SEL_VIEW_NONE;

  // visible GP pattern
  ui_gp_leds = 0x0000;

  // misc
  seq_ui_backup_req = 0;
  seq_ui_format_req = 0;
  seq_ui_saveall_req = 0;

  seq_ui_sent_cc_track = 0xff; // invalidate

  // change to edit page
  ui_page = SEQ_UI_PAGE_NONE;
  SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT);

  // finally init bookmarks
  ui_bookmarks_prev_page = SEQ_UI_PAGE_EDIT;
  for(i=0; i<SEQ_UI_BOOKMARKS_NUM; ++i) {
    char buffer[10];
    seq_ui_bookmark_t *bm = (seq_ui_bookmark_t *)&seq_ui_bookmarks[i];

    sprintf(buffer, "BM%2d ", i+1);
    memcpy((char *)bm->name, buffer, 6);
    bm->enable.ALL = ~0;
    bm->flags.LOCKED = 0;
    SEQ_UI_Bookmark_Store(i);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Inits the speed mode of all encoders
// Auto mode should be used whenever:
//    - the edit screen is entered
//    - the group is changed
//    - a track is changed
//    - a layer is changed
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_InitEncSpeed(u32 auto_config)
{
  mios32_enc_config_t enc_config;

  if( auto_config ) {

    if( !seq_hwcfg_enc.auto_fast )
      return 0; // auto mode not enabled - ignore auto reconfiguration request

    switch( SEQ_PAR_AssignmentGet(SEQ_UI_VisibleTrackGet(), ui_selected_par_layer) ) {
      case SEQ_PAR_Type_Velocity:
      case SEQ_PAR_Type_Length:
      case SEQ_PAR_Type_CC:
      case SEQ_PAR_Type_Ctrl:
      case SEQ_PAR_Type_PitchBend:
      case SEQ_PAR_Type_Probability:
      case SEQ_PAR_Type_Delay:
      case SEQ_PAR_Type_ProgramChange:
      	seq_ui_button_state.FAST_ENCODERS = 1;
      	break;

      default:
      	seq_ui_button_state.FAST_ENCODERS = 0;
    }
  }

  // change for datawheel and GP encoders
  int enc;
  for(enc=0; enc<SEQ_HWCFG_NUM_ENCODERS; ++enc) {
    enc_config = MIOS32_ENC_ConfigGet(enc);
    enc_config.cfg.speed = (seq_ui_button_state.FAST_ENCODERS || seq_ui_button_state.FAST2_ENCODERS) ? FAST : NORMAL;
    enc_config.cfg.speed_par = 
        (enc == 0)  ? seq_hwcfg_enc.datawheel_fast_speed
      : (enc == 17) ? seq_hwcfg_enc.bpm_fast_speed
      : seq_hwcfg_enc.gp_fast_speed;
    MIOS32_ENC_ConfigSet(enc, enc_config);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Various installation routines for menu page LCD handlers
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_InstallButtonCallback(void *callback)
{
  ui_button_callback = callback;
  return 0; // no error
}

s32 SEQ_UI_InstallEncoderCallback(void *callback)
{
  ui_encoder_callback = callback;
  return 0; // no error
}

s32 SEQ_UI_InstallLEDCallback(void *callback)
{
  ui_led_callback = callback;
  return 0; // no error
}

s32 SEQ_UI_InstallLCDCallback(void *callback)
{
  ui_lcd_callback = callback;
  return 0; // no error
}

s32 SEQ_UI_InstallExitCallback(void *callback)
{
  ui_exit_callback = callback;
  return 0; // no error
}

s32 SEQ_UI_InstallDelayedActionCallback(void *callback, u16 delay_mS, u32 parameter)
{
  // must be atomic
  MIOS32_IRQ_Disable();
  ui_delayed_action_parameter = parameter;
  ui_delayed_action_callback = callback;
  ui_delayed_action_ctr = delay_mS;
  MIOS32_IRQ_Enable();

  return 0; // no error
}

s32 SEQ_UI_UnInstallDelayedActionCallback(void *callback)
{
  // must be atomic
  MIOS32_IRQ_Disable();
  if( ui_delayed_action_callback == callback )
    ui_delayed_action_callback = 0;
  MIOS32_IRQ_Enable();

  return 0; // no error
}


s32 SEQ_UI_InstallMIDIINCallback(void *callback)
{
  ui_midi_in_callback = callback;
  return 0; // no error
}

s32 SEQ_UI_NotifyMIDIINCallback(mios32_midi_port_t port, mios32_midi_package_t p)
{
  if( ui_midi_in_callback != NULL )
    return ui_midi_in_callback(port, p);

  return -1; // no callback install
}


/////////////////////////////////////////////////////////////////////////////
// Change the menu page
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_PageSet(seq_ui_page_t page)
{
  if( page != ui_page ) {

    // call page exit callback
    if( ui_exit_callback != NULL )
      ui_exit_callback();

    // disable store file which was maybe used in page (usually serviced by exit callback)
    ui_store_file_required = 0;

    // disable hooks of previous page and request re-initialisation
    portENTER_CRITICAL();
    ui_page = page;
    ui_button_callback = NULL;
    ui_encoder_callback = NULL;
    ui_led_callback = NULL;
    ui_lcd_callback = NULL;
    ui_exit_callback = NULL;
    ui_midi_in_callback = NULL;
    ui_delayed_action_callback = NULL;
    portEXIT_CRITICAL();

    // always disable ALL button when changing page
    seq_ui_button_state.CHANGE_ALL_STEPS_SAME_VALUE = 0;
    seq_ui_button_state.CHANGE_ALL_STEPS = 0;

    // request display initialisation
    seq_ui_display_init_req = 1;
  }

  // for MENU button:
  if( seq_hwcfg_button_beh.menu )
    seq_ui_button_state.MENU_PRESSED = 0; // MENU page selection finished

  // first page has been selected - display new screen
  seq_ui_button_state.MENU_FIRST_PAGE_SELECTED = 1;

  // for SEQ_UI_MENU which is accessible with EXIT button
  // remember the current selectable page
  if( ui_page >= SEQ_UI_FIRST_MENU_SELECTION_PAGE )
    ui_selected_page = ui_page;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Local help functions for copy/paste/clear/undo operations
/////////////////////////////////////////////////////////////////////////////
void SEQ_UI_Msg_Track(char *line2)
{
  char buffer[40];
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  sprintf(buffer, "Track G%dT%d", 1 + (visible_track / 4), 1 + (visible_track % 4));
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_ParLayer(char *line2)
{
  char buffer[40];
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  sprintf(buffer, "Parameter Layer G%dT%d %c:%s", 1 + (visible_track / 4), 1 + (visible_track % 4), 'A'+ui_selected_par_layer, SEQ_PAR_TypeStr(SEQ_PAR_AssignmentGet(visible_track, ui_selected_par_layer)));
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_TrgLayer(char *line2)
{
  char buffer[40];
  u8 visible_track = SEQ_UI_VisibleTrackGet();

  sprintf(buffer, "Trigger Layer G%dT%d %c:%s", 1 + (visible_track / 4), 1 + (visible_track % 4), 'A'+ui_selected_trg_layer, SEQ_TRG_TypeStr(SEQ_TRG_AssignmentGet(visible_track, ui_selected_trg_layer)));
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_InsLayer(char *line2)
{
  char buffer[40];
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);

  char name[6];
  if( event_mode == SEQ_EVENT_MODE_Drum ) {
    memcpy(name, seq_core_trk[visible_track].name + ui_selected_instrument*5, 5);
    name[5] = 0;
  } else {
    sprintf(name, "INS%d ", ui_selected_instrument+1);
  }

  sprintf(buffer, "Instrument G%dT%d %s", 1 + (visible_track / 4), 1 + (visible_track % 4), name);

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_Step(char *line2)
{
  char buffer[40];
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  sprintf(buffer, "Step G%dT%d #%d", 1 + (visible_track / 4), 1 + (visible_track % 4), ui_selected_step + 1);
  SEQ_UI_Msg(((ui_selected_step % 16) < 8) ? SEQ_UI_MSG_USER_R : SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_Layer(char *line2)
{
  char buffer[20];
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(SEQ_UI_VisibleTrackGet(), SEQ_CC_MIDI_EVENT_MODE);
  if( event_mode == SEQ_EVENT_MODE_Drum ) {
    sprintf(buffer, "Layer G%dT%d.I%d", 1 + (visible_track / 4), 1 + (visible_track % 4), ui_selected_instrument+1);
  } else {
    sprintf(buffer, "Layer G%dT%d.P%c", 1 + (visible_track / 4), 1 + (visible_track % 4), 'A'+ui_selected_par_layer);
  }
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_MixerMap(char *line2)
{
  char buffer[20];
  sprintf(buffer, "Mixer Map #%d", SEQ_MIXER_NumGet()+1);
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_Patterns(char *line2)
{
  char buffer[40];
  sprintf(buffer, "Patterns %d:%c%c %d:%c%c %d:%c%c %d:%c%c",
	  seq_pattern[0].bank+1, 'A' + seq_pattern[0].group + (seq_pattern[0].lower ? 32 : 0), '1' + seq_pattern[0].num,
	  seq_pattern[1].bank+1, 'A' + seq_pattern[1].group + (seq_pattern[1].lower ? 32 : 0), '1' + seq_pattern[1].num,
	  seq_pattern[2].bank+1, 'A' + seq_pattern[2].group + (seq_pattern[2].lower ? 32 : 0), '1' + seq_pattern[2].num,
	  seq_pattern[3].bank+1, 'A' + seq_pattern[3].group + (seq_pattern[3].lower ? 32 : 0), '1' + seq_pattern[3].num);
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_SongPos(char *line2)
{
  char buffer[20];
  sprintf(buffer, "Song Position %c%d", 'A' + (ui_song_edit_pos >> 3), (ui_song_edit_pos&7)+1);
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}

void SEQ_UI_Msg_LivePattern(char *line2)
{
  char buffer[20];

  seq_live_pattern_slot_t *slot = SEQ_LIVE_CurrentSlotGet();
  sprintf(buffer, "Live Pattern #%d", slot->pattern + 1);
  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, buffer, line2);
}


/////////////////////////////////////////////////////////////////////////////
// Dedicated button functions
// Mapped to physical buttons in SEQ_UI_Button_Handler()
// Will also be mapped to MIDI keys later (for MIDI remote function)
/////////////////////////////////////////////////////////////////////////////

// Print the result of a capture → track-in-slot (visible track → dst_track of a
// pattern slot, persisted). status >=0 ok, else failure. Shows ">A3.Tnn".
static void pattern_capture_slottrack_msg(s32 status, seq_pattern_t dst, u8 dst_track)
{
  if( status < 0 ) {
    SEQ_UI_Msg_Track("capture failed");
    sprintf(pattern_capture_status, "FAILED A%c%d.T%d",
            'A' + (dst.group & 0x07), (dst.num & 0x07) + 1, dst_track + 1);
  } else {
    char msg[12];
    int n = 0;
    msg[n++] = '>';
    msg[n++] = 'A' + (dst.group & 0x07);
    msg[n++] = '1' + (dst.num & 0x07);
    msg[n++] = '.';
    msg[n++] = 'T';
    if( (dst_track + 1) >= 10 )
      msg[n++] = '0' + ((dst_track + 1) / 10);
    msg[n++] = '0' + ((dst_track + 1) % 10);
    msg[n] = 0;
    SEQ_UI_Msg_Track(msg);
    sprintf(pattern_capture_status, "saved A%c%d.T%d",
            'A' + (dst.group & 0x07), (dst.num & 0x07) + 1, dst_track + 1);
  }
}

// Print the result of a pull (slot section -> live track, the RECOMBINE
// transfusion). Mirrors pattern_capture_slottrack_msg.
static void pull_commit_msg(s32 status, u8 bank, u8 letter, u8 num, u8 section, u8 dst_track)
{
  if( status < 0 ) {
    SEQ_UI_Msg_Track("pull failed");
    sprintf(pull_status, "FAILED %d:%c%d.S%d",
            bank + 1, 'A' + (letter & 0x07), (num & 0x07) + 1, section + 1);
  } else {
    char msg[12];
    sprintf(msg, "<%c%d.S%d", 'A' + (letter & 0x07), (num & 0x07) + 1, section + 1);
    SEQ_UI_Msg_Track(msg);
    sprintf(pull_status, "pulled %d:%c%d.S%d>T%d",
            bank + 1, 'A' + (letter & 0x07), (num & 0x07) + 1, section + 1, dst_track + 1);
  }
}

// Print the result of a retroactive CAPTURE span (ring track -> dst track, the
// north-star play-then-keep grab). Validates the two UI-level preconditions a
// terse engine code can't express (no ring / no destination picked) before
// calling SEQ_CORE_CaptureSpan (the dispatcher: PLAYING -> live tape of what
// sounded, STOPPED -> re-sim of the generative frame), then formats the
// held-overlay status + a transient track popup. Negative codes map per CMD_CAPTURE_SPAN.
static void capture_span_msg(u8 src, u8 dst, u8 k)
{
  if( src >= SEQ_CORE_NUM_TRACKS ) {           // ring empty or invalidated
    SEQ_UI_Msg_Track("no ring");
    sprintf(capture_status, "no ring: play a track");
    return;
  }
  if( dst >= SEQ_CORE_NUM_TRACKS ) {           // no select-row pick yet
    SEQ_UI_Msg_Track("pick dest");
    sprintf(capture_status, "pick dest trk (sel row)");
    return;
  }

  s32 r = SEQ_CORE_CaptureSpan(src, dst, k, SEQ_CORE_CAP_PHASE_GRID); // legacy gesture: loop-aligned
  if( r == 0 ) {
    char msg[12];
    sprintf(msg, ">T%d %dL", dst + 1, k);                 // L = loops (k loops of src)
    SEQ_UI_Msg_Track(msg);
    sprintf(capture_status, "T%d last %d loops -> T%d", src + 1, k, dst + 1);
  } else {
    const char *why;
    switch( r ) {
      case -2:  why = "src==dst";      break;
      case -3:  why = "STOP first";    break;
      case -4:  why = "ring not src";  break;
      case -5:  why = "ring overflow"; break;
      case -6:  why = "too few loops"; break; // fewer aligned loops than asked / torn ring
      case -10: why = "tape too dense";break; // span scrolled out of the live tape
      case -7:  why = ">256 steps";    break;
      case -8:  why = "play to grab";  break; // stopped re-sim is whole-measure only; PLAY -> tape grabs any length
      case -9:  why = "dst par full";  break;
      case -11: why = "arp track";     break;
      case -12: why = "dst trg full";  break;
      default:  why = "refused";       break;
    }
    SEQ_UI_Msg_Track("capture refused");
    sprintf(capture_status, "refused: %s", why);
  }
}

/////////////////////////////////////////////////////////////////////////////
// PHRASES name editor (Stage B): a global keypad sub-mode layered over the
// PHRASE select-view. The waypoint row lives on the SELECT row; the keypad
// chars live on the GP/step row + encoders, so they don't collide. Entered
// right after a CAPTURE (the editor's own LCD IS the capture confirmation);
// GP16 or EXIT commits the name to disk and closes. seq_phrase_name is edited
// in place via SEQ_PATTERN_PhraseName; persistence is SEQ_PATTERN_PhraseNameCommit.
// Provisional gesture — tuned by ear with hardware, per the FEARLESS precedent.
// (phrase_name_edit / phrase_name_edit_slot are declared earlier, ahead of
// SEQ_UI_GestureStateReset which closes a stuck editor on harness reset.)
/////////////////////////////////////////////////////////////////////////////

// route one GP button (incrementer 0) / GP encoder (incrementer != 0) into the
// keypad editor; GP16 commits the name + closes. Returns 1 when consumed.
static s32 SEQ_UI_PhraseName_Input(seq_ui_encoder_t encoder, s32 incrementer)
{
  if( encoder == SEQ_UI_ENCODER_GP16 ) { // store name + exit (button only)
    if( incrementer != 0 )
      return 1;
    SEQ_PATTERN_PhraseNameCommit(phrase_name_edit_slot);
    phrase_name_edit = 0;
    return 1;
  }

  char *buf = SEQ_PATTERN_PhraseName(phrase_name_edit_slot);
  if( buf == NULL ) { phrase_name_edit = 0; return 1; }
  return SEQ_UI_KeyPad_Handler(encoder, incrementer, buf, 20);
}

// format a phrase's confirmation label: trimmed name if named, else "Phrase N".
static void SEQ_UI_PhraseName_MsgLabel(u8 n, char *out)
{
  char *nm = SEQ_PATTERN_PhraseName(n);
  u8 has = 0;
  int len = 0, i;
  if( nm != NULL )
    for(i=0; i<20; ++i)
      if( nm[i] != ' ' ) { has = 1; len = i + 1; }

  if( has ) {
    sprintf(out, "PH%d ", n + 1);
    int p = (int)strlen(out);
    for(i=0; i<len && p<20; ++i)
      out[p++] = nm[i];
    out[p] = 0;
  } else {
    sprintf(out, "Phrase %d", n + 1);
  }
}

static s32 SEQ_UI_Button_GP(s32 depressed, u32 gp)
{
  // PHRASES name editor: while active it owns the GP/step row + encoders (the
  // waypoints stay on the select row). Route presses into the keypad.
  if( phrase_name_edit ) {
    if( !depressed )
      SEQ_UI_PhraseName_Input((seq_ui_encoder_t)gp, 0);
    seq_ui_display_update_req = 1;
    return 0;
  }

  if( !depressed ) // selection button has been pressed while Bookm/Step/Track/Param/Trigger/Instr/Mute/Phrase button pressed: don't take over new sel view anymore
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 0;

  // Capture gesture (PATTERN held): freeze a STATIC copy of the VISIBLE track
  // into a destination slot's track. Top rows pick the slot:
  //   GP1-8  (top row, left)  -> destination group letter (A..H); default current
  //   GP9-16 (top row, right) -> destination pattern number (1..8) -> commit
  // The lower select row optionally picks the destination TRACK within the slot
  // (stashed in SEQ_UI_Button_DirectTrack); default = the source's own track
  // index. Always SEQ_CORE_CaptureToSlotTrack — same effect for same/different
  // track or group. No jump; the source keeps playing.

  if( !depressed && seq_ui_button_state.PATTERN_PRESSED && gp < 8 ) {
    // top row left: destination group (stash; pattern press commits).
    pattern_capture_group = (u8)gp;
    pattern_held_gp_consumed = 1;
    return 0;
  }

  if( !depressed && seq_ui_button_state.PATTERN_PRESSED && gp >= 8 && gp < 16 ) {
    // top row right: destination pattern number -> commit, persisted to SD.
    pattern_held_gp_consumed = 1;
    u8 src_track = SEQ_UI_VisibleTrackGet();
    u8 src_group = src_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
    seq_pattern_t dst = seq_pattern[src_group];
    if( pattern_capture_group != 0xff )
      dst.group = pattern_capture_group & 0x07; // else keep the current group
    dst.num   = (u8)(gp - 8) & 0x07;
    dst.lower = 0;

    // Freeze a STATIC copy of the source track into the destination pattern's
    // track, preserving that pattern's other 3 tracks, persisted to SD. The
    // destination track is the select-row pick, or the source's own track index
    // if none was picked. Then ONE rule decides whether it also loads:
    //   - SAME group as the source -> persist only, NO load. The generator/source
    //     keeps playing its current pattern; the variation sits in the chosen
    //     pattern until you deliberately select it. (Builds a variation library
    //     without ever disturbing what you're tweaking.)
    //   - DIFFERENT group -> also load that group's pattern so the merged capture
    //     plays there immediately (audition in a spare group / build a multitimbral
    //     canvas). The source group is never the one loaded, so it is never jumped.
    // The user steers same-vs-cross purely by which destination track they aim at.
    u8 dst_track = (pattern_capture_dst_track != 0xff) ? pattern_capture_dst_track : src_track;
    u8 dst_group = dst_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
    // Write/load into the DESTINATION group's OWN bank, not the source group's
    // (dst was copied from seq_pattern[src_group], so dst.bank started as the
    // source's bank). The Pattern page navigates each group only within its
    // dedicated bank (bank change is #if 0'd in seq_ui_pattern.c; dedicated bank
    // = group index, seq_pattern.c), so a cross-group capture written to the
    // source's bank auditions fine but is UNREACHABLE when you switch the dst
    // group's pattern away and back — it lives in a bank that group can't address.
    // Same-group is a no-op: dst_group==src_group, so this is the bank dst already had.
    dst.bank = seq_pattern[dst_group].bank;
    // Transport-conditional source (Option 1, 2026-06-27): while PLAYING, grab the
    // live RECORDER (the retroactive tape — what actually sounded over the last loop,
    // incl. live keys / coin-flips / self-bus wander) into the slot; while STOPPED,
    // keep freezing the static render (unchanged, works with no playback history).
    // dst_track defaults to src_track above => "same track, another pattern."
    s32 cap_r = SEQ_BPM_IsRunning()
      ? SEQ_CORE_CaptureSpanToSlotTrack(src_track, dst_track, dst.bank, dst.pattern, 1, SEQ_CORE_CAP_FIT_FILL, SEQ_CORE_CAP_PHASE_GRID)
      : SEQ_CORE_CaptureToSlotTrack(src_track, dst_track, dst.bank, dst.pattern);
    if( cap_r >= 0 ) {
      if( dst_group != src_group ) {
        // FEARLESS: the capture just replaced that slot's record. If it's the
        // dst group's WORKING slot, the switch below would first write the
        // group's stale live state back over the fresh capture — here the
        // slot content is the intent, so the divergence is discarded instead.
        if( dst.bank == seq_pattern[dst_group].bank && dst.pattern == seq_pattern[dst_group].pattern )
          SEQ_PATTERN_DirtyClearGroup(dst_group);
        SEQ_PATTERN_Change(dst_group, dst, 1); // cross-group: bring the merged pattern up live
        // The immediate (force=1) load skips the boundary handler's track restart,
        // so the loaded group would keep the previous pattern's stale step phase
        // (FIRST_CLK=0) and stay SILENT until a manual pattern switch re-bases it.
        // Re-synch the dst group's 4 tracks to the next measure so the merged
        // capture restarts from step 0 ON THE BAR and plays, locked to the master
        // (the source group is a different group, so it is never re-synched).
        SEQ_CORE_ManualSynchToMeasure(0xf << (4 * dst_group));
      }
    }
    pattern_capture_slottrack_msg(cap_r, dst, dst_track);
    // group + dst track stay set so the user can rapid-fire to more patterns.
    return 0;
  }

  // Retroactive CAPTURE gesture (UTILITY held): GP-n grabs the last n LOOPS (n =
  // GP index, GP1=1 .. GP16=16; a loop = the src track's full length) of the ring's
  // track into the chosen destination track and COMMITs. The GP LED row shows the
  // grabbable max = min(ring depth-1, what the dst par/trg buffer holds for src's
  // layout) — a heavy layout (e.g. a 16-instr drum track) caps it below the ring;
  // source = SEQ_CORE_CaptureRingTrack(),
  // destination = the select-row pick. Consumes the press (suppresses the bare-
  // tap navigation to the Utility page on release). dst/src stay so the user can
  // rapid-fire other bar counts.
  if( !depressed && capture_util_held && gp < 16 ) {
    capture_consumed = 1;
    capture_span_msg(SEQ_CORE_CaptureRingTrack(), capture_dst_track, (u8)(gp + 1));
    seq_ui_display_update_req = 1;
    return 0;
  }

  // Pull gesture (RECOMBINE): while a select-row track button is held, the top
  // row aims the SOURCE pattern and the number press commits the pull — the
  // mirror image of the PATTERN-hold capture above. (PATTERN-held events never
  // reach here: the capture intercepts cover all GPs while PATTERN is down.)
  // TRACKS view only — guards against a stale hold committing a phantom pull
  // after a view switch (see the arm-side note in SEQ_UI_Button_DirectTrack).
  if( !depressed && pull_held_track != 0xff && seq_ui_sel_view == SEQ_UI_SEL_VIEW_TRACKS && gp < 8 ) {
    pull_letter = (u8)gp; // source pattern letter (stash; number press commits)
    return 0;
  }

  if( !depressed && pull_held_track != 0xff && seq_ui_sel_view == SEQ_UI_SEL_VIEW_TRACKS && gp >= 8 && gp < 16 ) {
    // source column: the explicit select-row pick, or the held track's own
    u8 src_col = (pull_src_column != 0xff) ? pull_src_column : pull_held_track;
    u8 src_bank = src_col / SEQ_CORE_NUM_TRACKS_PER_GROUP;     // bank = column's group (dedicated-bank identity)
    u8 src_section = src_col % SEQ_CORE_NUM_TRACKS_PER_GROUP;  // section within the slot
    // letter defaults to what the column's group currently has loaded
    u8 letter = (pull_letter != 0xff) ? pull_letter : (u8)seq_pattern[src_bank].group;
    u8 src_pattern = (u8)(((letter & 0x07) << 3) | ((gp - 8) & 0x07));

    // The transfusion: one stored section into the held track, bar-aligned;
    // the verb arms the track undo with the held track's prior state.
    s32 r = SEQ_CORE_LoadTrackFromSlot(pull_held_track, src_bank, src_pattern, src_section);
    pull_commit_msg(r, src_bank, letter, (u8)(gp - 8), src_section, pull_held_track);
    // aim stays set so the user can rapid-fire pulls from neighboring patterns
    return 0;
  }

  // POSTURE-MORPH coarse bar: while a posture-morph is armed in PHRASE view, the
  // GP row is a 16-segment morph-position bar — GP_k grabs position k+1 (GP16 =
  // full throw). Fine-trim with the datawheel from there. Consumes the press
  // (mirrors the pull intercept) so the underlying page never sees it. Scoped to
  // the page the morph was armed on: sel_view==PHRASE can stay LATCHED on top of
  // EDIT/other pages, where the GP row belongs to that page — don't hijack it.
  if( !depressed && seq_ui_sel_view == SEQ_UI_SEL_VIEW_PHRASE && ui_page == morph_armed_page &&
      SEQ_PATTERN_PhraseMorphTarget() >= 0 && gp < 16 ) {
    SEQ_PATTERN_PhraseMorphSet((u8)(gp + 1));
    seq_ui_display_update_req = 1;
    return 0;
  }

  // in MENU page: overrule GP buttons as long as MENU button is pressed/active
  if( seq_ui_button_state.MENU_PRESSED || seq_hwcfg_blm.gp_always_select_menu_page ) {
    if( depressed ) return -1;
    SEQ_UI_PageSet(SEQ_UI_PAGES_MenuShortcutPageGet(gp));
  } else {
    if( depressed )
      ui_selected_gp_buttons &= ~(1 << gp);
    else
      ui_selected_gp_buttons |= (1 << gp);

    // forward to menu page
    if( ui_button_callback != NULL ) {
      ui_button_callback(gp, depressed);
      ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0; // ensure that value is visible when it has been changed
    }
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Left(s32 depressed)
{
  // forward to menu page
  if( !seq_ui_button_state.MENU_PRESSED && ui_button_callback != NULL ) {
    ui_button_callback(SEQ_UI_BUTTON_Left, depressed);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Right(s32 depressed)
{
  // forward to menu page
  if( !seq_ui_button_state.MENU_PRESSED && ui_button_callback != NULL ) {
    ui_button_callback(SEQ_UI_BUTTON_Right, depressed);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Down(s32 depressed)
{
  seq_ui_button_state.DOWN = depressed ? 0 : 1;

  // INS view, melodic keyboard on the B-row: > scrolls the row -1 semitone (any
  // page), so scroll works while the keyboard is played from a latched sel-view.
  if( !depressed && !seq_ui_button_state.MENU_PRESSED &&
      seq_ui_sel_view == SEQ_UI_SEL_VIEW_INS && SEQ_UI_INSSEL_KeyboardActive() ) {
    SEQ_UI_INSSEL_KeyboardScroll(-1);
    return 0;
  }

  // forward to menu page
  if( !seq_ui_button_state.MENU_PRESSED && ui_button_callback != NULL ) {
    ui_button_callback(SEQ_UI_BUTTON_Down, depressed);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Up(s32 depressed)
{
  seq_ui_button_state.UP = depressed ? 0 : 1;

  // INS view, melodic keyboard on the B-row: < scrolls the row +1 semitone (any page)
  if( !depressed && !seq_ui_button_state.MENU_PRESSED &&
      seq_ui_sel_view == SEQ_UI_SEL_VIEW_INS && SEQ_UI_INSSEL_KeyboardActive() ) {
    SEQ_UI_INSSEL_KeyboardScroll(1);
    return 0;
  }

  // forward to menu page
  if( !seq_ui_button_state.MENU_PRESSED && ui_button_callback != NULL ) {
    ui_button_callback(SEQ_UI_BUTTON_Up, depressed);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  return 0; // no error
}

s32 SEQ_UI_Button_Stop(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // if sequencer running: stop it
  // if sequencer already stopped: reset song position
  if( SEQ_BPM_IsRunning() ) {
#if 0
    // TK: maybe to complicated to understand: STOP sequencer in slave mode if stop button pressed twice
    u8 enable_slaveclk_mute = !SEQ_BPM_IsMaster() && seq_core_slaveclk_mute != SEQ_CORE_SLAVECLK_MUTE_Enabled;
#else
    // always mute tracks, never stop sequencer (can only be done from external)
    u8 enable_slaveclk_mute = !SEQ_BPM_IsMaster();
#endif
    if( enable_slaveclk_mute ) {
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Enabled;
    } else {
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
      SEQ_BPM_Stop();
    }
  } else {
    seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
    SEQ_SONG_Reset(0);
    SEQ_CORE_Reset(0);
    SEQ_MIDPLY_Reset();
  }

  seq_play_timer.seconds = 0;
	
  return 0; // no error
}

static s32 SEQ_UI_Button_Pause(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // if in auto mode and BPM generator is not clocked in slave mode:
  // change to master mode
  SEQ_BPM_CheckAutoMaster();

  // toggle pause
  ui_seq_pause ^= 1;

  // execute stop/continue depending on new mode
  MIOS32_IRQ_Disable();
  if( ui_seq_pause ) {
    if( !SEQ_BPM_IsMaster() ) {
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Enabled;
    } else {
      SEQ_BPM_Stop();
    }
  } else {
    if( !SEQ_BPM_IsMaster() ) {
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
    }

    if( !SEQ_BPM_IsRunning() )
      SEQ_BPM_Cont();
  }
  MIOS32_IRQ_Enable();

  return 0; // no error
}

s32 SEQ_UI_Button_Play(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // if MENU button pressed -> tap tempo
  if( seq_ui_button_state.MENU_PRESSED )
    return SEQ_UI_BPM_TapTempo();

  // if in auto mode and BPM generator is not clocked in slave mode:
  // change to master mode
  SEQ_BPM_CheckAutoMaster();

  // slave mode and tracks muted: enable on next measure
  if( !SEQ_BPM_IsMaster() && SEQ_BPM_IsRunning() ) {
    if( seq_core_slaveclk_mute != SEQ_CORE_SLAVECLK_MUTE_Off )
      seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_OffOnNextMeasure;
    // TK: note - in difference to master mode pressing PLAY twice won't reset the sequencer!
  } else {
    // send program change & bank selects
    MUTEX_MIDIOUT_TAKE;
    u8 track;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track)
      SEQ_LAYER_SendPCBankValues(track, 0, 1);
    MUTEX_MIDIOUT_GIVE;

#if 0
    // if sequencer running: restart it
    // if sequencer stopped: continue at last song position
    if( SEQ_BPM_IsRunning() )
      SEQ_BPM_Start();
    else
      SEQ_BPM_Cont();
#else
    // always restart sequencer
    seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
    SEQ_BPM_Start();
#endif
  }

  seq_play_timer = MIOS32_SYS_TimeGet();
	
  return 0; // no error
}

static s32 SEQ_UI_Button_Rew(s32 depressed)
{
  seq_ui_button_state.REW = depressed ? 0 : 1;

  if( depressed ) return -1; // ignore when button depressed

  if( SEQ_SONG_ActiveGet() ) {
    portENTER_CRITICAL();
    SEQ_SONG_Rew();
    portEXIT_CRITICAL();
  } else {
    //SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "We are not", "in Song Mode!");
    SEQ_UI_Button_StepViewDec(depressed);
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Fwd(s32 depressed)
{
  seq_ui_button_state.FWD = depressed ? 0 : 1;

  if( depressed ) return -1; // ignore when button depressed

  if( SEQ_SONG_ActiveGet() ) {
    portENTER_CRITICAL();
    SEQ_SONG_Fwd();
    portEXIT_CRITICAL();
  } else {
    //SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "We are not", "in Song Mode!");
    SEQ_UI_Button_StepViewInc(depressed);
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Loop(s32 depressed)
{
  if( seq_hwcfg_button_beh.loop ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    // should be atomic
    portENTER_CRITICAL();
    seq_core_state.LOOP ^= 1;
  } else {
    // should be atomic
    portENTER_CRITICAL();
    // set mode
    seq_core_state.LOOP = depressed ? 0 : 1;
  }
  portEXIT_CRITICAL();

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Loop Mode", seq_core_state.LOOP ? "    on" : "   off");

  return 0; // no error
}

static s32 SEQ_UI_Button_Follow(s32 depressed)
{
  if( seq_hwcfg_button_beh.follow ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    // should be atomic
    portENTER_CRITICAL();
    seq_core_state.FOLLOW ^= 1;
  } else {
    // should be atomic
    portENTER_CRITICAL();
    // set mode
    seq_core_state.FOLLOW = depressed ? 0 : 1;
  }
  portEXIT_CRITICAL();

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Follow Mode", seq_core_state.FOLLOW ? "    on" : "   off");

  return 0; // no error
}

static s32 SEQ_UI_Button_Scrub(s32 depressed)
{
  // double function: -> Loop if menu button pressed
  if( seq_ui_button_state.MENU_PRESSED )
    return SEQ_UI_Button_Loop(depressed);

  if( seq_hwcfg_button_beh.scrub ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    seq_ui_button_state.SCRUB ^= 1;
  } else {
    // set mode
    seq_ui_button_state.SCRUB = depressed ? 0 : 1;
  }

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Scrub Mode", seq_ui_button_state.SCRUB ? "    on" : "   off");

  return 0; // no error
}

static s32 SEQ_UI_Button_TempoPreset(s32 depressed)
{
  static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;

  if( seq_hwcfg_button_beh.tempo_preset ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.TEMPO_PRESET ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.TEMPO_PRESET ^= 1; // toggle TEMPO_PRESET pressed (will also be released once GP button has been pressed)
  } else {
    // set mode
    seq_ui_button_state.TEMPO_PRESET = depressed ? 0 : 1;
  }

  if( seq_ui_button_state.TEMPO_PRESET ) {
    prev_page = ui_page;
    SEQ_UI_PageSet(SEQ_UI_PAGE_BPM_PRESETS);
  } else {
    if( ui_page == SEQ_UI_PAGE_BPM_PRESETS )
      SEQ_UI_PageSet(prev_page);
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_TapTempo(s32 depressed)
{
  seq_ui_button_state.TAP_TEMPO = depressed ? 0 : 1;

  if( depressed ) return -1; // ignore when button depressed

  return SEQ_UI_BPM_TapTempo();
}

static s32 SEQ_UI_Button_ExtRestart(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // should be atomic
  portENTER_CRITICAL();
  seq_core_state.EXT_RESTART_REQ = 1;
  portEXIT_CRITICAL();

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "External Restart", "requested");

  return 0; // no error
}

// FREEZE — the generator-mutation master switch, on the repurposed METRONOME
// button (this fork doesn't click to a metronome live). Toggles
// seq_core_state.FREEZE: engaged generator loops hold (the per-measure auto-
// mutate pauses), reversible. Two-face phrase recall falls out of this — recall
// while frozen lands the organism as static tape. Honors the button's existing
// toggle-vs-hold behaviour config (seq_hwcfg_button_beh.metronome).
static s32 SEQ_UI_Button_Freeze(s32 depressed)
{
  // double function preserved from the old METRONOME button: -> ExtRestart if
  // MENU is held (ExtRestart also keeps its own button / MIDI-note remote).
  if( seq_ui_button_state.MENU_PRESSED )
    return SEQ_UI_Button_ExtRestart(depressed);

  if( seq_hwcfg_button_beh.metronome ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    portENTER_CRITICAL();
    seq_core_state.FREEZE ^= 1;
  } else {
    // hold mode: frozen while held
    portENTER_CRITICAL();
    seq_core_state.FREEZE = depressed ? 0 : 1;
  }
  portEXIT_CRITICAL();

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "FREEZE", seq_core_state.FREEZE ? "  FROZEN" : "    live");

  return 0; // no error
}

s32 SEQ_UI_Button_Record(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // enable/disable recording
  SEQ_RECORD_Enable(seq_record_state.ENABLED ? 0 : 1, 1);

  SEQ_UI_Msg(SEQ_UI_MSG_USER_R,
	     1000,
	     seq_record_options.STEP_RECORD ? "Step Recording" : "Live Recording",
	     seq_record_state.ENABLED ? "      on" : "     off");

  return 0; // no error
}

static s32 SEQ_UI_Button_JamLive(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // enable live recording
  SEQ_UI_TRKJAM_RecordModeSet(0);

  // change to record page
  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKJAM);

  return 0; // no error
}

static s32 SEQ_UI_Button_JamStep(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // enable step recording
  SEQ_UI_TRKJAM_RecordModeSet(1);

  // change to record page
  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKJAM);

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// PROC sel-view (processor rack) shared helpers.
//
// The rack is the internal render stack (seq_processor_stack[track][4], fixed
// slot map in seq_core.h). In PROC view the B-row selects/focuses a slot, the
// GP encoders operate the focused processor, the GP row paints its 16-object
// shape (ChordMask's mask), and a persistent LCD overlay reads it back — one
// grammar, many processors.
//
// GOLDEN RULE: the UI never pokes seq_processor_stack. It writes through
// SEQ_CC_Set (or the processor's own setter for a global) so tcc, the slot
// mirror, and the render cache all stay coherent (the ChordMask/Tension/Pitch/
// Limit slot-syncs already run off SEQ_CC_Set); the UI only READS the slot for
// the rack/LED/LCD readout.
//
// G1 (2026-07-03): the operating surface is a **per-processor param list** — a
// proto-descriptor (study §3.5 invariant 5). The encoder routing, push-to-
// default, and LCD readout all ITERATE this list, so a slot is migrated onto the
// grammar by filling in a table, not by wiring a page. The table is keyed by the
// FIXED slot index (not the runtime id): Pitch/Tension/Limit read id==NONE while
// neutral yet must stay focusable — turning an encoder brings the slot alive via
// its sync (dark rack key = true pass-through, invariant 4). G2 hardens this into
// the full descriptor (custom surfaces, defaults registry, formatter table).
/////////////////////////////////////////////////////////////////////////////

// How a param is backed. Read/write switch on this so the table can stay a plain
// list of {label, cc, range, default} while the messy backings hide in one place.
typedef enum {
  PROC_KIND_CC = 0,   // plain CC value 0..hi                (ChordMask Str, Limit Lo/Hi, Tension Grip)
  PROC_KIND_BUS,      // ChordMask mask source 0..4: A..D (live bus) or Self (bit2, static mask)
  PROC_KIND_SNIBBLE,  // CC holds a signed 4-bit nibble, logical -8..+7 (Pitch Semi/Oct)
  PROC_KIND_FLAG,     // the FORCE_SCALE bit of MODE_FLAGS, 0/1 (Pitch FTS)
  PROC_KIND_GRAVITY,  // global s8 -64..+63 via SEQ_CORE_TensionGravitySet (Tension Grav)
  PROC_KIND_ECHO_REP, // Echo repeats: count in bits 0..5, 0x40 = disable flag. Masked RMW
                      //   so a count edit preserves the bypass bit (Echo, G1.5).
  PROC_KIND_ECHO_DLY, // Echo delay: the CC stores a non-contiguous INTERNAL value; the dial
                      //   operates in musical USER-index order (Map*ToInternal) (Echo).
  PROC_KIND_CM_STR,   // ChordMask strength: a plain CC value, but turning it up ENGAGES the
                      //   track's ChordMask playmode (the mode gates the slot) (ChordMask).
  PROC_KIND_SCALE,    // GLOBAL force-to-scale scale (seq_core_global_scale); name on the right
                      //   screen, dial clamps to the runtime scale count (Pitch).
  PROC_KIND_ROOT,     // GLOBAL scale root selection (0=Keyb, 1..12=C..B) (Pitch).
  PROC_KIND_SDEG,     // GLOBAL diatonic transpose (s8 ±degrees via WalkScale, FTS path) (Pitch).
  PROC_KIND_GRV_STYLE,// Groove style: index in bits 0..5, 0x40=sync, 0x80=disable. Masked RMW
                      //   preserving both flag bits; 0=off. Engage-seeds intensity (Groove, G1.6).
  PROC_KIND_GRV_SYNC, // Groove sync_to_track flag (bit 6 of GROOVE_STYLE): Trk / RefS (Groove).
  PROC_KIND_GRV_INTN, // Groove intensity: a plain CC write, but BROADCAST to every GLOBAL
                      //   track when this track is global (stock TRKGRV semantics via
                      //   GrooveEditMask; a local track edits itself alone) (Groove).
  PROC_KIND_GRV_GLOBAL,// Groove per-track GLOBAL flag: the INVERTED bit in
                      //   seq_groove_ui_local_selection (config-file state). on = this
                      //   track's Styl/Intn/Sync edits broadcast to all global tracks (Groove).
  PROC_KIND_GRV_STEPS,// Selected template's num_steps 1..16. Custom templates editable;
                      //   presets/off read-only (like the paint surface). No CC — template
                      //   state, persists via MBSEQ_G.V4 on page exit (Groove).
  PROC_KIND_GRV_VAL,  // The paint brush / hold-step editor (proc_groove_paint_val +
                      //   proc_groove_held_step — see those statics). No CC (Groove).
  PROC_KIND_GRV_LANE, // Groove GP-row paint lane selector: 0=Dly/1=Len/2=Vel. UI-only static,
                      //   no CC — picks which template lane the GP buttons paint (Groove).
  PROC_KIND_LFO_WAVE, // LFO waveform: index in bits 0..5 (0=off, 1..25 shapes), 0x80=disable.
                      //   Masked RMW; occ from !=0; engage-seeds Amp/Step/Rst (LFO, G1.7).
  PROC_KIND_LFO_AMP,  // LFO amplitude: BIPOLAR — CC 0..255, logical (raw-128) -128..+127.
                      //   128=center=pass-through (the first center-detent dial) (LFO).
  PROC_KIND_LFO_FLAG, // one bit of LFO_ENABLE_FLAGS as an on/off dial — the BIT NUMBER
                      //   lives in the param's cc slot (0=1Sht 1=Note 2=Vel 3=Len 4=CC
                      //   6=ClkD), NOT a CC index: needs its own read/write (LFO, 2026-07-13).
  PROC_KIND_LFO_XCC,  // the extra-CC stream's CC number (LFO_CC): 0 reads "---" (LFO).
  PROC_KIND_LFO_XCC_ON,// the extra-CC stream enable = INVERTED EXTRA_CC_OFF bit 5 (bit
                      //   number in cc slot, like LFO_FLAG); deflt 1 = on = raw 0 (LFO).
  PROC_KIND_LFO_PPQN, // LFO_CC_PPQN 0..8 -> displays the decoded rate 1/3/6/../384 (LFO).
                      // (LFO_TARG retired 2026-07-13 — the DEST plane exposes the flags
                      //   individually; the old single-select router forced EXTRA_CC_OFF.)
                      //   CC = the free-running stream (clears EXTRA_CC_OFF, not the CC bit) (LFO).
  PROC_KIND_ROBO_PROB,// Robotize overall probability (0..31). Headline: turning up ENGAGES
                      //   (sets robotize_active) and seeds the per-dim RANGES (Robotize, G2).
  PROC_KIND_ACTION,   // A momentary ACTION dial — turning is a no-op, ENCODER-PUSH executes.
                      //   `cc` holds the action id (PROC_ACT_*). The one non-snap push (G2).
  PROC_KIND_GEN_RANGE_LO, // PitchGen range_min (1..127). Reads/writes the pool-slot struct
                      //   directly (no CC) via SEQ_GENERATOR_Get; no-ops pre-ENGAGE (PitchGen).
  PROC_KIND_GEN_RANGE_HI, // PitchGen range_max (1..127), clamped >= range_min (PitchGen).
  PROC_KIND_GEN_RATE, // PitchGen mutation_rate 0..127 — per-measure touch probability.
                      //   0 is a valid ENGAGED state (frozen, not off) — unlike the other
                      //   emission rows' kind-0-means-off, so occupancy can't proxy off this
                      //   dial; ENGAGE is the B-row double-tap instead (PitchGen).
  PROC_KIND_GEN_DEPTH,// PitchGen mutation_depth 0..127 (0=frozen, 127=full reroll) (PitchGen).
  PROC_KIND_GEN_CONTOUR, // PitchGen contour_shape 0..3 (Uni/Lo/Hi/Tri reroll bias) (PitchGen).
  PROC_KIND_GEN_WINDOW,  // PitchGen/TrigGen shared GP-row window select 0..3 (which 16-step
                      //   quarter of the 64-step loop the GP row shows/edits). UI-only
                      //   static, no CC — one shared window across both rows (PitchGen/TrigGen).
  PROC_KIND_TGEN_DENSITY, // G3 TrigGen density (0..127 on-probability, shown as %). Same
                      //   struct field as PitchGen's range_min, read via the SEPARATE
                      //   trigger key-space pointer (SEQ_GENERATOR_TrgGet) (TrigGen).
  PROC_KIND_TGEN_RATE,    // G3 TrigGen mutation_rate — same semantics as PitchGen's Rate,
                      //   resolved via the trigger key-space (TrigGen).
  PROC_KIND_TGEN_DEPTH,   // G3 TrigGen mutation_depth — >=127 reroll(density) else flip;
                      //   resolved via the trigger key-space (TrigGen).
  PROC_KIND_HUM_VALUE, // Humanize intensity (HUMANIZE_VALUE, 0..127 — the row's full headline
                      //   range, so its disable bit lives on a separate cc; see disable_cc).
                      //   0->on turn seeds Mode to Note+Vel+Len if no bit is chosen yet, done
                      //   as one direct write, not via SeedRowDefaults (Humanize).
  PROC_KIND_HUM_NOTE, // Humanize Mode bit 0 (Note event humanized) (Humanize).
  PROC_KIND_HUM_VEL,  // Humanize Mode bit 1 (Velocity humanized) (Humanize).
  PROC_KIND_HUM_LEN,  // Humanize Mode bit 2 (Gatelength humanized) (Humanize).
  PROC_KIND_ARP_MODE, // Arp mode: a plain CC value (0=Off/1..4=Up/Down/UpDown/Random) — this
                      // kind exists purely so ParamPrintValue can print the name (Arp).
  PROC_KIND_ARP_BUS,  // Arp chord source: plain CC 0..4 (0=Self/1..4=bus A..D). Distinct from
                      // ChordMask's PROC_KIND_BUS: Self is 0 here (natural zero default), no
                      // bit-2 encoding. Read/write generic; kind exists only to print the name.
  PROC_KIND_VOICE_SPREAD, // Voicing spread: value in bits 0..3, 0x80 = the row's bypass bit.
                      //   Masked RMW so a spread edit preserves bypass (Echo idiom) (Voicing).
  PROC_KIND_VOICE_BIPOLAR, // Voicing Strm/Tilt: BIPOLAR CC, logical (raw-64) -63..+63
                      //   around a 64 raw centre. 0 = detent = off; sign = direction (Voicing).
  PROC_KIND_VOICE_DROP, // Voicing drop selector 0..3 — generic CC read/write, kind exists
                      //   so ParamPrintValue can name the voicing (off/Dp2/Dp3/D2+4) (Voicing).
  PROC_KIND_SPACER,   // a BLANK grid cell — visual group separator inside one row's dial
                      //   bank (Voic's voicing|range gap, 2026-07-12). Encoder inert: read=0,
                      //   write/push = no-op (its cc slot is 0 and must never reach SEQ_CC_Set).
  PROC_KIND_LIMIT_HI, // Voic's Hi clamp: pass-through at the TOP detent. The CC keeps the
                      //   stock encoding (0 = open top; the DSP substitutes 127), but the
                      //   dial presents it honestly: read 0 -> 127, write 127 -> 0. The
                      //   default/push detent is 127, so a resting row stays DARK (raw 0)
                      //   while the cell reads "127" = notes may reach the ceiling (Voic).
  PROC_KIND_SHADE,    // GRAVITY page's SHADE brightness ladder (a VIEW on the global
                      //   scale, not a CC): logical value = ladder pos 0..6 (Lyd..Loc),
                      //   -1 = off-ladder ("---"). Write < 0 is a NO-OP — push/reset must
                      //   never yank the global scale onto the ladder — so the table's
                      //   lo/deflt are -1. Divergence from the page: CCW from off-ladder
                      //   does nothing (the page jumps to Loc); enter the ladder CW (Tens).
} proc_pkind_t;

// Action ids for PROC_KIND_ACTION params (stored in the param's `cc` slot). PitchGen and
// TrigGen need DISTINCT ids (not shared) — action ids are global to RunAction's dispatch,
// which has no other way to know which accessor family (Get vs TrgGet) a given row's Roll/
// Anchor/Snap/Bounce push should call.
enum { PROC_ACT_RESEED = 1, PROC_ACT_FREEZE,
       PROC_ACT_GEN_ROLL, PROC_ACT_GEN_ANCHOR, PROC_ACT_GEN_SNAP, PROC_ACT_GEN_BOUNCE,
       PROC_ACT_TGEN_ROLL, PROC_ACT_TGEN_ANCHOR, PROC_ACT_TGEN_SNAP, PROC_ACT_TGEN_BOUNCE,
       PROC_ACT_GRV_CLEAR };

// How a param's VALUE is rendered (orthogonal to the backing kind). DEFAULT derives
// from the kind (int / bus / on-off / signed); the rest are per-param display maps so
// a plain CC can read out in its musical unit — the seed of the G2 formatter registry.
typedef enum {
  PROC_FMT_DEFAULT = 0, // int, or signed for SNIBBLE/GRAVITY
  PROC_FMT_PCT5,        // CC 0..40 shown as 0..200 percent (raw*5)   (Echo Vel/FbV/FbT/FbG)
  PROC_FMT_SEMI24,      // CC 0..48 shown as (raw-24) signed semitones (Echo FbN)
  PROC_FMT_PLUS1,       // value shown as v+1 (1-based counts)        (LFO Rate = steps/cycle)
  PROC_FMT_PCT,         // value shown as v% directly                 (LFO Phase 0..99)
  PROC_FMT_PCT127,      // value 0..127 shown as 0..100 percent (v*100/127) (TrigGen Density)
} proc_fmt_t;

typedef struct {
  const char  *label;  // short dial label (<= 4 chars — it heads a 5-col encoder cell)
  proc_pkind_t kind;
  u8           cc;      // SEQ_CC_* for CC/BUS/SNIBBLE/ECHO_*; ignored for FLAG/GRAVITY
  s8           lo, hi;  // logical range (signed for SNIBBLE/GRAVITY)
  s8           deflt;   // encoder-push / bypass target (the pass-through / detent value)
  proc_fmt_t   fmt;     // value display map (DEFAULT = derive from kind)
  s8           eng;     // engage-seed override, 0 = same as deflt (G2 defaults registry). Only
                        // needed where "make it audible on first touch" differs from the
                        // pass-through detent (Groove Intn, LFO Amp, Robotize Note/Vel/Len/Oct)
                        // — see SEQ_UI_PROC_SeedRowDefaults.
} proc_param_t;

// The rack is an ordered list of ROWS, not a raw walk of the render-stack slots.
// A row is backed EITHER by a render-stack slot (Ptch/Tension — PROC_ROW_STACK;
// absorbed slots ride other rows: Ptch fronts CHORDMASK, Voic fronts LIMIT, TGen
// fronts ARP — all 2026-07-12) OR by an emission-time effect's CCs (Echo —
// PROC_ROW_EMISSION:
// the DSP stays at emission per design §5; only the OPERATION joins the grammar,
// G1.5). ui_focused_proc_slot is now a ROW index. Every stack row carries its
// backing slot explicitly (.stack_slot) — row index != slot index since the Ptch
// row absorbed the ChordMask row (2026-07-12): row 0 fronts BOTH the PITCH and
// CHORDMASK slots. Migrating an effect onto the grammar = adding a row
// (a proc_param_t[] table + one occupancy predicate in SEQ_UI_PROC_RowState).
typedef enum {
  PROC_ROW_STACK = 0,  // occupancy/enable/strength from seq_processor_stack[track][stack_slot]
  PROC_ROW_EMISSION,   // occupancy derived from the effect's tcc CCs (no stack slot)
  PROC_ROW_GENERATOR,  // occupancy from a SEQ_GENERATOR_* pool-slot allocation (no CC at all;
                       // G2 — PitchGen, the first genuinely CONTINUOUS/self-mutating tenant)
} proc_rowkind_t;

// A bespoke full-plane face id (G2). 0 = a plain dial bank; else a hand-drawn surface
// (its GP-row / GP-button / right-screen branches keyed on this id, not a per-slot compare).
typedef enum {
  PROC_FACE_NONE = 0,
  PROC_FACE_ROBOLOOP,       // Robotize's LOOP plane: the 16 bar-anchors + reseed/freeze/reroll
  PROC_FACE_PITCHGEN_STEPS, // PitchGen's STEPS plane: a 16-step LOCK window into the 64-step loop
  PROC_FACE_TRIGGEN_STEPS,  // G3 TrigGen's STEPS plane: same shape, trigger key-space
  // Faces on a row's PRIMARY (only) plane — these tenants never had a 2nd plane to reach
  // via ‹/›, so their bespoke GP-row surface rides the row's face1 instead of face2.
  PROC_FACE_CHORDMASK_SELF, // ChordMask's Self mask: GP1-12 toggle PCs (bus mode == Self only)
  PROC_FACE_GROOVE_PAINT,   // Groove's paintable 16-step shape (custom templates only)
  PROC_FACE_LFO_PALETTE,    // LFO's waveform palette: tap a GP button to pick a shape
  PROC_FACE_TENSION_ZONES,  // Tension's zone jump: GP9-15 = DRONE..SLIP, GP16 = RESOLVE
} proc_face_t;

typedef struct {
  const char         *name;
  // <=4-char identity for the page header's right-aligned block ("Ptch  1/12 G1T1",
  // base layout 2026-07-12); .name stays full-length for B-row messages.
  const char         *abbr;
  proc_rowkind_t      rowkind;
  u8                  stack_slot;    // PROC_ROW_STACK only
  const proc_param_t *params;
  u8                  n_params;
  // Bespoke face on the PRIMARY (only) plane — NONE for a plain dial bank. Parallels face2
  // below but for rows that never had a 2nd plane to reach via ‹/› (ChordMask/Groove/LFO's
  // GP-row paint surfaces). See SEQ_UI_PROC_CurFace.
  proc_face_t         face1;
  // PROC_ROW_EMISSION only: the CC carrying the row's occupancy (count/style in bits
  // 0..5) and, if non-zero, the bit that BYPASSES the effect while keeping its config.
  // This is the one generalisation of G1.5's ECHO_REPEATS hardcode — RowState + the
  // B-row double-tap read these instead of naming a specific CC (Echo 0x40 / Groove 0x80).
  u8                  occ_cc;
  u8                  disable_mask;
  // disable_mask normally packs into occ_cc's own spare bits (Echo/Groove/LFO — their
  // headline dial never uses its CC's full 0..127 range). When it doesn't (Humanize's
  // Value IS 0..127), disable_cc names the OTHER cc the mask bit lives on instead; 0 =
  // same byte as occ_cc (every prior tenant). Both RowState and the B-row double-tap
  // read this before falling back to occ_cc.
  u8                  disable_cc;
  // enabled bit lives in a SEPARATE CC (Robotize: occupancy = PROBABILITY>0, enable =
  // ACTIVE). 0 = none. disable_mask takes precedence when both set (G2, occupancy split).
  u8                  enable_cc;
  // Optional 2nd PLANE, reached by the uniform ‹/› toggle (G2). A plane is a dial bank
  // (params2/n_params2, face2=NONE) or a bespoke face (face2 != NONE, its own GP surface).
  // NULL/0 = single-plane row. Generalise to a planes[] array only when a 3rd is needed.
  const proc_param_t *params2;
  u8                  n_params2;
  proc_face_t         face2;
  // Optional plane NAMES for the row-0 cue ("CONF 1/2"/"DEST 2/2" — LFO, 2026-07-13).
  // NULL = the defaults: plane 1 "OPER", plane 2 from face2 (LOOP/STEP) or "CFG".
  const char         *p1name;
  const char         *p2name;
  // Optional per-row CUSTOM right-screen readout (line 1) — style/waveform names, engaged
  // state, loop status, whatever doesn't fit a 4-char cell. NULL = the generic rack draws
  // nothing extra there (Echo). Replaces a 7-tenant if-chain (G2 defaults registry).
  void (*status)(u8 track, u8 slot);
} proc_row_t;

// Forward decls for proc_rows[]'s .status hooks — defined further down, once their
// dependencies (SEQ_UI_PROC_CurFace, SEQ_UI_PROC_LfoWaveName, ...) exist.
static void SEQ_UI_PROC_Status_Pitch(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_Tension(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_Groove(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_Robotize(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_PitchGen(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_TrigGen(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_Humanize(u8 track, u8 slot);
static void SEQ_UI_PROC_Status_Voicing(u8 track, u8 slot);

// Ptch — the merged pitch-domain cockpit (2026-07-12): the old Pitch row (transpose
// Semi/Oct + force-to-scale FTS with the GLOBAL Scale/Root/Deg it snaps to) absorbed
// the ChordMask row (Str/Bus + the GP-button mask face), filling all 8 encoder cells.
// Scale/Root/Deg are global (shared by all tracks + the keyboard, like Tension's
// Grav) — the same globals the Scale page edits; changing them re-renders every
// force-scale track. The scale NAME doesn't fit a cell, so it's shown on the right
// screen (row 0); the dial cell shows the scale index. Root reads out in its cell
// (Keyb/C..B). The row fronts TWO stack slots (PITCH + CHORDMASK) — see RowState.
static const proc_param_t proc_params_pitch[] = {
  { "Semi", PROC_KIND_SNIBBLE, SEQ_CC_TRANSPOSE_SEMI,     -8,   7, 0, PROC_FMT_DEFAULT },
  { "Oct",  PROC_KIND_SNIBBLE, SEQ_CC_TRANSPOSE_OCT,      -8,   7, 0, PROC_FMT_DEFAULT },
  { "Str",  PROC_KIND_CM_STR,  SEQ_CC_CHORDMASK_STRENGTH,  0, 127, 0, PROC_FMT_DEFAULT },
  { "Bus",  PROC_KIND_BUS,     SEQ_CC_CHORDMASK_BUS,       0,   4, 0, PROC_FMT_DEFAULT },
  { "FTS",  PROC_KIND_FLAG,    0,                          0,   1, 0, PROC_FMT_DEFAULT },
  { "Scle", PROC_KIND_SCALE,   0,                          0, 127, 0, PROC_FMT_DEFAULT },
  { "Root", PROC_KIND_ROOT,    0,                          0,  12, 0, PROC_FMT_DEFAULT },
  { "Deg",  PROC_KIND_SDEG,    0,                        -14,  14, 0, PROC_FMT_DEFAULT },
};
// Tension — expanded 2026-07-12: Shade (the GRAVITY page's brightness ladder — a view
// on the GLOBAL scale) joins the row, and FTS is DOUBLED here from Ptch for
// convenience (same per-track flag; GRAVITY and force-to-scale play as one
// instrument). Grouping: field dials | gap | scale flag (the Voic pattern).
static const proc_param_t proc_params_tension[] = {
  { "Grip",  PROC_KIND_CC,      SEQ_CC_TENSION_GRIP, 0, 127,  0, PROC_FMT_DEFAULT },
  { "Grav",  PROC_KIND_GRAVITY, 0,                 -64,  63,  0, PROC_FMT_DEFAULT },
  { "Shade", PROC_KIND_SHADE,   0,                  -1,   6, -1, PROC_FMT_DEFAULT },
  { "",      PROC_KIND_SPACER,  0,                   0,   0,  0, PROC_FMT_DEFAULT },
  { "FTS",   PROC_KIND_FLAG,    0,                   0,   1,  0, PROC_FMT_DEFAULT },
};
// Echo — the full G1.5 reference tenant: every dial exposed, each read out in its
// own musical unit. Rpt is the headline/strength dial (0 = true pass-through, dark
// row) and its 0->on turn seeds the neutral detents so a fresh track is audible at
// once (SEQ_UI_PROC_ParamWrite). The feedback dials are NOT zero-centred: 100%/
// no-change sits at 20 (velocity/ticks/gate, shown as %) and 0 semitones at 24
// (FbN, shown signed). Delay operates in musical order and reads out as a note name.
// FbN is capped at 48 (=+24 st): the raw CC's 49 is a "random pitch" MODE, an
// unlabelled top-detent discontinuity, held out of the clean sweep for now.
// Labels use the stock FX-echo vocabulary (Repeats/Delay/Vel.Level/FB Velocity/Note/
// Ticks/Gatelen.) truncated to fit the 4-char encoder cell — so a dial reads as what it
// is. Vel = initial level; FbV = per-repeat feedback velocity; Note/Tick/Gate = the
// per-repeat feedback offsets.
static const proc_param_t proc_params_echo[] = {
  { "Rpt",  PROC_KIND_ECHO_REP, SEQ_CC_ECHO_REPEATS,      0, 15,  0, PROC_FMT_DEFAULT },
  { "Dly",  PROC_KIND_ECHO_DLY, SEQ_CC_ECHO_DELAY,        0, 22,  8, PROC_FMT_DEFAULT },
  { "Vel",  PROC_KIND_CC,       SEQ_CC_ECHO_VELOCITY,     0, 40, 20, PROC_FMT_PCT5   },
  { "FbV",  PROC_KIND_CC,       SEQ_CC_ECHO_FB_VELOCITY,  0, 40, 20, PROC_FMT_PCT5   },
  { "Note", PROC_KIND_CC,       SEQ_CC_ECHO_FB_NOTE,      0, 48, 24, PROC_FMT_SEMI24 },
  { "Tick", PROC_KIND_CC,       SEQ_CC_ECHO_FB_TICKS,     0, 40, 20, PROC_FMT_PCT5   },
  { "Gate", PROC_KIND_CC,       SEQ_CC_ECHO_FB_GATELENGTH,0, 40, 20, PROC_FMT_PCT5   },
};
// Groove — the 2nd emission tenant (G1.6, "the config-copy archetype"). Styl is the
// headline/occupancy dial (0 = off, dark row; 1..6 presets, 7..22 custom templates;
// name on the right screen, like Pitch's Scale). Intn = intensity (scales the VPOS/
// VNEG template cells — on the classic Shuffle it IS the swing depth; 0 = pass-through).
// Sync = the phase reference (Trk vs RefS). Lane picks which of the template's three
// lanes the GP button row paints (Dly/Len/Vel). The GP row itself paints the 16-step
// shape (custom templates only) — see SEQ_UI_PROC_page_Button. hi for Styl is the
// static preset+template count-1; deflts are the pass-through detents.
// 2026-07-13 (Grve mock): the row fills all 8 cells. Glob = the stock TRKGRV global
// flag ON the rack — with it on, Styl/Intn/Sync broadcast to every global track.
// Stps = the template's step count (custom-editable). Val = the paint brush (0 =
// intensity-follow; else literal signed offsets) doubling as the HOLD-STEP editor:
// hold a GP button + turn Val to dial that step's exact cell. Clr = whole-template
// reset (SEQ_GROOVE_Clear; presets refuse).
static const proc_param_t proc_params_groove[] = {
  { "Styl", PROC_KIND_GRV_STYLE,  SEQ_CC_GROOVE_STYLE, 0,
    (SEQ_GROOVE_NUM_PRESETS + SEQ_GROOVE_NUM_TEMPLATES - 1), 0, PROC_FMT_DEFAULT },
  { "Intn", PROC_KIND_GRV_INTN,   SEQ_CC_GROOVE_VALUE, 0, 127,  0, PROC_FMT_DEFAULT, 32 },
  { "Glob", PROC_KIND_GRV_GLOBAL, 0,                   0,   1,  1, PROC_FMT_DEFAULT },
  { "Sync", PROC_KIND_GRV_SYNC,   SEQ_CC_GROOVE_STYLE, 0,   1,  0, PROC_FMT_DEFAULT },
  { "Stps", PROC_KIND_GRV_STEPS,  0,                   1,  16, 16, PROC_FMT_DEFAULT },
  { "Lane", PROC_KIND_GRV_LANE,   0,                   0,   2,  0, PROC_FMT_DEFAULT },
  { "Val",  PROC_KIND_GRV_VAL,    0,                -126, 126,  0, PROC_FMT_DEFAULT },
  { "Clr",  PROC_KIND_ACTION, PROC_ACT_GRV_CLEAR,    0,   0,  0, PROC_FMT_DEFAULT },
};
// LFO — the 3rd emission tenant (G1.7), the rack's first MODULATION SOURCE. Wave is
// the headline/occupancy (0=off; 1..25 shapes; engage-seeds Amp so it's audible at
// once). Amp is bipolar — logical -128..+127 around a 128 raw centre (0 = detent).
// 2026-07-13 (LFO mock): the row expands to the FULL stock LFO parameter set across
// two DIAL-BANK planes — CONF (shape: Wave Amp Phas Step Rst 1Sht ClkD; the waveform
// palette face rides this plane) and DEST (routing: the four enable flags, plus the
// extra-CC stream — number/enable/offset/PPQN). First row with NAMED planes (CONF/
// DEST via p1name/p2name — no "OPER" here). The old single-select Targ router is
// retired: the flags are independent now, exactly like the stock FX_LFO page.
static const proc_param_t proc_params_lfo[] = { // CONF plane
  { "Wave", PROC_KIND_LFO_WAVE, SEQ_CC_LFO_WAVEFORM, 0,  25,  0, PROC_FMT_DEFAULT },
  { "Amp",  PROC_KIND_LFO_AMP,  SEQ_CC_LFO_AMPLITUDE, -128, 127, 0, PROC_FMT_DEFAULT, 96 },
  { "Phas", PROC_KIND_CC,       SEQ_CC_LFO_PHASE,     0,  99,  0, PROC_FMT_PCT     },
  { "Step", PROC_KIND_CC,       SEQ_CC_LFO_STEPS,     0, 255, 15, PROC_FMT_PLUS1   },
  { "Rst",  PROC_KIND_CC,       SEQ_CC_LFO_STEPS_RST, 0, 255, 15, PROC_FMT_PLUS1   },
  { "1Sht", PROC_KIND_LFO_FLAG, 0,                    0,   1,  0, PROC_FMT_DEFAULT },
  { "ClkD", PROC_KIND_LFO_FLAG, 6,                    0,   1,  0, PROC_FMT_DEFAULT },
};
static const proc_param_t proc_params_lfo_dest[] = { // DEST plane
  { "Note", PROC_KIND_LFO_FLAG,   1,                    0,   1, 0, PROC_FMT_DEFAULT },
  { "Vel",  PROC_KIND_LFO_FLAG,   2,                    0,   1, 0, PROC_FMT_DEFAULT },
  { "Len",  PROC_KIND_LFO_FLAG,   3,                    0,   1, 0, PROC_FMT_DEFAULT },
  { "CC",   PROC_KIND_LFO_FLAG,   4,                    0,   1, 0, PROC_FMT_DEFAULT },
  { "xCC#", PROC_KIND_LFO_XCC,    SEQ_CC_LFO_CC,        0, 127, 0, PROC_FMT_DEFAULT },
  { "xCC",  PROC_KIND_LFO_XCC_ON, 5,                    0,   1, 1, PROC_FMT_DEFAULT },
  { "Offs", PROC_KIND_CC,         SEQ_CC_LFO_CC_OFFSET, 0, 127, 0, PROC_FMT_DEFAULT },
  { "PPQN", PROC_KIND_LFO_PPQN,   SEQ_CC_LFO_CC_PPQN,   0,   8, 0, PROC_FMT_DEFAULT },
};
// Robotize — the 4th emission tenant (G2), and the first TWO-PLANE row. Plane A (OPERATE)
// is "how much chaos": Prob is the headline (engages robotize_active + seeds the per-dim
// ranges so the probability dials have something to move); Note/Vel/Len/Oct/Skip are the
// per-dimension probabilities (0..31). Occupancy = PROBABILITY>0; enable = ACTIVE (a split
// the {occ_cc,disable_mask} model can't express, so the row also carries enable_cc).
static const proc_param_t proc_params_robo_op[] = {
  { "Prob", PROC_KIND_ROBO_PROB, SEQ_CC_ROBOTIZE_PROBABILITY,      0, 31, 0, PROC_FMT_DEFAULT },
  { "Note", PROC_KIND_CC,        SEQ_CC_ROBOTIZE_NOTE_PROBABILITY, 0, 31, 0, PROC_FMT_DEFAULT, 5 },
  { "Vel",  PROC_KIND_CC,        SEQ_CC_ROBOTIZE_VEL_PROBABILITY,  0, 31, 0, PROC_FMT_DEFAULT, 32 },
  { "Len",  PROC_KIND_CC,        SEQ_CC_ROBOTIZE_LEN_PROBABILITY,  0, 31, 0, PROC_FMT_DEFAULT, 32 },
  { "Oct",  PROC_KIND_CC,        SEQ_CC_ROBOTIZE_OCT_PROBABILITY,  0, 31, 0, PROC_FMT_DEFAULT, 1 },
  { "Skip", PROC_KIND_CC,        SEQ_CC_ROBOTIZE_SKIP_PROBABILITY, 0, 31, 0, PROC_FMT_DEFAULT },
};
// Plane B (LOOP) — the ROBOLOOP bar-anchor machine. Dials Cyc/Pal/Strt/Rot shape the loop
// window; Rsd/Frz are ACTION dials (encoder-push executes reseed / freeze). The GP row is
// the 16 bar-anchors (tap = reroll) — the PROC_FACE_ROBOLOOP bespoke surface.
static const proc_param_t proc_params_robo_loop[] = {
  { "Cyc",  PROC_KIND_CC,     SEQ_CC_ROBOTIZE_LOOP_CYCLES,    0, 16,  0, PROC_FMT_DEFAULT },
  { "Pal",  PROC_KIND_CC,     SEQ_CC_ROBOTIZE_PALETTE_LENGTH, 1, 16, 16, PROC_FMT_DEFAULT },
  { "Strt", PROC_KIND_CC,     SEQ_CC_ROBOTIZE_LOOP_START,     0, 15,  0, PROC_FMT_DEFAULT },
  { "Rot",  PROC_KIND_CC,     SEQ_CC_ROBOTIZE_LOOP_ROTATE,    0, 15,  0, PROC_FMT_DEFAULT },
  { "Rsd",  PROC_KIND_ACTION, PROC_ACT_RESEED,                0,  0,  0, PROC_FMT_DEFAULT },
  { "Frz",  PROC_KIND_ACTION, PROC_ACT_FREEZE,                0,  0,  0, PROC_FMT_DEFAULT },
};
// PitchGen — the rack's first GENERATOR row (G2): a continuous, self-mutating tenant, not
// an emission effect. Its state lives in a SEQ_GENERATOR_* pool slot (ENGAGE/DISENGAGE/
// BOUNCE), not a CC, so PROC_ROW_GENERATOR reads it via SEQ_GENERATOR_Get/IsEngaged instead
// of {occ_cc,...}. Plane A (OPERATE) — "how much chaos": Lo/Hi (range), Rate/Dpth (touch
// probability / how far), Cont (reroll bias), Roll (ACTION — on-demand reroll of unlocked
// steps). Dials no-op until ENGAGED (mirrors the stock PITCHGEN page's own contract) — the
// B-row DOUBLE-TAP is ENGAGE<->DISENGAGE (Rate=0 is a valid engaged/frozen state here, unlike
// the other rows' kind-0-means-off, so no dial can proxy occupancy the way Groove/LFO/
// Robotize's headline dial does). deflts mirror SEQ_GENERATOR_Engage's own seed values.
// 2026-07-12 (PGen mock): Roll rides cell 8 on BOTH planes — one physical "dice"
// encoder, reachable without a plane flip (the FTS-doubling idiom; spacers pad the gap).
static const proc_param_t proc_params_pitchgen_op[] = {
  { "Lo",   PROC_KIND_GEN_RANGE_LO, 0, 1, 127, SEQ_GENERATOR_DEFAULT_RANGE_MIN, PROC_FMT_DEFAULT },
  { "Hi",   PROC_KIND_GEN_RANGE_HI, 0, 1, 127, SEQ_GENERATOR_DEFAULT_RANGE_MAX, PROC_FMT_DEFAULT },
  { "Rate", PROC_KIND_GEN_RATE,     0, 0, 127, SEQ_GENERATOR_DEFAULT_RATE,      PROC_FMT_DEFAULT },
  { "Dpth", PROC_KIND_GEN_DEPTH,    0, 0, 127, SEQ_GENERATOR_DEFAULT_DEPTH,     PROC_FMT_DEFAULT },
  { "Cont", PROC_KIND_GEN_CONTOUR,  0, 0,   3, SEQ_GENERATOR_DEFAULT_CONTOUR,   PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "Roll", PROC_KIND_ACTION, PROC_ACT_GEN_ROLL, 0, 0, 0, PROC_FMT_DEFAULT },
};
// Plane B (STEPS) — the loop's IDENTITY face. Win picks which 16-step quarter of the 64-step
// loop the GP row shows (UI-only, no CC — mirrors Groove's Lane selector); GP row = LOCK
// toggle for that window (the paintable-shape idiom, 4th tenant now). Anc/Snp/Bnc are ACTIONS:
// Anchor = snapshot current loop as identity; Snap = hard-restore it; Bounce = freeze the loop
// into the source and free the slot (the generator's own "harvest to static" verb).
static const proc_param_t proc_params_pitchgen_steps[] = {
  { "Win",  PROC_KIND_GEN_WINDOW, 0, 0, 3, 0, PROC_FMT_DEFAULT },
  { "Anc",  PROC_KIND_ACTION, PROC_ACT_GEN_ANCHOR, 0, 0, 0, PROC_FMT_DEFAULT },
  { "Snp",  PROC_KIND_ACTION, PROC_ACT_GEN_SNAP,   0, 0, 0, PROC_FMT_DEFAULT },
  { "Bnc",  PROC_KIND_ACTION, PROC_ACT_GEN_BOUNCE, 0, 0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "Roll", PROC_KIND_ACTION, PROC_ACT_GEN_ROLL,   0, 0, 0, PROC_FMT_DEFAULT },
};
// TrigGen — G3, the trigger Turing machine. The genuine gap named when this arc started:
// GENERATE's five types (Eucl/CA/Poly/Sub/Lsys) are static one-shot fills; nothing writes
// triggers LIVE. Same mechanics as PitchGen (lock/rate/depth/anchor/roll/bounce), now
// writing 0/1 into the track's assigned GATE trigger layer. Lives in a SEPARATE pool
// key-space from PitchGen (SEQ_GENERATOR_Trg*) so both can run at once on one melodic
// track — decoupled pitch + rhythm, the actual point of building this. No Contour: a coin
// flip has no distribution shape, so there's no boolean analogue to expose.
// 2026-07-12 (TGen mock): the ARP row dissolved into cells 6-7 here — rhythm generator
// and arpeggiator share one page ("figure it fit well enough"). Arp = the old Mode
// headline (0=Off/dark, 1..4=Up/Down/UpDown/Random); Bus = the chord source: Self
// (paint the chord on the Ptch row's GP1-12 mask face; shares chordmask_mask_h/l, 0
// default) or a live bus A..D. Both write plain CCs — SEQ_CC_Set runs ArpSlotSync.
// The row fronts the ARP stack slot alongside its generator pool slot (RowState).
static const proc_param_t proc_params_triggen_op[] = {
  { "Dens", PROC_KIND_TGEN_DENSITY, 0, 0, 127, SEQ_GENERATOR_DEFAULT_DENSITY, PROC_FMT_PCT127 },
  { "Rate", PROC_KIND_TGEN_RATE,    0, 0, 127, SEQ_GENERATOR_DEFAULT_RATE,    PROC_FMT_DEFAULT },
  { "Dpth", PROC_KIND_TGEN_DEPTH,   0, 0, 127, SEQ_GENERATOR_DEFAULT_DEPTH,   PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "Arp",  PROC_KIND_ARP_MODE, SEQ_CC_ARP_MODE, 0, 4, 0, PROC_FMT_DEFAULT },
  { "Bus",  PROC_KIND_ARP_BUS,  SEQ_CC_ARP_BUS,  0, 4, 0, PROC_FMT_DEFAULT },
  { "Roll", PROC_KIND_ACTION, PROC_ACT_TGEN_ROLL, 0, 0, 0, PROC_FMT_DEFAULT },
};
// Plane B (STEPS) — same shape as PitchGen's, same shared Win state (GEN_WINDOW), trigger
// key-space actions. GP row = LOCK toggle for the window (PROC_FACE_TRIGGEN_STEPS).
static const proc_param_t proc_params_triggen_steps[] = {
  { "Win",  PROC_KIND_GEN_WINDOW, 0, 0, 3, 0, PROC_FMT_DEFAULT },
  { "Anc",  PROC_KIND_ACTION, PROC_ACT_TGEN_ANCHOR, 0, 0, 0, PROC_FMT_DEFAULT },
  { "Snp",  PROC_KIND_ACTION, PROC_ACT_TGEN_SNAP,   0, 0, 0, PROC_FMT_DEFAULT },
  { "Bnc",  PROC_KIND_ACTION, PROC_ACT_TGEN_BOUNCE, 0, 0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,       0, 0,   0, 0, PROC_FMT_DEFAULT },
  { "Roll", PROC_KIND_ACTION, PROC_ACT_TGEN_ROLL,   0, 0, 0, PROC_FMT_DEFAULT },
};
// Humanize — the 5th emission tenant, EXPOSE-IN-PLACE of the stock seq_humanize.c /
// seq_ui_fx_humanize.c module. Int is the headline/occupancy dial (HUMANIZE_VALUE,
// 0..127 — full range, no packing room), 0=off. Note/Vel/Len toggle HUMANIZE_MODE's
// bits 0..2. Turning Int up from 0 seeds Mode to all three if none is chosen yet
// (PROC_KIND_HUM_VALUE) so the dial is audible at once. Because Value has no spare bit
// for a packed disable flag the way Echo/Groove/LFO's headline CCs do, bypass lives on
// Mode instead — the row's disable_cc points there rather than at occ_cc (the one new
// field this tenant needed; see proc_row_t). BUT `humanize_mode` is a 4-bit struct
// bitfield (`seq_cc.h`), not a full byte — bits 0..2 are Note/Vel/Len, so bit 3 (0x08)
// is the ONLY spare bit available, not bit 7 (an earlier pass used 0x80, which
// silently truncated away on every write and never actually gated the DSP — fixed by
// moving to bit 3 here AND teaching `SEQ_HUMANIZE_Event` to check it).
static const proc_param_t proc_params_humanize[] = {
  { "Int",  PROC_KIND_HUM_VALUE, SEQ_CC_HUMANIZE_VALUE, 0, 127, 0, PROC_FMT_DEFAULT },
  { "Note", PROC_KIND_HUM_NOTE,  SEQ_CC_HUMANIZE_MODE,  0,   1, 0, PROC_FMT_DEFAULT },
  { "Vel",  PROC_KIND_HUM_VEL,   SEQ_CC_HUMANIZE_MODE,  0,   1, 0, PROC_FMT_DEFAULT },
  { "Len",  PROC_KIND_HUM_LEN,   SEQ_CC_HUMANIZE_MODE,  0,   1, 0, PROC_FMT_DEFAULT },
};
// Voicing — the 6th emission tenant (POC): parametric voicing of the INTERNAL
// chord-layer expansion (Chord1/2/3 par layers; no-op on plain Note tracks — the
// .status hook says so on-screen). All three dials are a pure function of the
// chord byte, so capture/bounce reproduce them by re-expansion (deterministic
// SHAPING — preserved by the bounce reset, like Groove). Sprd opens the voicing
// upward an octave-click at a time (bottom voice anchored); Inv walks classic
// inversions (+ lifts the lowest voice, - drops the highest); Strm is bipolar
// timing — ticks per voice, up-strum CW / down-strum CCW around the detent.
// Occupancy is ANY dial off-neutral (custom RowState arm — no single headline CC
// can proxy it); bypass = bit 7 of SPREAD, gating the voicing dials in the DSP.
// 2026-07-12: the row absorbed the Limit row — cells 7-8 are the note-range clamp
// (the LIMIT stack slot's CCs), with a SPACER cell between the two groups
// (voicing | range). At rest the range reads as the full keyboard — Lo 0 = open
// floor, Hi 127 = open ceiling (PROC_KIND_LIMIT_HI stores that as the stock "off"
// 0, keeping the slot dark) — and the clamp is INDEPENDENT of the row's bypass:
// the 0x80 bit only gates the voicing dials. The row fronts the LIMIT stack slot
// alongside its emission CCs — see the RowState arm.
static const proc_param_t proc_params_voicing[] = {
  { "Sprd", PROC_KIND_VOICE_SPREAD,  SEQ_CC_VOICE_SPREAD,   0,  12, 0, PROC_FMT_DEFAULT },
  { "Inv",  PROC_KIND_SNIBBLE,       SEQ_CC_VOICE_INV,     -8,   7, 0, PROC_FMT_DEFAULT },
  { "Drop", PROC_KIND_VOICE_DROP,    SEQ_CC_VOICE_DROP,     0,   3, 0, PROC_FMT_DEFAULT },
  { "Strm", PROC_KIND_VOICE_BIPOLAR, SEQ_CC_VOICE_STRUM,  -63,  63, 0, PROC_FMT_DEFAULT },
  { "Tilt", PROC_KIND_VOICE_BIPOLAR, SEQ_CC_VOICE_TILT,   -63,  63, 0, PROC_FMT_DEFAULT },
  { "",     PROC_KIND_SPACER,        0,                     0,   0,   0, PROC_FMT_DEFAULT },
  { "Lo",   PROC_KIND_CC,            SEQ_CC_LIMIT_LOWER,    0, 127,   0, PROC_FMT_DEFAULT },
  { "Hi",   PROC_KIND_LIMIT_HI,      SEQ_CC_LIMIT_UPPER,    1, 127, 127, PROC_FMT_DEFAULT },
};

static const proc_row_t proc_rows[] = {
  { .name = "Pitch",     .abbr = "Ptch", .rowkind = PROC_ROW_STACK, .stack_slot = SEQ_CORE_PITCH_SLOT,
    .params = proc_params_pitch,     .n_params = 8, .face1 = PROC_FACE_CHORDMASK_SELF,
    .status = SEQ_UI_PROC_Status_Pitch },
  // Voicing sits at position 2 (2026-07-12 merge): the pitch-SPACE rows cluster at the
  // top of the rack — Ptch shapes what the notes ARE, Voic shapes where they SIT
  // (voicing spread/inversion + the absorbed Limit range clamp).
  { .name = "Voicing",   .abbr = "Voic", .rowkind = PROC_ROW_EMISSION,
    .params = proc_params_voicing,   .n_params = 8,
    .occ_cc = SEQ_CC_VOICE_SPREAD, .disable_mask = 0x80, // double-tap bypass; occupancy is a custom RowState arm
    .status = SEQ_UI_PROC_Status_Voicing },
  { .name = "Tension",   .abbr = "Tens", .rowkind = PROC_ROW_STACK, .stack_slot = SEQ_CORE_TENSION_SLOT,
    .params = proc_params_tension,   .n_params = 5, .face1 = PROC_FACE_TENSION_ZONES,
    .status = SEQ_UI_PROC_Status_Tension },
  // PGen/TGen at positions 4/5 (2026-07-12 mocks "4/12"/"5/12"): the mock-by-mock
  // rack rebuild puts the pitch/generative cluster up top — Ptch, Voic, Tens, PGen,
  // TGen. The ARP row dissolved into TGen's OPER plane (cells 6-7).
  { .name = "PitchGen",  .abbr = "PGen", .rowkind = PROC_ROW_GENERATOR,
    .params = proc_params_pitchgen_op, .n_params = 8,
    .params2 = proc_params_pitchgen_steps, .n_params2 = 8, .face2 = PROC_FACE_PITCHGEN_STEPS,
    .status = SEQ_UI_PROC_Status_PitchGen },
  { .name = "TrigGen",   .abbr = "TGen", .rowkind = PROC_ROW_GENERATOR,
    .params = proc_params_triggen_op,  .n_params = 8,
    .params2 = proc_params_triggen_steps, .n_params2 = 8, .face2 = PROC_FACE_TRIGGEN_STEPS,
    .status = SEQ_UI_PROC_Status_TrigGen },
  // Grve at position 6 (2026-07-13 mock): the timing/feel row leads the emission tail.
  { .name = "Groove",    .abbr = "Grve", .rowkind = PROC_ROW_EMISSION,
    .params = proc_params_groove,    .n_params = 8,
    .occ_cc = SEQ_CC_GROOVE_STYLE, .disable_mask = 0x80, .face1 = PROC_FACE_GROOVE_PAINT,
    .status = SEQ_UI_PROC_Status_Groove },
  // Humn at position 7 (2026-07-13 mock): the feel pair (Grve, Humn) sits together.
  { .name = "Humanize",  .abbr = "Humn", .rowkind = PROC_ROW_EMISSION,
    .params = proc_params_humanize,  .n_params = 4,
    .occ_cc = SEQ_CC_HUMANIZE_VALUE, .disable_mask = 0x08, .disable_cc = SEQ_CC_HUMANIZE_MODE,
    .status = SEQ_UI_PROC_Status_Humanize },
  // Robo at position 8 (2026-07-13 mock — both planes already matched it exactly).
  { .name = "Robotize",  .abbr = "Robo", .rowkind = PROC_ROW_EMISSION,
    .params = proc_params_robo_op,   .n_params = 6,
    .occ_cc = SEQ_CC_ROBOTIZE_PROBABILITY, .enable_cc = SEQ_CC_ROBOTIZE_ACTIVE,
    .params2 = proc_params_robo_loop, .n_params2 = 6, .face2 = PROC_FACE_ROBOLOOP,
    .status = SEQ_UI_PROC_Status_Robotize },
  { .name = "Echo",      .abbr = "Echo", .rowkind = PROC_ROW_EMISSION,
    .params = proc_params_echo,      .n_params = 7,
    .occ_cc = SEQ_CC_ECHO_REPEATS, .disable_mask = 0x40 },
  { .name = "LFO",       .abbr = "LFO",  .rowkind = PROC_ROW_EMISSION,
    .params = proc_params_lfo,       .n_params = 7,
    .occ_cc = SEQ_CC_LFO_WAVEFORM, .disable_mask = 0x80, .face1 = PROC_FACE_LFO_PALETTE,
    .params2 = proc_params_lfo_dest, .n_params2 = 8,
    .p1name = "CONF", .p2name = "DEST" },
};
#define PROC_NUM_ROWS ((u8)(sizeof(proc_rows)/sizeof(proc_rows[0])))
// The merged pitch-domain row (Pitch+ChordMask, 2026-07-12) — the one row whose index
// needs naming: it fronts two stack slots and is the page's landing row.
#define PROC_ROW_PTCH 0

// The active groove template's SELECTED paint lane (Dly/Len/Vel), as a read-only 16-cell
// array for the GP-row LED display — presets included (const). NULL when the style is
// off/out of range. num_steps tiling is ignored here: the paint surface addresses all 16.
static const s8 *SEQ_UI_PROC_GrooveLane(u8 track)
{
  u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
  if( !style || style >= (SEQ_GROOVE_NUM_PRESETS + SEQ_GROOVE_NUM_TEMPLATES) )
    return NULL;
  const seq_groove_entry_t *g = (style >= SEQ_GROOVE_NUM_PRESETS)
    ? &seq_groove_templates[style - SEQ_GROOVE_NUM_PRESETS]
    : &seq_groove_presets[style];
  switch( proc_groove_paint_lane ) {
  case 1:  return g->add_step_length;
  case 2:  return g->add_step_velocity;
  default: return g->add_step_delay;
  }
}

// WRITABLE variants — the selected CUSTOM template (NULL for off/presets): the paint
// toggle, the Val hold-step editor, Stps, and Clr all edit through these.
static seq_groove_entry_t *SEQ_UI_PROC_GrooveCustomEntry(u8 track)
{
  u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
  if( style < SEQ_GROOVE_NUM_PRESETS ||
      style >= (SEQ_GROOVE_NUM_PRESETS + SEQ_GROOVE_NUM_TEMPLATES) )
    return NULL;
  return &seq_groove_templates[style - SEQ_GROOVE_NUM_PRESETS];
}
static s8 *SEQ_UI_PROC_GrooveCustomLane(u8 track)
{
  seq_groove_entry_t *g = SEQ_UI_PROC_GrooveCustomEntry(track);
  if( !g )
    return NULL;
  switch( proc_groove_paint_lane ) {
  case 1:  return g->add_step_length;
  case 2:  return g->add_step_velocity;
  default: return g->add_step_delay;
  }
}

// Stock TRKGRV "global groove" semantics for the rack's dials (2026-07-13, the Glob
// cell): a Styl/Intn/Sync edit on a GLOBAL track (bit CLEAR in
// seq_groove_ui_local_selection) lands on EVERY global track — the whole kit grooves
// as one; a LOCAL track edits itself alone. Glob itself, Lane/Val/Stps (template
// state) and Clr stay track/template-scoped.
static u16 SEQ_UI_PROC_GrooveEditMask(u8 track)
{
  if( seq_groove_ui_local_selection & (1u << track) )
    return (u16)(1u << track);
  return (u16)(~seq_groove_ui_local_selection & 0xffff);
}

// Index of the anchor currently playing in Robotize's loop window (matches Reseed's head
// formula + the running phase), for the LOOP-face LED flash + readout.
static u8 SEQ_UI_PROC_RoboPlayingAnchor(u8 track)
{
  u8 pal = SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_PALETTE_LENGTH);
  if( pal == 0 || pal > 16 ) pal = 16;
  u8 head = (SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_LOOP_START)
             + SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_LOOP_ROTATE)) % pal;
  return (head + (u8)(seq_core_trk[track].robotize_loop_phase % pal)) % pal;
}

// PitchGen target resolution — duplicated verbatim from seq_ui_trkpitchgen.c's static
// gen_instr()/gen_par_layer() (small, no cross-file coupling; the PROC grammar module is
// deliberately self-contained). Drum track: instrument = the drum cursor, one line per
// drum. Normal track: instrument 0 (single melodic line); the gen targets the CURSOR's Note
// layer if it is one (deliberate placement wins), else the track's linked Note layer.
static u8 SEQ_UI_PROC_GenInstr(u8 track)
{
  return (seq_cc_trk[track].event_mode == SEQ_EVENT_MODE_Drum) ? ui_selected_instrument : 0;
}

static u8 SEQ_UI_PROC_GenParLayer(u8 track)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum &&
      SEQ_PAR_AssignmentGet(track, ui_selected_par_layer) == SEQ_PAR_Type_Note )
    return ui_selected_par_layer;
  return (u8)tcc->link_par_layer_note;
}

// The pool slot for the visible track's PitchGen target, or NULL if never engaged.
static seq_generator_t *SEQ_UI_PROC_GenGet(u8 track)
{
  return SEQ_GENERATOR_Get(track, SEQ_UI_PROC_GenInstr(track));
}

// Is rack row `row` the TrigGen row? (matches on its primary/OPERATE param list.)
static u8 SEQ_UI_PROC_IsTrigGen(u8 row)
{
  return row < PROC_NUM_ROWS && proc_rows[row].params == proc_params_triggen_op;
}

static u8 SEQ_UI_PROC_IsVoicing(u8 row)
{
  return row < PROC_NUM_ROWS && proc_rows[row].params == proc_params_voicing;
}

// TrigGen target trigger-layer resolution: the track's assigned GATE layer (0-based
// index), or 0xff if none assigned. Mirrors GenParLayer's Note-layer resolution, but for
// the trigger side — Gate is the semantically loaded layer (silences/sounds the step),
// matching PitchGen's choice to target the semantically loaded Note layer rather than an
// arbitrary par layer.
static u8 SEQ_UI_PROC_TrgLayer(u8 track)
{
  u8 a = seq_cc_trk[track].trg_assignments.gate;
  return a ? (u8)(a - 1) : 0xff;
}

// The pool slot for the visible track's TrigGen target (the SEPARATE trigger key-space —
// same instrument-resolution as PitchGen, independent occupancy), or NULL if never engaged.
static seq_generator_t *SEQ_UI_PROC_TGenGet(u8 track)
{
  return SEQ_GENERATOR_TrgGet(track, SEQ_UI_PROC_GenInstr(track));
}

// The LFO's GP-row CUSTOM surface: a palette of 16 waveforms (tap to pick). A curated
// spread — Off, the three ramps + their inverses, and a range of pulse widths — indexing
// the SEQ_LFO_WAVEFORM_* enum (Rec05=4 .. Rec95=22).
static const u8 lfo_wave_palette[16] = {
  SEQ_LFO_WAVEFORM_Off,      SEQ_LFO_WAVEFORM_Sine,   SEQ_LFO_WAVEFORM_Triangle, SEQ_LFO_WAVEFORM_Saw,
  SEQ_LFO_WAVEFORM_InvSaw,   SEQ_LFO_WAVEFORM_InvTriangle, SEQ_LFO_WAVEFORM_InvSine, SEQ_LFO_WAVEFORM_Rec05,
  SEQ_LFO_WAVEFORM_Rec15,    SEQ_LFO_WAVEFORM_Rec25,  SEQ_LFO_WAVEFORM_Rec40,    SEQ_LFO_WAVEFORM_Rec50,
  SEQ_LFO_WAVEFORM_Rec60,    SEQ_LFO_WAVEFORM_Rec75,  SEQ_LFO_WAVEFORM_Rec85,    SEQ_LFO_WAVEFORM_Rec95,
};

// Short (<=4 char) name of an LFO waveform for the encoder cell / palette readout. The
// bit-7 disable flag is masked off by the caller. Rec05..Rec95 read as pulse-width "P05".
// Returns a STATIC buffer for the Rec* names: UI-task-only, and at most ONE call per
// printf — a format printing two wave names would read the same clobbered buffer twice.
static const char *SEQ_UI_PROC_LfoWaveName(u8 wave)
{
  switch( wave ) {
  case SEQ_LFO_WAVEFORM_Off:         return "off";
  case SEQ_LFO_WAVEFORM_Sine:        return "Sine";
  case SEQ_LFO_WAVEFORM_Triangle:    return "Tri";
  case SEQ_LFO_WAVEFORM_Saw:         return "Saw";
  case SEQ_LFO_WAVEFORM_InvSine:     return "iSin";
  case SEQ_LFO_WAVEFORM_InvTriangle: return "iTri";
  case SEQ_LFO_WAVEFORM_InvSaw:      return "iSaw";
  default: { // Rec05..Rec95 -> pulse width %
    static char buf[5];
    sprintf(buf, "P%02d", (wave - SEQ_LFO_WAVEFORM_Rec05 + 1) * 5);
    return buf;
  }
  }
}

// Fixed human name of a rack row (valid even when the row is neutral/empty).
static const char *SEQ_UI_PROC_SlotName(u8 row)
{
  return (row < PROC_NUM_ROWS) ? proc_rows[row].name : "--";
}

// The param list for a rack row's CURRENT plane (ui_proc_plane). *n = 0 for none.
// Plane 1 exists only where the row declares params2; every other row ignores the plane.
static void SEQ_UI_PROC_SlotParams(u8 row, const proc_param_t **out, u8 *n)
{
  if( row < PROC_NUM_ROWS ) {
    const proc_row_t *r = &proc_rows[row];
    if( ui_proc_plane == 1 && r->params2 ) {
      *out = r->params2;
      *n   = r->n_params2;
    } else {
      *out = r->params;
      *n   = r->n_params;
    }
  } else {
    *out = NULL;
    *n   = 0;
  }
}

// Does row `row` have a 2nd plane (so ‹/› toggles it)?
static u8 SEQ_UI_PROC_HasPlane2(u8 row)
{
  return row < PROC_NUM_ROWS && proc_rows[row].params2 != NULL;
}

// The bespoke face of the row's CURRENT plane (PROC_FACE_NONE for a plain dial bank).
// Drives the custom GP-row / GP-button / right-screen branches by descriptor id, not a
// per-slot compare. Plane 0 (the only plane most rows have) can carry a face too
// (ChordMask/Groove/LFO's paint surfaces) — face2 is reached only via the ‹/› toggle.
static proc_face_t SEQ_UI_PROC_CurFace(u8 row)
{
  if( row >= PROC_NUM_ROWS )
    return PROC_FACE_NONE;
  return (ui_proc_plane == 1) ? proc_rows[row].face2 : proc_rows[row].face1;
}

// Execute a PROC_KIND_ACTION (fired by the encoder PUSH — the one non-snap push). The
// action id was stashed in the param's `cc` slot. Robotize's LOOP-plane verbs:
// Reseed = all-new palette; Freeze = capture the last Cyc bars (default 4) as the loop.
static void SEQ_UI_PROC_RunAction(u8 track, u8 action)
{
  switch( action ) {
  case PROC_ACT_RESEED:
    SEQ_ROBOTIZE_Reseed(track);
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Robotize", "  reseeded");
    break;
  case PROC_ACT_FREEZE: {
    u8 k = SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_LOOP_CYCLES);
    if( !k ) k = 4; // no window set yet -> capture a 4-bar loop (also sets Cyc)
    SEQ_ROBOTIZE_FreezeQuantized(track, k);
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Robotize", "   frozen");
    break;
  }
  // PitchGen LOOP-plane verbs (G2). Roll = one-shot reroll of unlocked steps; Anchor =
  // snapshot the current loop as identity; Snap = hard-restore it; Bounce = freeze the
  // loop into the source and free the pool slot (the generator's "harvest" verb).
  case PROC_ACT_GEN_ROLL: {
    u8 n = SEQ_GENERATOR_Roll(track);
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "PitchGen", n ? "   rolled" : "not engaged");
    break;
  }
  case PROC_ACT_GEN_ANCHOR: {
    s32 r = SEQ_GENERATOR_Anchor(track, SEQ_UI_PROC_GenInstr(track));
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "PitchGen", (r == 0) ? " anchored" : "not engaged");
    break;
  }
  case PROC_ACT_GEN_SNAP: {
    s32 r = SEQ_GENERATOR_Snap(track, SEQ_UI_PROC_GenInstr(track));
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "PitchGen",
               (r == 0) ? "  snapped" : (r == -2) ? "no anchor" : "not engaged");
    break;
  }
  case PROC_ACT_GEN_BOUNCE: {
    s32 r = SEQ_GENERATOR_Bounce(track, SEQ_UI_PROC_GenInstr(track));
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "PitchGen", (r == 0) ? "  bounced" : "not engaged");
    break;
  }
  // TrigGen LOOP-plane verbs (G3) — same shape as PitchGen's, trigger key-space.
  case PROC_ACT_TGEN_ROLL: {
    u8 n = SEQ_GENERATOR_TrgRoll(track);
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "TrigGen", n ? "   rolled" : "not engaged");
    break;
  }
  case PROC_ACT_TGEN_ANCHOR: {
    s32 r = SEQ_GENERATOR_TrgAnchor(track, SEQ_UI_PROC_GenInstr(track));
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "TrigGen", (r == 0) ? " anchored" : "not engaged");
    break;
  }
  case PROC_ACT_TGEN_SNAP: {
    s32 r = SEQ_GENERATOR_TrgSnap(track, SEQ_UI_PROC_GenInstr(track));
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "TrigGen",
               (r == 0) ? "  snapped" : (r == -2) ? "no anchor" : "not engaged");
    break;
  }
  case PROC_ACT_TGEN_BOUNCE: {
    s32 r = SEQ_GENERATOR_TrgBounce(track, SEQ_UI_PROC_GenInstr(track));
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "TrigGen", (r == 0) ? "  bounced" : "not engaged");
    break;
  }
  // Groove Clr (2026-07-13): whole-template reset via the stock verb — all 3 lanes
  // wiped back to the preset-0 copy, name "Custom #n", num_steps 2 (the next paint's
  // paint-the-bar expands to 16). Presets refuse.
  case PROC_ACT_GRV_CLEAR: {
    u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
    if( style >= SEQ_GROOVE_NUM_PRESETS && SEQ_GROOVE_Clear(style) == 0 ) {
      proc_groove_dirty = 1;
      // a cleared template must not resurrect old cells via the toggle shadow
      memset(proc_groove_step_shadow, 0, sizeof(proc_groove_step_shadow));
      proc_groove_shadow_style = 0xff;
      SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Groove", "  cleared");
    } else
      SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Groove", "preset (ro)");
    break;
  }
  }
}

// Live rack-row state for the readout (LED / LCD): occupied = doing anything at all;
// enabled = occupied AND not bypassed; strength = headline depth for the pass-through
// wink. A STACK row reads the render slot; an EMISSION row derives it from the
// effect's CCs — the one place a virtual (slot-less) row supplies its own occupancy.
typedef struct { u8 occupied; u8 enabled; u8 strength; } proc_rowstate_t;
static proc_rowstate_t SEQ_UI_PROC_RowState(u8 track, u8 row)
{
  proc_rowstate_t s = { 0, 0, 0 };
  if( row >= PROC_NUM_ROWS )
    return s;
  const proc_row_t *r = &proc_rows[row];
  if( r->rowkind == PROC_ROW_STACK ) {
    const seq_processor_slot_t *p = &seq_processor_stack[track][r->stack_slot];
    s.occupied = (p->id != SEQ_PROCESSOR_ID_NONE);
    s.enabled  = s.occupied && p->enabled;
    s.strength = p->strength;
    // The merged Ptch row fronts TWO stack slots — transpose/FTS (PITCH) plus the
    // chord filter absorbed from the dissolved ChordMask row. The row reads alive
    // when EITHER slot is; strength = the louder of the two (B-row LED cue).
    if( row == PROC_ROW_PTCH ) {
      const seq_processor_slot_t *cm = &seq_processor_stack[track][SEQ_CORE_CHORDMASK_SLOT];
      if( cm->id != SEQ_PROCESSOR_ID_NONE ) {
        s.occupied = 1;
        s.enabled  = s.enabled || cm->enabled;
        if( cm->strength > s.strength )
          s.strength = cm->strength;
      }
    }
  } else if( r->rowkind == PROC_ROW_GENERATOR ) {
    // Generator row: occupancy is a POOL-SLOT allocation, not a CC. occupied = a slot
    // exists (ENGAGEd at least once, config persists across DISENGAGE); enabled =
    // currently ENGAGEd (mutating). strength = mutation_rate — an imperfect proxy (rate=0
    // is a legitimate *engaged* frozen state, not silence) but the closest "how alive" cue.
    // PitchGen and TrigGen are BOTH this rowkind but live in separate pool key-spaces
    // (G3) — disambiguate by row identity, same as every other per-tenant branch here.
    u8 instr = SEQ_UI_PROC_GenInstr(track);
    if( SEQ_UI_PROC_IsTrigGen(row) ) {
      seq_generator_t *g = SEQ_GENERATOR_TrgGet(track, instr);
      s.occupied = (g != NULL);
      s.enabled  = SEQ_GENERATOR_TrgIsEngaged(track, instr);
      s.strength = g ? g->mutation_rate : 0;
      // The TGen row also fronts the ARP stack slot (absorbed 2026-07-12, the
      // Ptch/CHORDMASK pattern): alive when either tenant is. Double-tap stays the
      // generator's ENGAGE gesture — kill the arp by dialling its Mode to Off.
      {
        const seq_processor_slot_t *arp = &seq_processor_stack[track][SEQ_CORE_ARP_SLOT];
        if( arp->id != SEQ_PROCESSOR_ID_NONE ) {
          s.occupied = 1;
          s.enabled  = s.enabled || arp->enabled;
          if( arp->strength > s.strength )
            s.strength = arp->strength;
        }
      }
    } else {
      seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
      s.occupied = (g != NULL);
      s.enabled  = SEQ_GENERATOR_IsEngaged(track, instr);
      s.strength = g ? g->mutation_rate : 0;
    }
  } else if( SEQ_UI_PROC_IsVoicing(row) ) {
    // Voicing: no single headline CC can proxy occupancy — the row is live when ANY
    // dial sits off-neutral (spread>0, inv!=0, strum!=center detent). Bypass = SPREAD
    // bit 7 (the row's disable_mask), which gates the voicing dials in the DSP.
    u8 sprd = SEQ_CC_Get(track, SEQ_CC_VOICE_SPREAD);
    u8 inv  = SEQ_CC_Get(track, SEQ_CC_VOICE_INV) & 0x0f;
    u8 strm = SEQ_CC_Get(track, SEQ_CC_VOICE_STRUM);
    u8 drop = SEQ_CC_Get(track, SEQ_CC_VOICE_DROP);
    u8 tilt = SEQ_CC_Get(track, SEQ_CC_VOICE_TILT);
    s.occupied = ((sprd & 0x0f) != 0) || (inv != 0) || (strm != 64)
              || (drop != 0) || (tilt != 64);
    s.enabled  = s.occupied && !(sprd & 0x80);
    s.strength = sprd & 0x0f;
    // The merged Voic row also fronts the LIMIT stack slot (absorbed 2026-07-12, the
    // Ptch/CHORDMASK pattern): alive when either half is, strength = the louder.
    {
      const seq_processor_slot_t *lim = &seq_processor_stack[track][SEQ_CORE_LIMIT_SLOT];
      if( lim->id != SEQ_PROCESSOR_ID_NONE ) {
        s.occupied = 1;
        s.enabled  = s.enabled || lim->enabled;
        if( lim->strength > s.strength )
          s.strength = lim->strength;
      }
    }
  } else {
    // Emission row: occupancy count/index in bits 0..5 of occ_cc — UNLESS the row's
    // disable bit lives on a separate disable_cc (Humanize: Value uses its full 0..127
    // range, so there's no spare bit to pack into occ_cc itself; only mask to 6 bits
    // when the bit is actually sharing occ_cc's byte). The ENABLED bit lives either in
    // that same/disable byte (disable_mask — Echo 0x40 / Groove/LFO 0x80 / Humanize's
    // MODE 0x08, its field's only spare bit) or, when the effect splits occupancy from
    // enable entirely, in a
    // separate enable_cc (Robotize: occupancy = PROBABILITY>0, enable = ACTIVE).
    // disable_mask wins if both are set. strength = count.
    u8 raw = SEQ_CC_Get(track, r->occ_cc);
    u8 count = (r->disable_mask && !r->disable_cc) ? (raw & 0x3f) : raw;
    s.occupied = (count != 0);
    if( r->disable_mask ) {
      u8 draw = r->disable_cc ? SEQ_CC_Get(track, r->disable_cc) : raw;
      s.enabled = s.occupied && !(draw & r->disable_mask);
    } else if( r->enable_cc )
      s.enabled = s.occupied && (SEQ_CC_Get(track, r->enable_cc) != 0);
    else
      s.enabled = s.occupied;
    s.strength = count;
  }
  return s;
}

// Read a param's current LOGICAL value (signed where the kind is signed).
static s32 SEQ_UI_PROC_ParamRead(u8 track, const proc_param_t *p)
{
  switch( p->kind ) {
  case PROC_KIND_SNIBBLE: {
    u8 raw = SEQ_CC_Get(track, p->cc) & 0x0f;   // two's-complement 4-bit: 0..7=+, 8..15=-
    return (raw < 8) ? (s32)raw : (s32)raw - 16;
  }
  case PROC_KIND_FLAG: {
    seq_core_trkmode_flags_t f;
    f.ALL = SEQ_CC_Get(track, SEQ_CC_MODE_FLAGS);
    return f.FORCE_SCALE;
  }
  case PROC_KIND_GRAVITY:
    return seq_core_tension_gravity;
  case PROC_KIND_ECHO_REP:
    return SEQ_CC_Get(track, p->cc) & 0x3f; // count only; strip the 0x40 disable bit
  case PROC_KIND_ECHO_DLY:
    return SEQ_CORE_Echo_MapInternalToUser(SEQ_CC_Get(track, p->cc)); // musical order
  case PROC_KIND_SCALE:
    return seq_core_global_scale;
  case PROC_KIND_ROOT:
    return seq_core_global_scale_root_selection;
  case PROC_KIND_SDEG:
    return seq_core_global_scale_transpose;
  case PROC_KIND_GRV_STYLE:
    return SEQ_CC_Get(track, p->cc) & 0x3f; // style index only; strip sync/disable bits
  case PROC_KIND_GRV_SYNC:
    return (SEQ_CC_Get(track, p->cc) & 0x40) ? 1 : 0;
  case PROC_KIND_GRV_LANE:
    return proc_groove_paint_lane;
  case PROC_KIND_GRV_GLOBAL:
    return (seq_groove_ui_local_selection & (1u << track)) ? 0 : 1;
  case PROC_KIND_GRV_STEPS: {
    u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
    if( !style || style >= (SEQ_GROOVE_NUM_PRESETS + SEQ_GROOVE_NUM_TEMPLATES) )
      return 16; // off — nothing selected, show the full bar
    const seq_groove_entry_t *g = (style >= SEQ_GROOVE_NUM_PRESETS)
      ? &seq_groove_templates[style - SEQ_GROOVE_NUM_PRESETS]
      : &seq_groove_presets[style];
    return g->num_steps ? g->num_steps : 16;
  }
  case PROC_KIND_GRV_VAL:
    if( proc_groove_held_step < 16 ) {
      // hold-step: the HELD cell's value. Sentinels read as the 0 detent for the
      // Inc math (a turn replaces them with a literal); the CELL displays +Int/-Int.
      const s8 *lane = SEQ_UI_PROC_GrooveLane(track);
      if( lane ) {
        s8 c = lane[proc_groove_held_step];
        return (c == SEQ_GROOVE_VPOS || c == SEQ_GROOVE_VNEG) ? 0 : c;
      }
      return 0;
    }
    return proc_groove_paint_val;
  case PROC_KIND_LFO_WAVE:
    return SEQ_CC_Get(track, p->cc) & 0x3f; // waveform index; strip the 0x80 disable bit
  case PROC_KIND_LFO_AMP:
    return (s32)SEQ_CC_Get(track, p->cc) - 128; // bipolar: raw 0..255 -> logical -128..+127
  case PROC_KIND_LFO_FLAG:
    // p->cc is the BIT NUMBER in LFO_ENABLE_FLAGS, not a CC index
    return (SEQ_CC_Get(track, SEQ_CC_LFO_ENABLE_FLAGS) >> p->cc) & 1;
  case PROC_KIND_LFO_XCC_ON:
    return ((SEQ_CC_Get(track, SEQ_CC_LFO_ENABLE_FLAGS) >> p->cc) & 1) ? 0 : 1; // inverted
  case PROC_KIND_ROBO_PROB:
    return SEQ_CC_Get(track, p->cc); // overall probability 0..31
  case PROC_KIND_ACTION:
    return 0; // momentary — no value (push executes)
  case PROC_KIND_GEN_RANGE_LO: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    return g ? g->range_min : 0; // 0 pre-ENGAGE; ParamPrintValue shows dashes, not 0
  }
  case PROC_KIND_GEN_RANGE_HI: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    return g ? g->range_max : 0;
  }
  case PROC_KIND_GEN_RATE: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    return g ? g->mutation_rate : 0;
  }
  case PROC_KIND_GEN_DEPTH: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    return g ? g->mutation_depth : 0;
  }
  case PROC_KIND_GEN_CONTOUR: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    return g ? g->contour_shape : 0;
  }
  case PROC_KIND_GEN_WINDOW:
    return proc_gen_step_window;
  case PROC_KIND_TGEN_DENSITY: {
    seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
    return g ? g->range_min : 0; // density; 0 pre-ENGAGE, dashes not 0 on screen
  }
  case PROC_KIND_TGEN_RATE: {
    seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
    return g ? g->mutation_rate : 0;
  }
  case PROC_KIND_TGEN_DEPTH: {
    seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
    return g ? g->mutation_depth : 0;
  }
  case PROC_KIND_BUS: {
    u8 b = SEQ_CC_Get(track, p->cc); // bits 0..1 = bus A..D, bit 2 = Self
    return (b & 0x04) ? 4 : (b & 0x03);
  }
  case PROC_KIND_HUM_NOTE:
    return (SEQ_CC_Get(track, p->cc) & (1 << 0)) ? 1 : 0;
  case PROC_KIND_HUM_VEL:
    return (SEQ_CC_Get(track, p->cc) & (1 << 1)) ? 1 : 0;
  case PROC_KIND_HUM_LEN:
    return (SEQ_CC_Get(track, p->cc) & (1 << 2)) ? 1 : 0;
  case PROC_KIND_VOICE_SPREAD:
    return SEQ_CC_Get(track, p->cc) & 0x0f; // spread magnitude; strip the 0x80 bypass bit
  case PROC_KIND_VOICE_BIPOLAR:
    return (s32)SEQ_CC_Get(track, p->cc) - 64; // bipolar: raw 0..127 -> logical -64..+63
  case PROC_KIND_SPACER:
    return 0; // blank cell — no backing (cc slot is 0, don't let it hit SEQ_CC_Get)
  case PROC_KIND_LIMIT_HI: {
    u8 r = SEQ_CC_Get(track, p->cc);
    return r ? r : 127; // raw 0 = open top -> the dial reads 127 (the true ceiling)
  }
  case PROC_KIND_SHADE:
    return SEQ_UI_GRAVITY_ShadePosGet(); // ladder pos, -1 = off-ladder
  default: // CC, CM_STR, HUM_VALUE
    return SEQ_CC_Get(track, p->cc);
  }
}

// Forward decl: ParamWrite's headline cases (ECHO_REP/GRV_STYLE/LFO_WAVE/ROBO_PROB) call
// this on their own 0->on transition; its body (below ParamWrite, once ParamWrite exists
// to call) writes the row's OTHER dials.
static void SEQ_UI_PROC_SeedRowDefaults(u8 track, const proc_param_t *headline);

// Write a param's LOGICAL value (already range-clamped by the caller) to its
// backing — always via SEQ_CC_Set or the processor's own setter, never the slot.
static void SEQ_UI_PROC_ParamWrite(u8 track, const proc_param_t *p, s32 v)
{
  switch( p->kind ) {
  case PROC_KIND_SNIBBLE:
    SEQ_CC_Set(track, p->cc, (u8)(v & 0x0f)); // back to two's-complement nibble
    break;
  case PROC_KIND_FLAG: {
    seq_core_trkmode_flags_t f;
    f.ALL = SEQ_CC_Get(track, SEQ_CC_MODE_FLAGS);
    f.FORCE_SCALE = v ? 1 : 0;
    SEQ_CC_Set(track, SEQ_CC_MODE_FLAGS, f.ALL);
    break;
  }
  case PROC_KIND_GRAVITY:
    SEQ_CORE_TensionGravitySet((s8)v);
    break;
  case PROC_KIND_ECHO_REP: {
    u8 raw = SEQ_CC_Get(track, p->cc);
    u8 oldcount = raw & 0x3f;
    raw = (raw & ~0x3f) | ((u8)v & 0x3f); // preserve the 0x40 disable flag (and never set bit7 — #59)
    SEQ_CC_Set(track, p->cc, raw);
    // Engage-seed: a fresh track's echo feedback params are all 0, which makes the
    // echo train SILENT (velocity 0%). On the 0->on turn, seed the row's other dials
    // (each independently guarded on its own untouched state) so dialling Rpt up is
    // immediately audible. Fires once per field; never overrides a shaped value.
    if( oldcount == 0 && ((u8)v & 0x3f) )
      SEQ_UI_PROC_SeedRowDefaults(track, p);
    break;
  }
  case PROC_KIND_ECHO_DLY:
    SEQ_CC_Set(track, p->cc, SEQ_CORE_Echo_MapUserToInternal((u8)v));
    break;
  case PROC_KIND_CM_STR:
    SEQ_CC_Set(track, p->cc, (u8)v);
    // Engage ChordMask by turning Strength up — the same "dial it alive" move as the
    // other processors. ChordMask is gated on the track PLAYMODE (not just its params),
    // so entering the mode is what arms the slot (ChordMaskSlotSync fires on MODE).
    // Turning Str back to 0 leaves the mode engaged at pass-through; double-tap the row
    // to fully remove it (-> Normal). Strength was written first so the sync sees it.
    if( v > 0 && SEQ_CC_Get(track, SEQ_CC_MODE) != SEQ_CORE_TRKMODE_ChordMask )
      SEQ_CC_Set(track, SEQ_CC_MODE, SEQ_CORE_TRKMODE_ChordMask);
    break;
  case PROC_KIND_SCALE:
  case PROC_KIND_ROOT:
    // GLOBAL scale/root (shared with the Scale page). Re-render every force-scale
    // track so the change is heard, and flag the config for save — exactly as the
    // Scale page does. Caller already clamped to the valid range.
    if( p->kind == PROC_KIND_SCALE )
      seq_core_global_scale = (u8)v;
    else
      seq_core_global_scale_root_selection = (u8)v;
    SEQ_CORE_RenderDirtySetAll();
    ui_store_file_required = 1;
    break;
  case PROC_KIND_SDEG:
    // GLOBAL diatonic transpose — the FTS render walks notes ±v scale degrees.
    seq_core_global_scale_transpose = (s8)v;
    SEQ_CORE_RenderDirtySetAll();
    ui_store_file_required = 1;
    break;
  case PROC_KIND_GRV_STYLE: {
    // BROADCAST to the global set (GrooveEditMask — the Glob cell's whole meaning).
    u16 m = SEQ_UI_PROC_GrooveEditMask(track);
    u8 t;
    for(t=0; t<16; ++t) {
      if( !(m & (1u << t)) )
        continue;
      u8 raw = SEQ_CC_Get(t, p->cc);
      u8 oldstyle = raw & 0x3f;
      raw = (raw & ~0x3f) | ((u8)v & 0x3f); // preserve sync (0x40) + disable (0x80)
      SEQ_CC_Set(t, p->cc, raw);
      // Engage-seed (per receiving track): turning groove on from off with intensity 0
      // is SILENT (the VPOS/VNEG cells resolve to 0). On the 0->on turn, seed a musical
      // depth (Intn's `eng`, guarded on its own untouched state) so it is audible.
      if( oldstyle == 0 && ((u8)v & 0x3f) )
        SEQ_UI_PROC_SeedRowDefaults(t, p);
    }
    break;
  }
  case PROC_KIND_GRV_INTN: {
    if( proc_in_seed ) {
      SEQ_CC_Set(track, p->cc, (u8)v); // a seed is single-track (see proc_in_seed)
      break;
    }
    // Same broadcast as Styl — a plain CC otherwise.
    u16 m = SEQ_UI_PROC_GrooveEditMask(track);
    u8 t;
    for(t=0; t<16; ++t)
      if( m & (1u << t) )
        SEQ_CC_Set(t, p->cc, (u8)v);
    break;
  }
  case PROC_KIND_GRV_SYNC: {
    // Same broadcast as Styl.
    u16 m = SEQ_UI_PROC_GrooveEditMask(track);
    u8 t;
    for(t=0; t<16; ++t) {
      if( !(m & (1u << t)) )
        continue;
      u8 raw = SEQ_CC_Get(t, p->cc);
      raw = (raw & ~0x40) | (v ? 0x40 : 0x00); // toggle bit 6, keep style + disable
      SEQ_CC_Set(t, p->cc, raw);
    }
    break;
  }
  case PROC_KIND_GRV_GLOBAL:
    if( v )
      seq_groove_ui_local_selection &= (u16)~(1u << track);
    else
      seq_groove_ui_local_selection |= (u16)(1u << track);
    ui_store_file_required = 1; // LocalGrooveSelection lives in the config file
    break;
  case PROC_KIND_GRV_STEPS: {
    seq_groove_entry_t *g = SEQ_UI_PROC_GrooveCustomEntry(track);
    if( g ) { // presets/off are read-only, like the paint surface
      g->num_steps = (u8)v;
      proc_groove_dirty = 1;
    }
    break;
  }
  case PROC_KIND_GRV_VAL:
    if( proc_groove_held_step < 16 ) {
      // hold-step: dial the HELD step's exact cell; the release then skips its
      // toggle (the hold was a value edit, not a tap). Custom templates only —
      // the same gate as painting. Encoder-push while holding writes the 0
      // detent = erase the step.
      seq_groove_entry_t *g = SEQ_UI_PROC_GrooveCustomEntry(track);
      s8 *lane = SEQ_UI_PROC_GrooveCustomLane(track);
      if( lane ) {
        if( g->num_steps < 16 )
          g->num_steps = 16; // paint-the-bar
        // hold+push ERASE (v hits the 0 detent): shadow the old value first, so a
        // re-tap restores it — same persistence as the toggle-off path.
        if( v == 0 && lane[proc_groove_held_step] ) {
          u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
          if( proc_groove_shadow_style != style ) {
            memset(proc_groove_step_shadow, 0, sizeof(proc_groove_step_shadow));
            proc_groove_shadow_style = style;
          }
          proc_groove_step_shadow[proc_groove_paint_lane][proc_groove_held_step] =
            lane[proc_groove_held_step];
        }
        lane[proc_groove_held_step] = (s8)v;
        proc_groove_held_turned = 1;
        proc_groove_dirty = 1;
      }
    } else
      proc_groove_paint_val = (s8)v; // the brush (0 = intensity-follow)
    break;
  case PROC_KIND_GRV_LANE:
    proc_groove_paint_lane = (u8)v; // UI-only: which template lane the GP row paints
    break;
  case PROC_KIND_LFO_WAVE: {
    u8 raw = SEQ_CC_Get(track, p->cc);
    u8 oldwave = raw & 0x3f;
    raw = (raw & 0x80) | ((u8)v & 0x3f); // set waveform, preserve the 0x80 disable bit
    SEQ_CC_Set(track, p->cc, raw);
    // Engage-seed: on the 0->on turn, give a fresh/neutral LFO a musical baseline (each
    // dial independently guarded on its own untouched state — see SeedRowDefaults) so
    // dialling Wave up is immediately audible.
    if( oldwave == 0 && ((u8)v & 0x3f) )
      SEQ_UI_PROC_SeedRowDefaults(track, p);
    break;
  }
  case PROC_KIND_LFO_AMP:
    SEQ_CC_Set(track, p->cc, (u8)(v + 128)); // bipolar: logical -128..+127 -> raw 0..255
    break;
  case PROC_KIND_LFO_FLAG: {
    // p->cc is the BIT NUMBER in LFO_ENABLE_FLAGS, not a CC index
    u8 f = SEQ_CC_Get(track, SEQ_CC_LFO_ENABLE_FLAGS);
    f = v ? (f | (1 << p->cc)) : (f & ~(1 << p->cc));
    SEQ_CC_Set(track, SEQ_CC_LFO_ENABLE_FLAGS, f);
    break;
  }
  case PROC_KIND_LFO_XCC_ON: {
    u8 f = SEQ_CC_Get(track, SEQ_CC_LFO_ENABLE_FLAGS);
    f = v ? (f & ~(1 << p->cc)) : (f | (1 << p->cc)); // inverted: on = bit CLEAR
    SEQ_CC_Set(track, SEQ_CC_LFO_ENABLE_FLAGS, f);
    break;
  }
  case PROC_KIND_ROBO_PROB: {
    SEQ_CC_Set(track, p->cc, (u8)v); // overall probability 0..31
    // Engage: robotize is gated on `active AND probability>0`. Turning Prob up arms
    // active, and — since the per-dimension probabilities have nothing to move unless the
    // matching RANGE is non-zero — seeds musical default ranges (Note/Vel/Len/Oct, each
    // independently guarded on its own untouched state) if still 0. Skip is deliberately
    // left unseeded (its `eng` is unset). The ranges also live on the stock FX-Robotize
    // page for finer control.
    if( v > 0 ) {
      SEQ_CC_Set(track, SEQ_CC_ROBOTIZE_ACTIVE, 1);
      SEQ_UI_PROC_SeedRowDefaults(track, p);
    }
    break;
  }
  case PROC_KIND_ACTION:
    break; // momentary — turning does nothing; the encoder PUSH executes (see below)
  case PROC_KIND_GEN_RANGE_LO: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    if( g ) g->range_min = (u8)((v > g->range_max) ? g->range_max : v); // clamp <= range_max
    break; // no-op pre-ENGAGE — mirrors the stock page's "nothing to tune yet" contract
  }
  case PROC_KIND_GEN_RANGE_HI: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    if( g ) g->range_max = (u8)((v < g->range_min) ? g->range_min : v); // clamp >= range_min
    break;
  }
  case PROC_KIND_GEN_RATE: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    if( g ) g->mutation_rate = (u8)v;
    break;
  }
  case PROC_KIND_GEN_DEPTH: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    if( g ) g->mutation_depth = (u8)v;
    break;
  }
  case PROC_KIND_GEN_CONTOUR: {
    seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
    if( g ) g->contour_shape = (u8)v;
    break;
  }
  case PROC_KIND_GEN_WINDOW:
    proc_gen_step_window = (u8)v; // UI-only: which 16-step quarter the GP row shows/edits
    break;
  case PROC_KIND_TGEN_DENSITY: {
    seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
    if( g ) g->range_min = (u8)v; // density; no-op pre-ENGAGE
    break;
  }
  case PROC_KIND_TGEN_RATE: {
    seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
    if( g ) g->mutation_rate = (u8)v;
    break;
  }
  case PROC_KIND_TGEN_DEPTH: {
    seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
    if( g ) g->mutation_depth = (u8)v;
    break;
  }
  case PROC_KIND_BUS: {
    // ChordMask mask source. 0..3 = bus A..D (clear the Self bit); 4 = Self (static
    // mask — set bit 2, keep the underlying bus for Tension). Switching to Self while
    // the static mask is empty SEEDS it from the current live bus chord (the "grab a
    // bus chord as your static mask" bridge).
    u8 cur = SEQ_CC_Get(track, p->cc);
    if( v >= 4 ) {
      u16 stat = (((u16)SEQ_CC_Get(track, SEQ_CC_CHORDMASK_MASK_H) << 8)
                  | SEQ_CC_Get(track, SEQ_CC_CHORDMASK_MASK_L)) & 0x0fff;
      if( !stat ) {
        u16 chord = SEQ_MIDI_IN_BusPCSetGet(cur & 0x03) & 0x0fff;
        SEQ_CC_Set(track, SEQ_CC_CHORDMASK_MASK_L, chord & 0xff);
        SEQ_CC_Set(track, SEQ_CC_CHORDMASK_MASK_H, (chord >> 8) & 0x0f);
      }
      SEQ_CC_Set(track, p->cc, 0x04 | (cur & 0x03));
    } else {
      SEQ_CC_Set(track, p->cc, (u8)v & 0x03);
    }
    break;
  }
  case PROC_KIND_HUM_VALUE: {
    u8 old = SEQ_CC_Get(track, p->cc);
    SEQ_CC_Set(track, p->cc, (u8)v);
    // Engage-seed: intensity alone is silent until at least one Note/Vel/Len bit is on
    // (SEQ_HUMANIZE_Event's own `if(!mode) return 0` gate). On the 0->on turn, if no
    // mode bit is chosen yet, turn all three on so dialling Int up is audible at once —
    // one direct write, not via SeedRowDefaults: Note/Vel/Len share a single backing
    // byte, and that registry's per-field "untouched" test re-reads the byte fresh each
    // call, so seeding them one at a time would go stale after the first write.
    if( old == 0 && v > 0 ) {
      u8 f = SEQ_CC_Get(track, SEQ_CC_HUMANIZE_MODE);
      if( !(f & 0x07) )
        SEQ_CC_Set(track, SEQ_CC_HUMANIZE_MODE, f | 0x07);
    }
    break;
  }
  case PROC_KIND_HUM_NOTE:
  case PROC_KIND_HUM_VEL:
  case PROC_KIND_HUM_LEN: {
    u8 bit = (p->kind == PROC_KIND_HUM_NOTE) ? 0 : (p->kind == PROC_KIND_HUM_VEL) ? 1 : 2;
    u8 f = SEQ_CC_Get(track, p->cc);
    f = v ? (f | (1 << bit)) : (f & ~(1 << bit));
    SEQ_CC_Set(track, p->cc, f);
    break;
  }
  case PROC_KIND_VOICE_SPREAD: {
    u8 raw = SEQ_CC_Get(track, p->cc);
    SEQ_CC_Set(track, p->cc, (raw & 0x80) | ((u8)v & 0x0f)); // preserve the bypass bit
    break;
  }
  case PROC_KIND_VOICE_BIPOLAR:
    SEQ_CC_Set(track, p->cc, (u8)(v + 64)); // bipolar: logical -63..+63 -> raw 1..127, 64 = detent
    break;
  case PROC_KIND_SPACER:
    break; // blank cell — nothing to write (encoder turn/push land here harmlessly)
  case PROC_KIND_LIMIT_HI:
    // 127 stores as the stock "open top" 0, so the resting detent keeps the LIMIT
    // slot dark (the DSP treats a stored 127 identically anyway — clamping at the
    // ceiling clamps nothing — but 0 is the encoding the slot-sync reads as off).
    SEQ_CC_Set(track, p->cc, (v >= 127) ? 0 : (u8)v);
    break;
  case PROC_KIND_SHADE:
    SEQ_UI_GRAVITY_ShadeSet(v); // GLOBAL scale via the ladder; v<0 = no-op (see the kind)
    break;
  default: // CC, CM_STR
    SEQ_CC_Set(track, p->cc, (u8)v);
    break;
  }
}

// G2 defaults registry: seed a freshly-engaged row's OTHER dials (params[1..n-1] — index 0
// is the headline the caller just wrote, e.g. Rpt/Styl/Wave/Prob, already correct) from the
// table instead of a hand-written literal block per tenant. `eng` (falling back to `deflt`
// when 0) is the seed value; a seed that resolves to 0 is a true pass-through with nothing
// to make audible, so it's skipped. Each field is independently guarded on its own backing
// still reading "untouched" — this is a genuine tightening, not just a refactor: Echo's old
// gate checked ONLY Velocity before blindly overwriting all six dials, and LFO's Target had
// no guard at all (always reset to Vel on every re-engage); both now respect a field the
// user already shaped, the same way Robotize's per-field checks always did.
static void SEQ_UI_PROC_SeedRowDefaults(u8 track, const proc_param_t *headline)
{
  u8 row;
  for(row = 0; row < PROC_NUM_ROWS; ++row)
    if( proc_rows[row].params == headline )
      break;
  if( row >= PROC_NUM_ROWS )
    return;

  const proc_param_t *params = proc_rows[row].params;
  u8 n = proc_rows[row].n_params;
  u8 i;
  proc_in_seed = 1; // seeds are single-track (suppress the groove Glob broadcast)
  for(i = 1; i < n; ++i) {
    const proc_param_t *p = &params[i];
    // Momentary + UI-state dials (paint lane/brush, template length, the global
    // broadcast flag) are not "make the row audible" seeds — and several have no CC
    // backing (cc slot 0), so the untouched-guard below would misread CC 0.
    switch( p->kind ) {
    case PROC_KIND_ACTION:
    case PROC_KIND_SPACER:
    case PROC_KIND_GRV_LANE:
    case PROC_KIND_GRV_GLOBAL:
    case PROC_KIND_GRV_STEPS:
    case PROC_KIND_GRV_VAL:
    case PROC_KIND_LFO_FLAG:    // cc slot = a BIT NUMBER, not a CC index
    case PROC_KIND_LFO_XCC_ON:  // (same)
      continue;
    default:
      break;
    }

    s32 seed = p->eng ? p->eng : p->deflt;
    if( !seed )
      continue; // true pass-through already — nothing to make audible

    // "Still untouched" reads as raw CC==0 for every kind here except LFO_AMP: its raw-0
    // boot state decodes to logical -128 (not 0), and ANY non-positive depth — not just a
    // literal 0 — still wants the seed (matches the amplitude dial's own original guard).
    u8 untouched = (p->kind == PROC_KIND_LFO_AMP)
      ? (SEQ_CC_Get(track, p->cc) <= 128)
      : (SEQ_CC_Get(track, p->cc) == 0);
    if( untouched )
      SEQ_UI_PROC_ParamWrite(track, p, seed);
  }
  proc_in_seed = 0;
}

// Nudge a param by `incr` detents, clamped to its logical range. Scale's ceiling is
// the runtime scale count (the table's hi is just a placeholder), so it stops at the
// last real scale rather than a fixed guess.
static void SEQ_UI_PROC_ParamInc(u8 track, const proc_param_t *p, s32 incr)
{
  s32 v = SEQ_UI_PROC_ParamRead(track, p) + incr;
  s32 hi = (p->kind == PROC_KIND_SCALE) ? (SEQ_SCALE_NumGet() - 1) : p->hi;
  if( v < p->lo ) v = p->lo;
  if( v > hi ) v = hi;
  SEQ_UI_PROC_ParamWrite(track, p, v);
}

// Print a signed value into a 4-char cell as "<sign><mag>". NOTE: SEQ_LCD's vsprintf
// has NO '+' flag (MBSEQV4_REFERENCE.md) — "%+3d" renders the literal "3d" and never
// consumes the argument, so the sign must be emitted by hand (the stock "+%d"/"-%d"
// idiom). This was the "Semi/Oct show 3d and don't change" bug.
static void SEQ_UI_PROC_PrintSigned(s32 val)
{
  // Plain %c + %d only (no width/flags) — the value is short (|v| <= 64) and the cell
  // was blanked before drawing, so the trailing gap to the next cell is preserved.
  SEQ_LCD_PrintFormattedString("%c%d", (val < 0) ? '-' : '+',
                               (int)((val < 0) ? -val : val));
}

// Print one param's VALUE in a 4-char field (the label is drawn separately, above it,
// in the encoder grid). Formatted by kind, then by the fmt display map for CC dials.
static void SEQ_UI_PROC_ParamPrintValue(u8 track, const proc_param_t *p)
{
  s32 v = SEQ_UI_PROC_ParamRead(track, p);
  switch( p->kind ) {
  case PROC_KIND_BUS:
    if( v >= 4 ) SEQ_LCD_PrintString("Sf  ");            // Self = static hand-set mask
    else         SEQ_LCD_PrintFormattedString("%c   ", 'A' + (v & 0x03));
    return;
  case PROC_KIND_ARP_MODE: {
    static const char *const an[5] = { "Off ", "Up  ", "Dn  ", "U-D ", "Rnd " };
    u8 m = (u8)v;
    SEQ_LCD_PrintString(an[(m < 5) ? m : 0]);
    return;
  }
  case PROC_KIND_ARP_BUS:
    if( v == 0 ) SEQ_LCD_PrintString("Self");            // 0 = shared static mask
    else         SEQ_LCD_PrintFormattedString("%c   ", 'A' + ((v - 1) & 0x03)); // 1..4 = bus A..D
    return;
  case PROC_KIND_FLAG:
  case PROC_KIND_HUM_NOTE:
  case PROC_KIND_HUM_VEL:
  case PROC_KIND_HUM_LEN:
  case PROC_KIND_GRV_GLOBAL:
  case PROC_KIND_LFO_FLAG:
  case PROC_KIND_LFO_XCC_ON:
    SEQ_LCD_PrintFormattedString("%-4s", v ? "on" : "off");
    return;
  case PROC_KIND_LFO_XCC:
    if( v ) SEQ_LCD_PrintFormattedString("%03d ", (int)v); // the stock ExtraCC# idiom
    else    SEQ_LCD_PrintString("--- ");
    return;
  case PROC_KIND_LFO_PPQN:
    // 0..8 -> 1/3/6/12/24/48/96/192/384 (the stock decode)
    SEQ_LCD_PrintFormattedString("%3d ", v ? (3 << ((int)v - 1)) : 1);
    return;
  case PROC_KIND_GRV_VAL:
    if( proc_groove_held_step < 16 ) {
      // holding a step: sentinel cells read out as their MEANING, not the raw s8
      const s8 *lane = SEQ_UI_PROC_GrooveLane(track);
      s8 c = lane ? lane[proc_groove_held_step] : 0;
      if( c == SEQ_GROOVE_VPOS ) { SEQ_LCD_PrintString("+Int"); return; }
      if( c == SEQ_GROOVE_VNEG ) { SEQ_LCD_PrintString("-Int"); return; }
    }
    SEQ_UI_PROC_PrintSigned(v);
    return;
  case PROC_KIND_SNIBBLE:
  case PROC_KIND_GRAVITY:
  case PROC_KIND_SDEG:
  case PROC_KIND_VOICE_BIPOLAR:
    SEQ_UI_PROC_PrintSigned(v);
    return;
  case PROC_KIND_VOICE_DROP: {
    static const char *const dn[4] = { "off ", "Dp2 ", "Dp3 ", "D2+4" };
    SEQ_LCD_PrintString(dn[(v >= 0 && v < 4) ? v : 0]);
    return;
  }
  case PROC_KIND_ECHO_DLY:
    SEQ_LCD_PrintFormattedString("%-4s",
                                 SEQ_CORE_Echo_GetDelayModeName(SEQ_CC_Get(track, p->cc)));
    return;
  case PROC_KIND_ROOT:
    if( !v ) SEQ_LCD_PrintString("Keyb");   // 0 = follow the played key
    else     SEQ_LCD_PrintRootValue((u8)v); // 1..12 -> " C  ".." B  " (4 chars)
    return;
  case PROC_KIND_GRV_STYLE:
    if( !v ) SEQ_LCD_PrintString("off ");           // 0 = no groove (dark row)
    else     SEQ_LCD_PrintFormattedString("%3d ", (int)v); // index; name on right screen
    return;
  case PROC_KIND_GRV_SYNC:
    SEQ_LCD_PrintString(v ? "RefS" : "Trk ");
    return;
  case PROC_KIND_GRV_LANE:
    SEQ_LCD_PrintString((v == 0) ? "Dly " : (v == 1) ? "Len " : "Vel ");
    return;
  case PROC_KIND_LFO_WAVE:
    SEQ_LCD_PrintFormattedString("%-4s", SEQ_UI_PROC_LfoWaveName((u8)v)); // name; full on right
    return;
  case PROC_KIND_LFO_AMP:
    SEQ_UI_PROC_PrintSigned(v); // bipolar depth (-128..+127); centre 0 = pass-through
    return;
  case PROC_KIND_ACTION:
    SEQ_LCD_PrintString("push");  // momentary — press the encoder to fire
    return;
  case PROC_KIND_SPACER:
    SEQ_LCD_PrintString("    ");  // blank cell (group separator)
    return;
  case PROC_KIND_SHADE:
    SEQ_LCD_PrintFormattedString("%-4s", SEQ_UI_GRAVITY_ShadeName(v));
    return;
  case PROC_KIND_GEN_RANGE_LO:
  case PROC_KIND_GEN_RANGE_HI:
  case PROC_KIND_GEN_RATE:
  case PROC_KIND_GEN_DEPTH:
    // Dashes pre-ENGAGE (mirrors the stock PITCHGEN page's "Lo:--  Hi:--  R:---" idiom) —
    // else fall through to the generic %3d map (PROC_FMT_DEFAULT) below.
    if( !SEQ_UI_PROC_GenGet(track) ) { SEQ_LCD_PrintString("--- "); return; }
    break;
  case PROC_KIND_GEN_CONTOUR: {
    static const char *const cn[4] = { "Uni", "Lo ", "Hi ", "Tri" };
    if( !SEQ_UI_PROC_GenGet(track) ) { SEQ_LCD_PrintString("--- "); return; }
    SEQ_LCD_PrintFormattedString("%-4s", cn[v & 0x03]);
    return;
  }
  case PROC_KIND_GEN_WINDOW:
    SEQ_LCD_PrintFormattedString("Q%d/4", (int)v + 1); // which quarter of the 64-step loop
    return;
  case PROC_KIND_TGEN_DENSITY:
  case PROC_KIND_TGEN_RATE:
  case PROC_KIND_TGEN_DEPTH:
    // Dashes pre-ENGAGE, same idiom as PitchGen's OPERATE dials (separate check: TrigGen
    // resolves through the trigger key-space, not SEQ_UI_PROC_GenGet).
    if( !SEQ_UI_PROC_TGenGet(track) ) { SEQ_LCD_PrintString("--- "); return; }
    break;
  default: break; // CC / ECHO_REP / CM_STR / SCALE -> fmt map below (SCALE = numeric index)
  }
  switch( p->fmt ) {
  case PROC_FMT_PCT5:   SEQ_LCD_PrintFormattedString("%3d%%", (int)v * 5); break;
  case PROC_FMT_SEMI24: SEQ_UI_PROC_PrintSigned((int)v - 24);             break;
  case PROC_FMT_PLUS1:  SEQ_LCD_PrintFormattedString("%3d ", (int)v + 1); break;
  case PROC_FMT_PCT:    SEQ_LCD_PrintFormattedString("%3d%%", (int)v);    break;
  case PROC_FMT_PCT127: SEQ_LCD_PrintFormattedString("%3d%%", (int)v * 100 / 127); break;
  default:              SEQ_LCD_PrintFormattedString("%3d ", (int)v);     break;
  }
}

// Reset a whole processor to pass-through (all params -> default). The bypass
// gesture for the slots whose presence is param-driven (Ptch/Tension).
static void SEQ_UI_PROC_SlotReset(u8 track, u8 slot)
{
  const proc_param_t *params; u8 n, i;
  SEQ_UI_PROC_SlotParams(slot, &params, &n);
  for(i=0; i<n; ++i)
    SEQ_UI_PROC_ParamWrite(track, &params[i], params[i].deflt);
}


/////////////////////////////////////////////////////////////////////////////
// The PROC page — the processor rack's readout home.
//
// PROC is a real page (like FX / GRAVITY), NOT an LCD-takeover overlay: it owns
// the LCD the normal way and behaves like every other page. It is *page-scoped* —
// seq_ui_sel_view = PROC is set on entry (Init) and cleared on leave (the exit
// callback), so the B-row rack / GP-encoder operate / GP-row mask (all keyed on
// sel_view == PROC in the global handlers) are live exactly while you are on the
// page, and revert the moment you leave — matching how the other modes work.
/////////////////////////////////////////////////////////////////////////////

static seq_ui_page_t proc_prev_page = SEQ_UI_PAGE_EDIT; // where LIVE/EXIT returns to

// Page LCD — the encoder-aligned OPERATE grid (invariants 3 + 4). LEFT screen
// (cols 0..39) is the dial bank: param i sits in a 5-char cell at column i*5, so its
// label (line 0) and value (line 1) fall directly under GP encoder (i+1) — turn the
// knob, watch the number under it. RIGHT screen (cols 40..79) is identity + the
// processor's custom readout (Ptch's live 12-PC mask keyboard). Read-only mirror; edits
// flow via the global sel_view==PROC encoder intercept -> SEQ_CC_Set / the setter.
static s32 SEQ_UI_PROC_page_LCD(u8 high_prio)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  u8 slot = ui_focused_proc_slot;
  const proc_param_t *params; u8 nparams;
  SEQ_UI_PROC_SlotParams(slot, &params, &nparams);
  proc_rowstate_t rs = SEQ_UI_PROC_RowState(track, slot);

  SEQ_LCD_CursorSet(0, 0); SEQ_LCD_PrintSpaces(80);
  SEQ_LCD_CursorSet(0, 1); SEQ_LCD_PrintSpaces(80);

  // Left screen: the dial grid (label over value, one cell per encoder). Up to 8
  // params fit cols 0..39; the merged Ptch row fills all 8. >8 would page here.
  int i;
  for(i=0; i<nparams && i<8; ++i) {
    SEQ_LCD_CursorSet(i*5, 0);
    SEQ_LCD_PrintFormattedString("%-4s", params[i].label);
    SEQ_LCD_CursorSet(i*5, 1);
    SEQ_UI_PROC_ParamPrintValue(track, &params[i]);
  }

  // Right screen line 0 — the BASE LAYOUT header (2026-07-12): identity compressed and
  // right-aligned to cols 65..79 ("Ptch  1/12 G1T1" — row abbr + rack position + the
  // stock GxTy idiom), so cols 40..54 belong to the row's own row-0 readout (Ptch:
  // scale name; Tension: zone name, right-justified to 64). Cols 55..57 = the steady
  // BYP cue (a set-but-switched-off row — matches the winking-green LED); 60..63 =
  // the plane cue.
  SEQ_LCD_CursorSet(65, 0);
  SEQ_LCD_PrintFormattedString("%-4s %2d/%d ",
                               proc_rows[slot].abbr, slot + 1, PROC_NUM_ROWS);
  SEQ_LCD_PrintGxTy(ui_selected_group, ui_selected_tracks);
  if( rs.occupied && !rs.enabled ) {
    SEQ_LCD_CursorSet(55, 0);
    SEQ_LCD_PrintString("BYP");
  }
  // Plane cue (G2): when the focused row has a 2nd plane, name the current one WITH
  // its position — "OPER 1/2" / "STEP 2/2" / "LOOP 2/2" — ‹/› (or Up/Dn) flips it.
  // Lives at col 41 since the PGen mock (2026-07-12): the cue IS these rows' row-0
  // readout, so it sits where Ptch/Voic/Tens put theirs (none of the plane rows has
  // a competing row-0 readout).
  if( SEQ_UI_PROC_HasPlane2(slot) ) {
    SEQ_LCD_CursorSet(41, 0);
    const char *plane1_name = proc_rows[slot].p1name ? proc_rows[slot].p1name : "OPER";
    const char *plane2_name = proc_rows[slot].p2name ? proc_rows[slot].p2name
      : (proc_rows[slot].face2 == PROC_FACE_ROBOLOOP) ? "LOOP"
      : (proc_rows[slot].face2 == PROC_FACE_PITCHGEN_STEPS) ? "STEP"
      : (proc_rows[slot].face2 == PROC_FACE_TRIGGEN_STEPS)  ? "STEP" : "CFG";
    SEQ_LCD_PrintFormattedString("%-4s %d/2",
      (ui_proc_plane == 0) ? plane1_name : plane2_name, ui_proc_plane + 1);
  }

  // Right screen line 1: per-row CUSTOM readout (style/waveform names, engaged state,
  // loop status...) — dispatched through the row's own .status hook instead of a
  // 7-tenant if-chain (G2 defaults registry). Tension/Limit have none (NULL).
  if( proc_rows[slot].status )
    proc_rows[slot].status(track, slot);

  return 0; // no error
}

// Ptch's .status (the merged pitch+chord cockpit, 2026-07-12):
// Row 0: the global scale name (the "Scle" cell only shows the index; the name
// doesn't fit a cell), plus — when the diatonic transpose is off-zero — the note the
// current scale degree lands on (">E": the tonic walked Deg degrees via the same
// WalkScale the transpose uses; its old home at col 30 row 1 now holds Root's value).
// Row 1: the mask keyboard — a FIXED 12-slot chromatic strip (3 cols per PC, the
// same stride the old active-only readout used), active PCs as note names, inactive
// as dots. "M*:" = the editable Self mask, "M: " = the live chord on the source bus.
// Always drawn, engaged or not — visible == paintable (GP buttons 1-12, Self mode).
static void SEQ_UI_PROC_Status_Pitch(u8 track, u8 slot)
{
  static const char *const pc_name[12] = {
    "C ","C#","D ","D#","E ","F ","F#","G ","G#","A ","A#","B " };

  // TRIMMED scale name (the table pads to 20 chars; printing the padding would blank
  // the BYP/plane cues page_LCD already drew at 55/60 — the line was space-cleared
  // at the top of the redraw anyway, so nothing stale needs overwriting). A long name
  // may still RUN INTO the cue zone (to col 60) — deliberate: scale names carry
  // musical information, Ptch has no plane cue, and its BYP state is practically
  // unreachable (the pitch slot has no disable path; ChordMask enable = the playmode).
  const char *sn = SEQ_SCALE_NameGet(seq_core_global_scale);
  int len = (int)strlen(sn);
  while( len && sn[len-1] == ' ' )
    --len;
  if( len > 20 )
    len = 20;
  {
    char buf[21];
    memcpy(buf, sn, (size_t)len);
    buf[len] = 0;
    SEQ_LCD_CursorSet(41, 0);
    SEQ_LCD_PrintString(buf);
  }
  if( seq_core_global_scale_transpose && (41 + len + 4) <= 65 ) {
    // only when it fits left of the identity block
    u8 rsel = seq_core_global_scale_root_selection;
    u8 root_pc = (rsel == 0) ? seq_core_keyb_scale_root : (u8)(rsel - 1);
    s32 dn = SEQ_SCALE_WalkScale((u8)(60 + (root_pc % 12)), seq_core_global_scale,
                                 root_pc, seq_core_global_scale_transpose);
    SEQ_LCD_CursorSet((u16)(41 + len + 1), 0);
    SEQ_LCD_PrintFormattedString(">%s", pc_name[dn % 12]);
  }

  u8 cmbus = SEQ_CC_Get(track, SEQ_CC_CHORDMASK_BUS);
  u8 self = (cmbus & 0x04) ? 1 : 0;
  u16 mask = self
    ? ((((u16)SEQ_CC_Get(track, SEQ_CC_CHORDMASK_MASK_H) << 8)
        | SEQ_CC_Get(track, SEQ_CC_CHORDMASK_MASK_L)) & 0x0fff)
    : (SEQ_MIDI_IN_BusPCSetGet(cmbus & 0x03) & 0x0fff);
  SEQ_LCD_CursorSet(40, 1);
  SEQ_LCD_PrintString(self ? "M*:" : "M: ");
  int pc;
  for(pc=0; pc<12; ++pc) {
    if( mask & (1u << pc) )
      SEQ_LCD_PrintFormattedString(" %s", pc_name[pc]);
    else
      SEQ_LCD_PrintString(" . ");
  }
}

// Tension's zone table (PROC_FACE_TENSION_ZONES) — mirrors SEQ_UI_GRAVITY_ZoneName's
// thresholds (index 0..6 = DRONE..SLIP, DETENT at 3). tension_zone_jump[i] is each zone's
// rough midpoint, where the matching GP9-15 button lands you; Status_Tension's row1 and
// the GP-row button handler both read this one table.
static const char *const tension_zone_abbrev[7] = { "DRN","CHD","SCL","DET","LEN","RUB","SLP" };
static const s8          tension_zone_jump[7]   = { -56, -36, -12,   0,  12,  36,  56 };

static u8 tension_zone_index(s8 g)
{
  if( g == 0 ) return 3;
  if( g < 0 )  return (g >= -24) ? 2 : (g >= -48) ? 1 : 0;
  return (g <= 24) ? 4 : (g <= 48) ? 5 : 6;
}

// Tension's .status: ports the dedicated GRAVITY cockpit page's (seq_ui_gravity.c)
// visualization onto the rack row, which otherwise shows only the bare dial numbers —
// reusing SEQ_UI_GRAVITY_ZoneName (one zone-threshold source of truth, not a 2nd copy).
// Row 0: zone name + signed value, LEFT-anchored at col 41 (the base-layout row-readout
// zone, like Ptch's scale name; was right-justified pre-base-layout). Row 1 right
// screen: 7 cells (GP9-15, 5 cols each) naming each zone, current one bracketed;
// GP16's cell is the RESOLVE hint (see the PROC_FACE_TENSION_ZONES button handler for
// the actual gestures — this is display only). Row 1 left screen: the 16-track GRIP
// overview bar (who else is held by the field, same thresholds as the original page's
// bar) — UNLABELED at cols 24..39 since the 2026-07-12 dial expansion (Shade/FTS) took
// its old col-12 home; the dead cells right of FTS fit the 16 tracks exactly.
static void SEQ_UI_PROC_Status_Tension(u8 track, u8 slot)
{
  s8 g = seq_core_tension_gravity;

  SEQ_LCD_CursorSet(41, 0);
  SEQ_LCD_PrintFormattedString("%s %c%d", SEQ_UI_GRAVITY_ZoneName(g),
                               (g < 0) ? '-' : '+', (int)((g < 0) ? -(int)g : (int)g));

  SEQ_LCD_CursorSet(24, 1);
  {
    char bar[17];
    u8 i;
    for(i=0; i<16; ++i) {
      u8 gp = seq_cc_trk[i].tension_grip;
      bar[i] = gp ? (gp < 43 ? 'o' : (gp < 86 ? 'O' : '#')) : '.';
    }
    bar[16] = 0;
    SEQ_LCD_PrintString(bar);
  }

  u8 zi = tension_zone_index(g);
  u8 z;
  for(z=0; z<7; ++z) {
    SEQ_LCD_CursorSet((u16)(40 + z*5), 1);
    SEQ_LCD_PrintFormattedString((z == zi) ? "[%s]" : " %s ", tension_zone_abbrev[z]);
  }
  SEQ_LCD_CursorSet(75, 1);
  SEQ_LCD_PrintString("Rslv");
}

// Groove's .status: the selected style's name and, for a custom template, which lane the
// GP row is painting; presets read "(ro)" (paint the GP row only on a custom slot), off
// reads "(off)".
static void SEQ_UI_PROC_Status_Groove(u8 track, u8 slot)
{
  u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
  SEQ_LCD_CursorSet(41, 1);
  SEQ_LCD_PrintFormattedString("%-12s ", SEQ_GROOVE_NameGet(style));
  if( !style )
    SEQ_LCD_PrintString("(off)");
  else if( style < SEQ_GROOVE_NUM_PRESETS )
    SEQ_LCD_PrintString("(ro)");
  else
    SEQ_LCD_PrintFormattedString("paint %s",
      (proc_groove_paint_lane == 0) ? "Dly"
      : (proc_groove_paint_lane == 1) ? "Len" : "Vel");
}

// Robotize's .status: a "‹/› LOOP" discoverability hint on the OPERATE plane, the live
// loop status (cycles / palette / playing anchor) on the LOOP plane.
static void SEQ_UI_PROC_Status_Robotize(u8 track, u8 slot)
{
  SEQ_LCD_CursorSet(41, 1);
  if( SEQ_UI_PROC_CurFace(slot) == PROC_FACE_ROBOLOOP ) {
    u8 cyc = SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_LOOP_CYCLES);
    SEQ_LCD_PrintFormattedString("Cyc%-2d Pal%-2d play b%-2d",
      cyc, SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_PALETTE_LENGTH),
      cyc ? (SEQ_UI_PROC_RoboPlayingAnchor(track) + 1) : 0);
  } else {
    SEQ_LCD_PrintFormattedString("robotize %-3s  Up/Dn=LOOP",
      SEQ_CC_Get(track, SEQ_CC_ROBOTIZE_ACTIVE) ? "on" : "off");
  }
}

// Shared by PitchGen/TrigGen's .status (G2/G3) — identical shape (engaged state on
// OPERATE; window range + lock count on STEPS), differing only in which key-space's
// Get/IsEngaged pair to call. Prints at col 41 under the plane cue (base layout);
// the old "Up/Dn=STEPS" hint is gone — the "OPER 1/2" cue on row 0 names the planes
// now (2026-07-12 PGen mock).
static void SEQ_UI_PROC_Status_Gen(u8 track, u8 slot, u8 is_trg)
{
  SEQ_LCD_CursorSet(41, 1);
  u8 instr = SEQ_UI_PROC_GenInstr(track);
  seq_generator_t *g = is_trg ? SEQ_GENERATOR_TrgGet(track, instr) : SEQ_GENERATOR_Get(track, instr);
  proc_face_t steps_face = is_trg ? PROC_FACE_TRIGGEN_STEPS : PROC_FACE_PITCHGEN_STEPS;
  u8 engaged = is_trg ? SEQ_GENERATOR_TrgIsEngaged(track, instr) : SEQ_GENERATOR_IsEngaged(track, instr);
  if( SEQ_UI_PROC_CurFace(slot) == steps_face ) {
    u8 locked = 0;
    if( g ) {
      int i;
      for(i=0; i<16; ++i)
        if( SEQ_GENERATOR_LockGet(g, proc_gen_step_window*16 + i) )
          ++locked;
    }
    SEQ_LCD_PrintFormattedString("Steps %2d-%-3d locked:%-2d",
      proc_gen_step_window*16 + 1, proc_gen_step_window*16 + 16, locked);
    // The window's ACTIVITY strip (2026-07-13): what each step actually IS, so a
    // hand-played phrase can be locked BY SIGHT before the generator mutates around
    // it. Active = the step's GATE (what sounds; TGen's gate layer IS its content).
    // o = active unlocked ("yours to lock"), # = active+locked, - = locked empty
    // (pinned silence), . = empty, blank = past the track's length. The GP LEDs
    // stay the LOCK state (1:1 under the buttons); this is the truth table.
    {
      u16 num_steps = SEQ_TRG_NumStepsGet(track);
      int i;
      SEQ_LCD_CursorSet(64, 1);
      for(i=0; i<16; ++i) {
        u16 st = (u16)(proc_gen_step_window*16 + i);
        if( st >= num_steps ) { SEQ_LCD_PrintChar(' '); continue; }
        u8 gate = SEQ_TRG_GateGet(track, st, instr) ? 1 : 0;
        u8 lk   = (g && SEQ_GENERATOR_LockGet(g, st)) ? 1 : 0;
        SEQ_LCD_PrintChar(lk ? (gate ? '#' : '-') : (gate ? 'o' : '.'));
      }
    }
  } else if( g && engaged ) {
    if( seq_cc_trk[track].event_mode == SEQ_EVENT_MODE_Drum )
      SEQ_LCD_PrintFormattedString("D%-2d ENGAGED", instr + 1);
    else
      SEQ_LCD_PrintString("ENGAGED");
  } else if( g ) {
    SEQ_LCD_PrintString("disengaged  dblTap=ENGAGE");
  } else {
    SEQ_LCD_PrintString("dbl-tap B-row = ENGAGE");
  }
}
static void SEQ_UI_PROC_Status_PitchGen(u8 track, u8 slot) { SEQ_UI_PROC_Status_Gen(track, slot, 0); }
static void SEQ_UI_PROC_Status_TrigGen(u8 track, u8 slot)  { SEQ_UI_PROC_Status_Gen(track, slot, 1); }

// Humanize's .status: which of Note/Vel/Len the intensity actually touches — Int alone
// says nothing about that (unlike Echo/Groove/LFO, where the headline dial IS the whole
// story).
static void SEQ_UI_PROC_Status_Humanize(u8 track, u8 slot)
{
  u8 mode = SEQ_CC_Get(track, SEQ_CC_HUMANIZE_MODE);
  SEQ_LCD_CursorSet(41, 1);
  SEQ_LCD_PrintFormattedString("Note:%-3s Vel:%-3s Len:%-3s",
    (mode & (1 << 0)) ? "on" : "off",
    (mode & (1 << 1)) ? "on" : "off",
    (mode & (1 << 2)) ? "on" : "off");
}

// Voicing's .status: the voicing dials only act on Chord par layers — a plain Note
// track makes them silent no-ops, which the dial cells alone can't explain. Say so.
// On a chord track, name the strum direction (the Strm cell only shows the signed
// magnitude). Row 0 (the base-layout row-readout zone): the absorbed Limit half's
// range as NOTE NAMES when the clamp is active — the Lo/Hi cells only show raw
// 0..127; PrintNote's "---" for a 0 side is exactly Limit's open-side semantics.
// (The range clamps ANY track, so it prints even when the chord-layer warning shows.)
static void SEQ_UI_PROC_Status_Voicing(u8 track, u8 slot)
{
  // Raw CC reads: 0 = open side (stock encoding; the Hi DIAL shows it as 127), and a
  // legacy stored 127 is the same open top — both read as "no clamp" here, so the
  // readout only appears when the clamp actually bites (PrintNote's "---" = open side).
  u8 lo = SEQ_CC_Get(track, SEQ_CC_LIMIT_LOWER);
  u8 hi = SEQ_CC_Get(track, SEQ_CC_LIMIT_UPPER);
  if( lo || (hi && hi != 127) ) {
    SEQ_LCD_CursorSet(41, 0);
    SEQ_LCD_PrintString("Rng ");
    SEQ_LCD_PrintNote(lo);
    SEQ_LCD_PrintString("..");
    SEQ_LCD_PrintNote((hi == 127) ? 0 : hi);
  }

  SEQ_LCD_CursorSet(40, 1);
  if( seq_cc_trk[track].link_par_layer_chord < 0 ) {
    SEQ_LCD_PrintString("no Chord layer on trk (see TrkEvnt)");
    return;
  }
  u8 strum = SEQ_CC_Get(track, SEQ_CC_VOICE_STRUM);
  SEQ_LCD_PrintFormattedString("Chord voicing  Strum:%-4s",
    (strum == 64) ? "off" : (strum > 64) ? "up" : "down");
}

// Page buttons. The B-row (rack), GP encoders (operate), and GP-row mask are all
// global sel_view==PROC intercepts; the GP button ROW is the read-only mask, so
// swallow it. EXIT returns to where we came from.
static s32 SEQ_UI_PROC_page_Button(seq_ui_button_t button, s32 depressed)
{
  // GP RELEASES matter on the groove face (hold-step, 2026-07-13): the paint TOGGLE
  // fires on release — unless a Val turn landed during the hold, which converts the
  // press into a value edit (the EDIT-page hold-step idiom). Every other button
  // still acts on the press edge only.
  if( button >= SEQ_UI_BUTTON_GP1 && button <= SEQ_UI_BUTTON_GP16 && depressed ) {
    u8 pc = (u8)(button - SEQ_UI_BUTTON_GP1);
    if( pc == proc_groove_held_step ) {
      u8 turned = proc_groove_held_turned;
      // A QUICK release is the tap/toggle; a longer hold was a PEEK at the Val cell
      // (or a value edit) — release does nothing then.
      u8 quick = ((u32)MIOS32_TIMESTAMP_GetDelay(proc_groove_held_t0) < 350);
      proc_groove_held_step = 0xff;
      proc_groove_held_turned = 0;
      seq_ui_display_update_req = 1; // Val cell returns to the brush readout
      if( !turned && quick &&
          SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_GROOVE_PAINT ) {
        // the TAP half: toggle this step off (its value goes to the SHADOW) or back
        // on (the shadow restores it; shadowless cells paint the brush — 0 = the
        // legacy intensity-follow VPOS sentinel, else the literal offset)
        u8 track = SEQ_UI_VisibleTrackGet();
        seq_groove_entry_t *g = SEQ_UI_PROC_GrooveCustomEntry(track);
        s8 *lane = SEQ_UI_PROC_GrooveCustomLane(track);
        if( lane ) {
          u8 style = SEQ_CC_Get(track, SEQ_CC_GROOVE_STYLE) & 0x3f;
          if( proc_groove_shadow_style != style ) { // shadow follows ONE template
            memset(proc_groove_step_shadow, 0, sizeof(proc_groove_step_shadow));
            proc_groove_shadow_style = style;
          }
          if( g->num_steps < 16 )
            g->num_steps = 16; // paint-the-bar
          if( lane[pc] ) {
            proc_groove_step_shadow[proc_groove_paint_lane][pc] = lane[pc];
            lane[pc] = 0;
          } else {
            s8 back = proc_groove_step_shadow[proc_groove_paint_lane][pc];
            lane[pc] = back ? back
              : (proc_groove_paint_val ? proc_groove_paint_val : SEQ_GROOVE_VPOS);
          }
          if( SEQ_CC_Get(track, SEQ_CC_GROOVE_VALUE) == 0 )
            SEQ_CC_Set(track, SEQ_CC_GROOVE_VALUE, 32); // make the paint audible at once
          proc_groove_dirty = 1;
        }
      }
      return 1;
    }
    return 0;
  }
  if( depressed ) return 0;

  if( button >= SEQ_UI_BUTTON_GP1 && button <= SEQ_UI_BUTTON_GP16 ) {
    // GP row = the 12-PC mask. Read-only when bus-derived; EDITABLE in Self mode —
    // GP1..12 toggle pitch classes C..B in the static mask. Since the merged Ptch
    // row (2026-07-12) the keyboard is ALWAYS drawn, engaged or not, so the gate is
    // the CC's Self bit (visible == paintable), no longer the live slot: painting a
    // parked mask is harmless (the CCs are what an engage reads) and it unblocks
    // Arp-Self painting when ChordMask itself isn't engaged.
    u8 track = SEQ_UI_VisibleTrackGet();
    u8 pc = (u8)(button - SEQ_UI_BUTTON_GP1); // 0..15
    if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_CHORDMASK_SELF && pc < 12 &&
        (SEQ_CC_Get(track, SEQ_CC_CHORDMASK_BUS) & 0x04) ) {
      u16 mask = ((u16)SEQ_CC_Get(track, SEQ_CC_CHORDMASK_MASK_H) << 8)
               | SEQ_CC_Get(track, SEQ_CC_CHORDMASK_MASK_L);
      mask ^= (1u << pc); // toggle this pitch class
      SEQ_CC_Set(track, SEQ_CC_CHORDMASK_MASK_L, mask & 0xff);
      SEQ_CC_Set(track, SEQ_CC_CHORDMASK_MASK_H, (mask >> 8) & 0x0f);
    }
    // Groove GP row = paint the selected lane's 16-step shape. Custom templates only
    // (presets are const). Since the hold-step gesture (2026-07-13) the PRESS only
    // ARMS: a plain tap toggles on RELEASE (see the release path at the top of this
    // handler), and holding + turning the Val encoder dials that step's exact cell
    // instead. Edits persist to MBSEQ_G.V4 on page exit (proc_groove_dirty).
    else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_GROOVE_PAINT ) {
      if( pc < 16 && SEQ_UI_PROC_GrooveCustomLane(track) ) {
        proc_groove_held_step = pc;
        proc_groove_held_turned = 0;
        proc_groove_held_t0 = (u32)MIOS32_TIMESTAMP_Get(); // tap-vs-peek split
        seq_ui_display_update_req = 1; // Val cell shows the held step while held
      }
    }
    // LFO GP row = the waveform PALETTE. GP1..16 pick the palette shape; routed through
    // the Wave param so it engage-seeds depth/target just like turning the Wave dial.
    else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_LFO_PALETTE && pc < 16 ) {
      SEQ_UI_PROC_ParamWrite(track, &proc_params_lfo[0], lfo_wave_palette[pc]);
    }
    // Robotize LOOP face: GP row = the 16 bar-anchors. Tap = REROLL that slot (the molding
    // tool) — fresh random for that bar, others locked. Only on the LOOP plane.
    else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_ROBOLOOP && pc < 16 ) {
      SEQ_ROBOTIZE_RerollBar(track, pc);
    }
    // PitchGen STEPS face: GP row = LOCK toggle for the current 16-step WINDOW (Win selects
    // which quarter of the 64-step loop). Pre-ENGAGE (2026-07-13): no slot yet -> ADOPT
    // one on the first lock tap — disengaged, loop = the current source — so locks can
    // be set up front ("know what to expect") and the later ENGAGE mutates around them.
    else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_PITCHGEN_STEPS && pc < 16 ) {
      seq_generator_t *g = SEQ_UI_PROC_GenGet(track);
      if( !g && SEQ_GENERATOR_Adopt(track, SEQ_UI_PROC_GenInstr(track),
                                    SEQ_UI_PROC_GenParLayer(track)) == 0 )
        g = SEQ_UI_PROC_GenGet(track);
      if( g )
        SEQ_GENERATOR_LockToggle(track, SEQ_UI_PROC_GenInstr(track), proc_gen_step_window*16 + pc);
    }
    // TrigGen STEPS face (G3): same idiom, trigger key-space (+ the same pre-ENGAGE adopt).
    else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_TRIGGEN_STEPS && pc < 16 ) {
      seq_generator_t *g = SEQ_UI_PROC_TGenGet(track);
      if( !g && SEQ_GENERATOR_TrgAdopt(track, SEQ_UI_PROC_GenInstr(track),
                                       SEQ_UI_PROC_TrgLayer(track),
                                       SEQ_GENERATOR_DEFAULT_DENSITY) == 0 )
        g = SEQ_UI_PROC_TGenGet(track);
      if( g )
        SEQ_GENERATOR_TrgLockToggle(track, SEQ_UI_PROC_GenInstr(track), proc_gen_step_window*16 + pc);
    }
    // Tension zone jump: GP9-15 snap GRAVITY straight to that zone's midpoint (a manual
    // turn, same as the encoder — cancels an in-flight RESOLVE first). GP16 = RESOLVE, the
    // original GRAVITY page's smooth bar-quantized ramp back to the detent, unchanged.
    else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_TENSION_ZONES && pc >= 8 ) {
      if( pc == 15 ) {
        SEQ_CORE_TensionResolve();
        SEQ_UI_Msg_Track("RESOLVE -> the One");
      } else {
        SEQ_CORE_TensionResolveCancel();
        SEQ_CORE_TensionGravitySet(tension_zone_jump[pc - 8]);
      }
    }
    return 1; // swallow either way
  }

  switch( button ) {
  // The uniform PLANE toggle (G2). Bound to BOTH nav pairs because ‹/› (left/right) are
  // disabled (0xff) in the stock hwcfg — only Up/Down are wired on this panel. Any of the
  // four FLIPS between the row's two planes (toggle is exact for 2 planes; when a 3rd
  // plane lands, split into prev/next). Swallowed either way so nothing falls through.
  case SEQ_UI_BUTTON_Up:
  case SEQ_UI_BUTTON_Down:
  case SEQ_UI_BUTTON_Left:
  case SEQ_UI_BUTTON_Right:
    if( SEQ_UI_PROC_HasPlane2(ui_focused_proc_slot) ) {
      ui_proc_plane ^= 1;
      seq_ui_display_update_req = 1;
    }
    return 1;
  case SEQ_UI_BUTTON_Exit:
    SEQ_UI_PageSet((proc_prev_page == SEQ_UI_PAGE_PROC) ? SEQ_UI_PAGE_EDIT : proc_prev_page);
    return 1;
  }
  return -1;
}

// Leaving the page (by ANY route) ends PROC mode: drop the latched sel-view so the
// B-row / encoders / mask revert. This is what makes PROC page-scoped. If a Groove
// paint edited a custom template, persist MBSEQ_G.V4 on the way out (same file + gesture
// as the stock TRKGRV exit) so the shape survives a reboot.
static s32 SEQ_UI_PROC_page_Exit(void)
{
  if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_PROC )
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_NONE;
  if( proc_groove_dirty ) {
    proc_groove_dirty = 0;
    MUTEX_SDCARD_TAKE;
    s32 status = SEQ_FILE_G_Write(seq_file_session_name);
    MUTEX_SDCARD_GIVE;
    if( status < 0 )
      SEQ_UI_SDCardErrMsg(2000, status);
  }
  return 0; // no error
}

s32 SEQ_UI_PROC_Init(u32 mode)
{
  SEQ_UI_InstallButtonCallback(SEQ_UI_PROC_page_Button);
  SEQ_UI_InstallLCDCallback(SEQ_UI_PROC_page_LCD);
  SEQ_UI_InstallExitCallback(SEQ_UI_PROC_page_Exit);
  // No encoder/LED callback: the global sel_view==PROC intercepts own the GP
  // encoders (operate), the B-row (rack select), and the GP row (mask paint).
  seq_ui_sel_view = SEQ_UI_SEL_VIEW_PROC; // PROC mode is active while on this page
  return 0; // no error
}


static s32 SEQ_UI_Button_Live(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // The LIVE button is the PROC-mode home (decided 2026-07-03; page rework
  // 2026-07-05). It TOGGLES the Processor Rack page — tap in to sculpt the rack,
  // tap out to return where you came from. Its dedicated LED is the "in PROC mode"
  // lamp (lit while on the page). The displaced Live-Forwarding toggle is gone —
  // FWD_MIDI is pinned on as a permanent default (seq_record.c), so nothing here
  // touches it.
  if( ui_page == SEQ_UI_PAGE_PROC ) {
    SEQ_UI_PageSet((proc_prev_page == SEQ_UI_PAGE_PROC) ? SEQ_UI_PAGE_EDIT : proc_prev_page);
  } else {
    proc_prev_page = ui_page;
    ui_focused_proc_slot = PROC_ROW_PTCH; // land on the merged Ptch (pitch+chord) row
    ui_proc_plane = 0;                              // always enter on the primary plane
    SEQ_UI_PageSet(SEQ_UI_PAGE_PROC); // Init latches sel_view = PROC
  }

  return 0; // no error
}

// A quick UTILITY TAP (no sub-gesture, < this) opens the Utility page (stock); a
// HOLD arms CAPTURE and returns you to where you were on release — so reaching for
// CAPTURE never strands you on the Utility page.
#define CAPTURE_UTIL_TAP_MS 500

static s32 SEQ_UI_Button_Utility(s32 depressed)
{
  if( !depressed ) {
    // UTILITY pressed: arm the retroactive-CAPTURE modifier; don't navigate yet.
    // While held, the select row picks a destination track and a GP-n press grabs
    // the last n loops of the ring's track into it (see SEQ_UI_Button_GP /
    // SEQ_UI_Button_DirectTrack). The release below opens the Utility page only on
    // a quick tap. Disarm a pull hold (same hardening as the PATTERN gesture).
    capture_util_held = 1;
    capture_consumed = 0;
    capture_dst_track = 0xff;
    capture_status[0] = 0;
    capture_util_t0 = MIOS32_TIMESTAMP_Get();
    pull_held_track = 0xff;
    seq_ui_display_update_req = 1;
    return 0;
  }

  // UTILITY released. A quick TAP with no sub-gesture opens the Utility page
  // (stock). A HOLD — whether you committed a capture or just held to read the
  // ring depth and let go — returns you to where you were (no page jump).
  capture_util_held = 0;
  u8 consumed = capture_consumed;
  capture_consumed = 0;
  capture_dst_track = 0xff;
  seq_ui_display_update_req = 1;

  if( !consumed && (u32)MIOS32_TIMESTAMP_GetDelay(capture_util_t0) < CAPTURE_UTIL_TAP_MS )
    SEQ_UI_PageSet(SEQ_UI_PAGE_UTIL); // quick tap -> stock behavior: utility page

  return 0; // no error
}

static s32 SEQ_UI_Button_Copy(s32 depressed)
{
  static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;

  seq_ui_button_state.COPY = depressed ? 0 : 1;

  if( ui_page == SEQ_UI_PAGE_MIXER ) {
    if( depressed ) return -1;
    SEQ_UI_MIXER_Copy();
    SEQ_UI_Msg_MixerMap("copied");
    return 1;
  } else if( ui_page == SEQ_UI_PAGE_PARSEL ) {
    if( !depressed ) {
      SEQ_UI_UTIL_CopyParLayer();
      SEQ_UI_Msg_ParLayer("copied");
    }
  } else if( ui_page == SEQ_UI_PAGE_TRGSEL ) {
    if( !depressed ) {
      u8 visible_track = SEQ_UI_VisibleTrackGet();
      u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
      u8 selbuttons_available = seq_hwcfg_blm8x8.dout_gp_mapping == 3;
      if( event_mode == SEQ_EVENT_MODE_Drum && !selbuttons_available ) {
	SEQ_UI_UTIL_CopyInsLayer();
	SEQ_UI_Msg_InsLayer("copied");
      } else {
	SEQ_UI_UTIL_CopyTrgLayer();
	SEQ_UI_Msg_TrgLayer("copied");
      }
    }
  } else if( ui_page == SEQ_UI_PAGE_INSSEL ) {
    if( !depressed ) {
      SEQ_UI_UTIL_CopyInsLayer();
      SEQ_UI_Msg_InsLayer("copied");
    }
  } else if( ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_PATTERN_RMX ) {
    if( depressed ) return -1;
    if( SEQ_UI_PATTERN_MultiCopy(0) >= 0 )
      SEQ_UI_Msg_Patterns("copied");
    return 1;
  } else if( ui_page == SEQ_UI_PAGE_SONG ) {
    if( depressed ) return -1;
    SEQ_UI_SONG_Copy();
    SEQ_UI_Msg_SongPos("copied");
    return 1;
  } else if( SEQ_UI_TRKJAM_PatternRecordSelected() ) {
    if( depressed ) return -1;
    SEQ_UI_UTIL_CopyLivePattern();
    SEQ_UI_Msg_LivePattern("copied");
    return 1;
  } else {
    if( seq_ui_button_state.MENU_PRESSED ) {
      if( depressed ) return 1;
      SEQ_UI_PATTERN_MultiCopy(1);
      return 1;
    }

    if( !depressed ) {
      prev_page = ui_page;
      SEQ_UI_PageSet(SEQ_UI_PAGE_UTIL);
    }
    
    s32 status = 1;
    status = SEQ_UI_UTIL_CopyButton(depressed);

    if( depressed ) {
      if( prev_page != SEQ_UI_PAGE_UTIL )
	SEQ_UI_PageSet(prev_page);
      
      SEQ_UI_Msg_Track("copied");
    }

    return status;
  }

  return 1;
}

static s32 SEQ_UI_Button_Paste(s32 depressed)
{
  static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;

  seq_ui_button_state.PASTE = depressed ? 0 : 1;

  if( ui_page == SEQ_UI_PAGE_MIXER ) {
    if( depressed ) return -1;
    SEQ_UI_MIXER_Paste();
    SEQ_UI_Msg_MixerMap("pasted");
    return 1;
  } else if( ui_page == SEQ_UI_PAGE_PARSEL ) {
    if( !depressed ) {
      SEQ_UI_UTIL_PasteParLayer();
      SEQ_UI_Msg_ParLayer("pasted");
    }
  } else if( ui_page == SEQ_UI_PAGE_TRGSEL ) {
    if( !depressed ) {
      u8 visible_track = SEQ_UI_VisibleTrackGet();
      u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
      u8 selbuttons_available = seq_hwcfg_blm8x8.dout_gp_mapping == 3;
      if( event_mode == SEQ_EVENT_MODE_Drum && !selbuttons_available ) {
	SEQ_UI_UTIL_PasteInsLayer();
	SEQ_UI_Msg_InsLayer("pasted");
      } else {
	SEQ_UI_UTIL_PasteTrgLayer();
	SEQ_UI_Msg_TrgLayer("pasted");
      }
    }
  } else if( ui_page == SEQ_UI_PAGE_INSSEL ) {
    if( !depressed ) {
      SEQ_UI_UTIL_PasteInsLayer();
      SEQ_UI_Msg_InsLayer("pasted");
    }
  } else if( ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_PATTERN_RMX ) {
    if( depressed ) return -1;
    if( seq_ui_button_state.COPY ) {
      // copy+paste pressed: paste to next pattern position
      portENTER_CRITICAL();
      int group;
      for(group=0; group<SEQ_CORE_NUM_GROUPS; ++group) {
	if( seq_pattern[group].pattern < 63 )
	  ++seq_pattern[group].pattern;
	seq_pattern[group].REQ = 0;
	seq_pattern_req[group] = seq_pattern[group];
      }
      portEXIT_CRITICAL();
    }

    if( SEQ_UI_PATTERN_MultiPaste(0) >= 0 )
      SEQ_UI_Msg_Patterns("pasted");
    return 1;
  } else if( ui_page == SEQ_UI_PAGE_SONG ) {
    if( depressed ) return -1;
    SEQ_UI_SONG_Paste();
    SEQ_UI_Msg_SongPos("pasted");
    return 1;
  } else if( SEQ_UI_TRKJAM_PatternRecordSelected() ) {
    if( depressed ) return -1;
    SEQ_UI_UTIL_PasteLivePattern();
    SEQ_UI_Msg_LivePattern("pasted");
    return 1;
  } else {
    if( seq_ui_button_state.MENU_PRESSED ) {
      if( depressed ) return 1;
      SEQ_UI_PATTERN_MultiPaste(1);
      return 1;
    }

    if( seq_ui_button_state.COPY ) {
      // copy+paste pressed: duplicate steps
      if( !depressed ) {
	u8 visible_track = SEQ_UI_VisibleTrackGet();
	if( SEQ_UI_UTIL_PasteDuplicateSteps(visible_track) >= 1 ) {
	  SEQ_UI_Msg_Track("steps duplicated");
	} else {
	  SEQ_UI_Msg_Track("full - no duplication!");
	}
      }

      return 1;
    }

    if( !depressed ) {
      prev_page = ui_page;
      SEQ_UI_PageSet(SEQ_UI_PAGE_UTIL);
    }

    s32 status = SEQ_UI_UTIL_PasteButton(depressed);
    if( depressed ) {
      if( prev_page != SEQ_UI_PAGE_UTIL )
	SEQ_UI_PageSet(prev_page);

      if( seq_ui_button_state.SELECT_PRESSED )
	SEQ_UI_Msg_Layer("pasted");
      else
	SEQ_UI_Msg_Track("pasted");
    }

    return status;
  }

  return 1;
}


// Shared SELECT+CLEAR / UNDO-button dispatch: toggle the unified action journal
// and post a scope-aware message. Both buttons drive the same net, so this lives
// in one place to keep them from drifting (a stale message split is exactly the
// kind of divergence the unified journal was built to eliminate).
static void SEQ_UI_JournalToggleDispatch(void)
{
  u8 jstate = SEQ_CORE_JRNL_EMPTY, jscope = SEQ_CORE_JRNL_TRACK;
  SEQ_CORE_JournalInfoGet(&jstate, NULL, &jscope);
  if( jstate == SEQ_CORE_JRNL_UNDOABLE ) {
    s32 t = SEQ_CORE_JournalUndo();
    if( t >= 0 ) {
      if( jscope == SEQ_CORE_JRNL_ORGANISM ) {
        SEQ_UI_Msg_Track("REVERT undone");  // whole organism back to the pre-revert jam
      } else {
        char msg[16];
        sprintf(msg, "undone T%d", (int)t + 1);
        SEQ_UI_Msg_Track(msg);
      }
    }
  } else if( jstate == SEQ_CORE_JRNL_REDOABLE ) {
    s32 t = SEQ_CORE_JournalRedo();
    if( t >= 0 ) {
      if( jscope == SEQ_CORE_JRNL_ORGANISM ) {
        SEQ_UI_Msg_Track("REVERT redone");  // re-reverted to the checkpoint
      } else {
        char msg[16];
        sprintf(msg, "redone T%d", (int)t + 1);
        SEQ_UI_Msg_Track(msg);
      }
    }
  } else {
    SEQ_UI_Msg_Track("nothing to undo");
  }
}


static s32 SEQ_UI_Button_Clear(s32 depressed)
{
  static u8 select_clear_fired = 0;

  seq_ui_button_state.CLEAR = depressed ? 0 : 1;

  // SELECT+CLEAR = the panic UNDO/REDO toggle (the unified action journal,
  // §10(a2)): one gesture steps back the last deliberate track-grain verb
  // (pull / utility copy-paste-clear / generator ENGAGE / capture-to-track),
  // and pressing it again steps forward (redo). The midiphy panel has no UNDO
  // button (BUTTON_UNDO unmapped), so this is the performance-surface net.
  // SELECT+CLEAR NEVER falls through to a destructive clear — with nothing
  // armed it just reports, so a slipped SELECT can't cost material.
  if( depressed && select_clear_fired ) {
    select_clear_fired = 0;
    return 0; // swallow the release of a SELECT+CLEAR press
  }
  if( !depressed && seq_ui_button_state.SELECT_PRESSED ) {
    select_clear_fired = 1;
    SEQ_UI_JournalToggleDispatch();
    return 1;
  }

  if( ui_page == SEQ_UI_PAGE_MIXER ) {
    if( !depressed ) {
      SEQ_UI_MIXER_Clear();
      SEQ_UI_Msg_MixerMap("cleared");
    }
  } else if( ui_page == SEQ_UI_PAGE_PARSEL ) {
    if( !depressed ) {
      SEQ_UI_UTIL_ClearParLayer();
      SEQ_UI_Msg_ParLayer("cleared");
    }
  } else if( ui_page == SEQ_UI_PAGE_TRGSEL ) {
    if( !depressed ) {
      u8 visible_track = SEQ_UI_VisibleTrackGet();
      u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
      u8 selbuttons_available = seq_hwcfg_blm8x8.dout_gp_mapping == 3;
      if( event_mode == SEQ_EVENT_MODE_Drum && !selbuttons_available ) {
	SEQ_UI_UTIL_ClearInsLayer();
	SEQ_UI_Msg_InsLayer("cleared");
      } else {
	SEQ_UI_UTIL_ClearTrgLayer();
	SEQ_UI_Msg_TrgLayer("cleared");
      }
    }
  } else if( ui_page == SEQ_UI_PAGE_INSSEL ) {
    if( !depressed ) {
      SEQ_UI_UTIL_ClearInsLayer();
      SEQ_UI_Msg_InsLayer("cleared");
    }
  } else if( ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_PATTERN_RMX ) {
    if( depressed ) return -1;
    if( SEQ_UI_PATTERN_MultiClear(0) >= 0 )
      SEQ_UI_Msg_Patterns("cleared");
    return 1;
  } else if( ui_page == SEQ_UI_PAGE_SONG ) {
    if( !depressed ) {
      SEQ_UI_SONG_Clear();
      SEQ_UI_Msg_SongPos("cleared");
    }
  } else if( SEQ_UI_TRKJAM_PatternRecordSelected() ) {
    if( !depressed ) {
      SEQ_UI_UTIL_ClearLivePattern();
      SEQ_UI_Msg_LivePattern("cleared");
    }
  } else if( ui_page == SEQ_UI_PAGE_TRKJAM ) {
    if( !depressed ) {
      SEQ_UI_UTIL_ClearStep(SEQ_UI_VisibleTrackGet(), ui_selected_step, ui_selected_instrument);

      if( seq_record_state.ENABLED && !seq_record_options.STEP_RECORD ) {
	SEQ_UI_Msg(((ui_selected_step % 16) < 8) ? SEQ_UI_MSG_USER_R : SEQ_UI_MSG_USER, 1000, "Hold key to", "clear steps");
      } else {
	SEQ_UI_Msg_Step("cleared");
      }

      // print edit screen for 2 seconds
      ui_hold_msg_ctr = 2000;
      ui_hold_msg_ctr_drum_edit = 0; // ensure that drum triggers are displayed
      seq_ui_display_update_req = 1;
    }
  } else {
    if( seq_ui_button_state.MENU_PRESSED ) {
      if( depressed ) return 1;
      SEQ_UI_PATTERN_MultiClear(1);
      return 1;
    }

    if( !depressed ) {
      SEQ_UI_UTIL_ClearButton(0); // button pressed
      SEQ_UI_UTIL_ClearButton(1); // button depressed
      if( seq_ui_button_state.SELECT_PRESSED )
	SEQ_UI_Msg_Layer("cleared");
      else
	SEQ_UI_Msg_Track("cleared");
    }
  }

  return 1;
}


static s32 SEQ_UI_Button_Undo(s32 depressed)
{
  static u8 undo_fired = 0;

  seq_ui_button_state.UNDO = depressed ? 0 : 1;

  // MIXER page keeps its own map undo (a separate subsystem, not the journal).
  if( ui_page == SEQ_UI_PAGE_MIXER ) {
    if( depressed ) return -1;
    SEQ_UI_MIXER_Undo();
    SEQ_UI_Msg_MixerMap("Undo applied");
    return 1;
  }

  // Everywhere else this is the unified UNDO/REDO toggle — the same panic net
  // as SELECT+CLEAR. BUTTON_UNDO is unmapped on midiphy; kept here (and routed
  // through the journal, NOT the one-shot rollback) for rigs that map it.
  if( depressed && undo_fired ) {
    undo_fired = 0;
    return 0; // swallow the release
  }
  if( !depressed ) {
    undo_fired = 1;
    SEQ_UI_JournalToggleDispatch();
  }
  return 1;
}

static s32 SEQ_UI_Button_Move(s32 depressed)
{
  static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;

  seq_ui_button_state.MOVE = depressed ? 0 : 1;

  if( !depressed ) {
    prev_page = ui_page;
    SEQ_UI_PageSet(SEQ_UI_PAGE_UTIL);
  }

  s32 status = SEQ_UI_UTIL_MoveButton(depressed);
  if( depressed ) {
    if( prev_page != SEQ_UI_PAGE_UTIL )
      SEQ_UI_PageSet(prev_page);
  }

  return status;
}

static s32 SEQ_UI_Button_Scroll(s32 depressed)
{
  static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;

  seq_ui_button_state.SCROLL = depressed ? 0 : 1;

  if( !depressed ) {
    prev_page = ui_page;
    SEQ_UI_PageSet(SEQ_UI_PAGE_UTIL);
  }

  s32 status = SEQ_UI_UTIL_ScrollButton(depressed);
  if( depressed ) {
    if( prev_page != SEQ_UI_PAGE_UTIL )
      SEQ_UI_PageSet(prev_page);
  }

  return status;
}


static s32 SEQ_UI_Button_Menu(s32 depressed)
{
  if( seq_hwcfg_button_beh.menu ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    seq_ui_button_state.MENU_FIRST_PAGE_SELECTED = 0;
    if( !seq_ui_button_state.MENU_PRESSED ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.MENU_PRESSED ^= 1; // toggle MENU pressed (will also be released once GP button has been pressed)
  } else {
    // set mode
    seq_ui_button_state.MENU_FIRST_PAGE_SELECTED = 0;
    seq_ui_button_state.MENU_PRESSED = depressed ? 0 : 1;
  }

  return 0; // no error
}


// FEARLESS SWITCHING Stage C — SELECT+BOOKMARK hold threshold: below = a tap
// (CHECKPOINT), at/above = a hold (REVERT). REVERT discards the live jam, so it
// gets the deliberate long press. MIOS32_TIMESTAMP is mS-accurate.
#define ANCHOR_REVERT_HOLD_MS 1000

static s32 SEQ_UI_Button_Bookmark(s32 depressed)
{
  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  // FEARLESS SWITCHING Stage C — SELECT+BOOKMARK anchor gesture. Quick tap =
  // CHECKPOINT (bless all 4 groups to the anchor); hold >= ANCHOR_REVERT_HOLD_MS
  // = REVERT (restore the blessed organism). Armed only when SELECT is down at
  // BOOKMARK press; fires at release by measured duration; swallows press +
  // release so the combo never flips to the bookmarks view. Mirrors the
  // SELECT+CLEAR=undo idiom (RECOMBINE) — a deliberate combo, never a bare tap.
  static u8  anchor_armed = 0;
  static u32 anchor_t0 = 0;
  if( !depressed ) {                          // BOOKMARK pressed
    if( seq_ui_button_state.SELECT_PRESSED ) {
      anchor_armed = 1;
      anchor_t0 = (u32)MIOS32_TIMESTAMP_Get();
      return 0;                               // swallow — no bookmarks-view flip
    }
  } else if( anchor_armed ) {                 // BOOKMARK released, gesture armed
    anchor_armed = 0;
    if( (u32)MIOS32_TIMESTAMP_GetDelay(anchor_t0) >= ANCHOR_REVERT_HOLD_MS ) {
      if( SEQ_PATTERN_AnchorPresent() <= 0 ) {
        SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1500, "REVERT", "no checkpoint yet");
      } else {
        s32 r = SEQ_PATTERN_Revert();
        if( r >= 0 )
          SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "REVERT", "organism restored");
        else
          SEQ_UI_SDCardErrMsg(2000, r);
      }
    } else {
      s32 r = SEQ_PATTERN_Checkpoint();
      if( r >= 0 )
        SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "CHECKPOINT", "organism blessed");
      else
        SEQ_UI_SDCardErrMsg(2000, r);
    }
    return 0;                                 // swallow the release
  }

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_BOOKMARKS;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( seq_hwcfg_button_beh.bookmark ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.BOOKMARK ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
#if 0
    seq_ui_button_state.BOOKMARK ^= 1; // toggle BOOKMARK pressed (will also be released once GP button has been pressed)
#else
    seq_ui_button_state.BOOKMARK = 1; // seems that it's better *not* to toggle!
#endif
  } else {
    // set mode
    seq_ui_button_state.BOOKMARK = depressed ? 0 : 1;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    if( seq_ui_button_state.BOOKMARK ) {
      if( ui_page != SEQ_UI_PAGE_BOOKMARKS )
        ui_bookmarks_prev_page = ui_page;
      SEQ_UI_PageSet(SEQ_UI_PAGE_BOOKMARKS);
    } else {
      if( ui_page == SEQ_UI_PAGE_BOOKMARKS )
        SEQ_UI_PageSet(ui_bookmarks_prev_page);
    }
  }

  return 0; // no error
}


static s32 SEQ_UI_Button_DirectBookmark(s32 depressed, u32 bookmark_button)
{
  if( !depressed )
    ui_bookmarks_prev_page = ui_page;

  return SEQ_UI_BOOKMARKS_Button_Handler(bookmark_button, depressed);
}


static s32 SEQ_UI_Button_Enc(s32 depressed, u32 enc_button)
{
  // 0=Datawheel, 1..16=GPx, 17=BPM

  // PROC view: pushing a GP encoder snaps its parameter to the default (pass-
  // through) — the uniform "encoder push = snap-to-default" gesture (§4.8). GPn
  // push -> param (n-1) default. Fires on the press edge, through the param's
  // backing (SEQ_CC_Set / setter) so the slot re-syncs; the LCD overlay is the
  // confirmation (no transient message needed).
  // PROC view: the DATAWHEEL push jumps a GROUP (2026-07-13) — the wheel's turn
  // walks tracks ±1, the push leaps G1→G2→G3→G4→G1 keeping the track position
  // within the group. Fine/coarse navigation on one control; GxTy is the feedback.
  if( !depressed && seq_ui_sel_view == SEQ_UI_SEL_VIEW_PROC && enc_button == 0 ) {
    u8 nt = (u8)((SEQ_UI_VisibleTrackGet() + 4) & 0x0f);
    ui_selected_tracks = (u16)(1 << nt);
    ui_selected_group  = (u8)(nt >> 2);
    seq_ui_display_update_req = 1;
    // fall through — the tail keeps FAST_ENCODERS in step like every enc press
  }
  if( !depressed && seq_ui_sel_view == SEQ_UI_SEL_VIEW_PROC &&
      enc_button >= 1 && enc_button <= 16 ) {
    const proc_param_t *params; u8 nparams;
    SEQ_UI_PROC_SlotParams(ui_focused_proc_slot, &params, &nparams);
    u8 idx = (u8)enc_button - 1;
    if( idx < nparams ) {
      if( params[idx].kind == PROC_KIND_ACTION )
        SEQ_UI_PROC_RunAction(SEQ_UI_VisibleTrackGet(), params[idx].cc); // push = execute
      else
        SEQ_UI_PROC_ParamWrite(SEQ_UI_VisibleTrackGet(), &params[idx], params[idx].deflt);
      seq_ui_display_update_req = 1;
    }
  }

  // current hardcoded to FAST mode
  seq_ui_button_state.FAST_ENCODERS = depressed ? 0 : 1;

  SEQ_UI_InitEncSpeed(0); // no auto config

  return 0; // no error
}


static s32 SEQ_UI_Button_Select(s32 depressed)
{
  // double function: -> Bookmark if menu button pressed
  if( seq_ui_button_state.MENU_PRESSED )
    return SEQ_UI_Button_Bookmark(depressed);

  // forward to menu page
  seq_ui_button_state.SELECT_PRESSED = depressed ? 0 : 1;
  if( ui_button_callback != NULL ) {
    ui_button_callback(SEQ_UI_BUTTON_Select, depressed);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Exit(s32 depressed)
{
  // double function: -> Follow if menu button pressed
  if( seq_ui_button_state.MENU_PRESSED )
    return SEQ_UI_Button_Follow(depressed);

  if( depressed ) return -1; // ignore when button depressed

  // PHRASES name editor: EXIT commits the name + closes (same as GP16), and does
  // NOT fall through to page/menu navigation.
  if( phrase_name_edit ) {
    SEQ_PATTERN_PhraseNameCommit(phrase_name_edit_slot);
    phrase_name_edit = 0;
    seq_ui_display_update_req = 1;
    return 1;
  }

  u8 prev_ui_page = ui_page;

  // forward to menu page
  if( ui_button_callback != NULL ) {
    if( ui_button_callback(SEQ_UI_BUTTON_Exit, depressed) >= 1 )
      return 1; // page has already handled exit button
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  // release all button states
  // seq_ui_button_state.ALL = 0;
  // clashes with SOLO/ALL/etc.

  // enter menu page if we were not there before
  if( prev_ui_page != SEQ_UI_PAGE_MENU )
    SEQ_UI_PageSet(SEQ_UI_PAGE_MENU);

  return 0; // no error
}

static s32 SEQ_UI_Button_Edit(s32 depressed)
{
  seq_ui_button_state.EDIT_PRESSED = depressed ? 0 : 1;

  // change to edit page
  if( !depressed ) {
    SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT);

    // set/clear encoder fast function if required
    SEQ_UI_InitEncSpeed(1); // auto config
  }

  // EDIT button notification to button callback
  // currently only used in EDIT page itself!
  if( depressed && ui_page != SEQ_UI_PAGE_EDIT )
    return -1;

  if( ui_button_callback != NULL ) {
    ui_button_callback(SEQ_UI_BUTTON_Edit, depressed);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Mute(s32 depressed)
{
  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_MUTE;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    // note: this means that parameter layers can't be muted with this simplified handling
    seq_ui_button_state.MUTE_PRESSED = depressed ? 0 : 1;
  }

#if 0
  // doesn't really work since MUTE button also selects layer mutes when pressed
  //
  if( seq_hwcfg_button_beh.mute ) {
    if( depressed ) return -1; // ignore when button depressed

    ui_mute_prev_page = ui_page;
    SEQ_UI_PageSet(SEQ_UI_PAGE_MUTE);
  } else {
    if( seq_ui_button_state.MUTE_PRESSED ) {
      ui_mute_prev_page = ui_page;
      SEQ_UI_PageSet(SEQ_UI_PAGE_MUTE);
    } else {
      if( ui_page == SEQ_UI_PAGE_MUTE )
	SEQ_UI_PageSet(ui_mute_prev_page);
    }
  }
#else
  if( depressed ) return -1; // ignore when button depressed

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    SEQ_UI_PageSet(SEQ_UI_PAGE_MUTE);
  }
#endif

  return 0; // no error
}

static s32 SEQ_UI_Button_Pattern(s32 depressed)
{
  if( !depressed ) {
    // PATTERN pressed: arm the modifier; don't navigate yet. Source = visible
    // track. While held, GP1-8 pick the destination group, GP9-16 pick the
    // pattern and COMMIT capture → slot, and the lower select row commits
    // capture → that track (SEQ_UI_Button_DirectTrack). The release handler
    // below only handles the bare-tap navigation. The held-overlay is drawn by
    // the LCD dispatcher (search for PATTERN_PRESSED branch).
    seq_ui_button_state.PATTERN_PRESSED = 1;
    pattern_held_gp_consumed = 0;
    pattern_capture_group = 0xff;
    pattern_capture_dst_track = 0xff;
    pattern_capture_status[0] = 0;
    // disarm a pull hold: while PATTERN is held the select-row intercept above
    // eats the held button's release, which would leave the pull armed with no
    // button down (phantom pulls on later GP presses)
    pull_held_track = 0xff;
    return 0;
  }

  // PATTERN released.
  //   - No capture button touched during the hold -> bare tap; navigate to the
  //     Pattern page.
  //   - Otherwise a capture already committed on the GP9-16 / select-row press
  //     (or the user picked only a group, i.e. aborted) -> nothing to do.
  seq_ui_button_state.PATTERN_PRESSED = 0;

  u8 gp_consumed = pattern_held_gp_consumed;
  pattern_capture_group = 0xff;
  pattern_capture_dst_track = 0xff;
  pattern_held_gp_consumed = 0;

  if( !gp_consumed ) {
    SEQ_UI_MsgStop();
    SEQ_UI_PageSet(SEQ_UI_PAGE_PATTERN);
  }
  return 0;
}

static s32 SEQ_UI_Button_Pattern_Remix(s32 depressed)
{
  if ( ui_page == SEQ_UI_PAGE_PATTERN_RMX ) {
#ifndef MIOS32_FAMILY_EMULATION
    // to avoid race conditions using the same button(any other way of solving that?)
    vTaskDelay(60);
#endif
    // a trick to use the same button with 2 functionalitys
    ui_button_callback(SEQ_UI_BUTTON_Edit, depressed);
  } else {
    SEQ_UI_PageSet(SEQ_UI_PAGE_PATTERN_RMX);
  }

  return 0; // no error
	
}

// Where SONG was before it opened Capture, so a second press returns you there.
static seq_ui_page_t capture_prev_page = SEQ_UI_PAGE_PATTERN;

static s32 SEQ_UI_Button_Song(s32 depressed)
{
  // Repurposed (fork, 2026-06-27): SONG TOGGLES the unified Capture page. First
  // press enters (remembering the page you came from); pressing SONG again
  // returns you there. The active track (= the Capture source) never moves on
  // the page — the B-row only sets the destination — so only the page is
  // restored. SONG and PHRASE were redundant (both navigated to
  // SEQ_UI_PAGE_SONG); the song-arrangement page stays reachable via PHRASE
  // (every song-page read of SONG_PRESSED is OR'd with PHRASE_PRESSED).
  if( depressed ) return -1; // act on press only

  if( ui_page == SEQ_UI_PAGE_CAPTURE ) {
    SEQ_UI_PageSet((capture_prev_page == SEQ_UI_PAGE_CAPTURE) ? SEQ_UI_PAGE_PATTERN : capture_prev_page);
    return 0;
  }

  capture_prev_page = ui_page;
  return SEQ_UI_CAPTURE_Enter();
}

static s32 SEQ_UI_Button_Phrase(s32 depressed)
{
  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_PHRASE;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    seq_ui_button_state.PHRASE_PRESSED = depressed ? 0 : 1;

    if( depressed ) return -1; // ignore when button depressed

    SEQ_UI_PageSet(SEQ_UI_PAGE_SONG);
  }
  
  return 0; // no error
}

static s32 SEQ_UI_Button_Solo(s32 depressed)
{
  if( seq_hwcfg_button_beh.solo ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    seq_ui_button_state.SOLO ^= 1;    
  } else {
    // set mode
    seq_ui_button_state.SOLO = depressed ? 0 : 1;
  }

  // seq_core_trk_soloed currently only used for the BLM16x16+X
  // which overrules the legacy SOLO function
  if( !seq_ui_button_state.SOLO )
    seq_core_trk_soloed = 0;

  SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Solo", seq_ui_button_state.SOLO ? " on " : " off");

  return 0; // no error
}

static s32 SEQ_UI_Button_Fast(s32 depressed)
{
  if( seq_hwcfg_button_beh.fast ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    seq_ui_button_state.FAST_ENCODERS ^= 1;
  } else {
    // set mode
    seq_ui_button_state.FAST_ENCODERS = depressed ? 0 : 1;
  }

  SEQ_UI_InitEncSpeed(0); // no auto config

  return 0; // no error
}

static s32 SEQ_UI_Button_Fast2(s32 depressed)
{
  if( seq_hwcfg_button_beh.fast2 ) {
    // toggle mode
    if( depressed ) return -1; // ignore when button depressed
    seq_ui_button_state.FAST2_ENCODERS ^= 1;
  } else {
    // set mode
    seq_ui_button_state.FAST2_ENCODERS = depressed ? 0 : 1;
  }

  SEQ_UI_InitEncSpeed(0); // no auto config

  return 0; // no error
}

static s32 SEQ_UI_Button_All(s32 depressed)
{
  if( seq_hwcfg_button_beh.all ) {
    // toggle mode
    if( !depressed ) {
      seq_ui_button_state.CHANGE_ALL_STEPS ^= 1;
    }
  } else {
    // set mode
    seq_ui_button_state.CHANGE_ALL_STEPS = depressed ? 0 : 1;
  }

  if( !seq_ui_options.ALL_RELATIVE ) {
    // if users don't like ramps they can disable it in the options menu
    seq_ui_button_state.CHANGE_ALL_STEPS_SAME_VALUE = seq_ui_button_state.CHANGE_ALL_STEPS;
  } else {
    // toggle ramp mode - note that this will only work if toggle function is also enabled for ALL button
    seq_ui_button_state.CHANGE_ALL_STEPS_SAME_VALUE = depressed ? 0 : 1;
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_StepView(s32 depressed)
{
  //  static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;
  // also used by seq_ui_stepsel

  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_STEPS;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;

    if( ui_page == SEQ_UI_PAGE_MUTE || SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_SONG )
      SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT); // this selection only makes sense in EDIT page
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( seq_hwcfg_button_beh.step_view ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.STEP_VIEW ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.STEP_VIEW ^= 1; // toggle STEP_VIEW pressed (will also be released once GP button has been pressed)
  } else {
    // set mode
    seq_ui_button_state.STEP_VIEW = depressed ? 0 : 1;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    if( seq_ui_button_state.STEP_VIEW ) {
      ui_stepview_prev_page = ui_page;
      SEQ_UI_PageSet(SEQ_UI_PAGE_STEPSEL);
    } else {
      if( ui_page == SEQ_UI_PAGE_STEPSEL )
        SEQ_UI_PageSet(ui_stepview_prev_page);
    }
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_StepViewInc(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  int num_steps = SEQ_TRG_NumStepsGet(visible_track);

  int new_step_view = ui_selected_step_view + 1;
  if( (16*new_step_view) < num_steps ) {
    // select new step view
    ui_selected_step_view = new_step_view;

    // select step within view
    if( !seq_ui_button_state.CHANGE_ALL_STEPS ) { // don't change the selected step if ALL function is active, otherwise the ramp can't be changed over multiple views
      ui_selected_step = (ui_selected_step_view << 4) | (ui_selected_step & 0xf);
    }
  }

  char buffer[20];
  sprintf(buffer, "%d-%d", ui_selected_step_view*16 + 1, ui_selected_step_view*16 + 16);
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Step View", buffer);

  return 0; // no error
}

static s32 SEQ_UI_Button_StepViewDec(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  int new_step_view = ui_selected_step_view - 1;
  if( new_step_view >= 0 ) {
    // select new step view
    ui_selected_step_view = new_step_view;

    // select step within view
    if( !seq_ui_button_state.CHANGE_ALL_STEPS ) { // don't change the selected step if ALL function is active, otherwise the ramp can't be changed over multiple views
      ui_selected_step = (ui_selected_step_view << 4) | (ui_selected_step & 0xf);
    }
  }

  char buffer[20];
  sprintf(buffer, "%d-%d", ui_selected_step_view*16 + 1, ui_selected_step_view*16 + 16);
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Step View", buffer);

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackSel(s32 depressed)
{
  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_TRACKS;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( seq_hwcfg_button_beh.track_sel ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.TRACK_SEL ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.TRACK_SEL ^= 1; // toggle TRACKSEL status (will also be released once GP button has been pressed)
  } else {
    seq_ui_button_state.TRACK_SEL = depressed ? 0 : 1;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    if( seq_ui_button_state.TRACK_SEL ) {
      ui_tracksel_prev_page = ui_page;
      SEQ_UI_PageSet(SEQ_UI_PAGE_TRACKSEL);
    } else {
      if( ui_page == SEQ_UI_PAGE_TRACKSEL )
        SEQ_UI_PageSet(ui_tracksel_prev_page);
    }
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Group(s32 depressed, u32 group)
{
  if( depressed ) return -1; // ignore when button depressed

  if( group >= 4 ) return -2; // max. 4 group buttons

  // in song page: track and group buttons are used to select the cursor position
  if( ui_page == SEQ_UI_PAGE_SONG ) {
    ui_selected_item = 3 + group;
    return 0;
  }

  // if group has changed:
  if( group != ui_selected_group ) {
    // get current track selection
    u16 old_tracks = ui_selected_tracks >> (4*ui_selected_group);

    // select new group
    ui_selected_group = group;

    // take over old track selection
    ui_selected_tracks = old_tracks << (4*ui_selected_group);
  }

  // set/clear encoder fast function if required
  SEQ_UI_InitEncSpeed(1); // auto config

  return 0; // no error
}

static s32 SEQ_UI_Button_Track(s32 depressed, u32 track_button)
{
  static u8 button_state = 0x0f; // all 4 buttons depressed

  if( track_button >= 4 ) return -2; // max. 4 track buttons

  if( depressed ) {
    button_state |= (1 << track_button);
    return 0; // no error
  }

  button_state &= ~(1 << track_button);

  // in pattern and song page: use track buttons as group buttons
  if( ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_SONG ) {
    return SEQ_UI_Button_Group(depressed, track_button);
  }

  if( button_state == (~(1 << track_button) & 0xf) ) {
    // if only one select button pressed: radio-button function (1 of 4)
    ui_selected_tracks = 1 << (track_button + 4*ui_selected_group);
  } else {
    // if more than one select button pressed: toggle function (4 of 4)
    ui_selected_tracks ^= 1 << (track_button + 4*ui_selected_group);
  }

  // set/clear encoder fast function if required
  SEQ_UI_InitEncSpeed(1); // auto config

  return 0; // no error
}

// PHRASES — PHRASE-view GP hold threshold: a quick tap RECALLS the phrase (the
// frequent performance move); a hold >= this CAPTURES the live organism into it
// (occasional, and protected against accidentally overwriting a phrase you were
// only navigating to — the destructive write gets the deliberate hold, the same
// principle as CHECKPOINT-tap / REVERT-hold).
#define PHRASE_CAPTURE_HOLD_MS 1000

static s32 SEQ_UI_Button_DirectTrack(s32 depressed, u32 sel_button)
{
  static u16 button_state = 0xffff; // all 16 buttons depressed

  if( sel_button >= 16 ) return -2; // max. 16 direct track buttons

  // PHRASES name editor active: swallow the select row so a stray waypoint press
  // doesn't recall/capture mid-rename (the GP/step row is the keypad).
  if( phrase_name_edit )
    return 0;

  // Capture gesture (PATTERN held): the select row picks a DESTINATION track
  // within the slot — stash it; the GP9-16 pattern press commits the capture
  // into that track of the chosen slot (persisted). Swallow the button so the
  // normal track-select doesn't also fire — BUT still maintain button_state and
  // the take-over flag exactly as the normal handler would, otherwise a select
  // press/release that straddles the PATTERN-hold boundary leaves button_state
  // with a stuck bit and corrupts the track-select radio/toggle logic afterward
  // (symptom: no track LED lit, presses toggle instead of switching tracks).
  if( seq_ui_button_state.PATTERN_PRESSED ) {
    if( depressed ) {
      button_state |= (1 << sel_button);
    } else {
      button_state &= ~(1 << sel_button);
      seq_ui_button_state.TAKE_OVER_SEL_VIEW = 0;
      pattern_capture_dst_track = (u8)sel_button;
      pattern_held_gp_consumed = 1;
    }
    return 0;
  }

  // Retroactive CAPTURE gesture (UTILITY held): the select row picks the
  // DESTINATION track for the grab (stash; a GP-n press commits). Swallow the
  // press so the normal track-select doesn't fire — the visible track must NOT
  // change (a switch invalidates the ring). Maintain button_state exactly as the
  // normal handler (stuck-bit hardening, same as the PATTERN intercept above).
  if( capture_util_held ) {
    if( depressed ) {
      button_state |= (1 << sel_button);
    } else {
      button_state &= ~(1 << sel_button);
      seq_ui_button_state.TAKE_OVER_SEL_VIEW = 0;
      capture_dst_track = (u8)sel_button;
      capture_consumed = 1;
      seq_ui_display_update_req = 1;
    }
    return 0;
  }

  // Unified Capture page: the B-row picks the DESTINATION track only — it must
  // NOT move the active/visible track, which is the SOURCE (it "actively tracks
  // which track you are on"). Set the page's dst (the select-row LEDs follow it,
  // special-cased below) and SWALLOW — must not fall through to the pull-arm /
  // stock select, which would change the visible track AND arm a RECOMBINE pull
  // whose later GP-number press would shadow the Capture commit. button_state
  // maintained (stuck-bit hardening, as above).
  if( ui_page == SEQ_UI_PAGE_CAPTURE ) {
    if( depressed ) {
      button_state |= (1 << sel_button);
    } else {
      button_state &= ~(1 << sel_button);
      seq_ui_button_state.TAKE_OVER_SEL_VIEW = 0;
      SEQ_UI_CAPTURE_SetDstTrack((u8)sel_button);
    }
    return 0;
  }

  // Pull gesture (RECOMBINE): the FIRST held select-row button is the pull
  // destination; while it is held, OTHER select-row presses pick the SOURCE
  // column (intercepted — this shadows the stock multi-track chord-select).
  // The held button's own press/release flows through unchanged, so the stock
  // release-select still fires and the cursor follows the transfusion target.
  //
  // TRACKS view ONLY: the select row only means "destination track" here. In
  // PHRASE/MUTE/etc. the row is repurposed (PHRASE = snapshot waypoints), so
  // arming a pull there would let a later top-row GP press fire a phantom
  // bar-aligned LoadTrackFromSlot into a track the user never targeted — e.g. a
  // GP press during a phrase capture-hold (the hold keeps the select button down
  // the whole window). Disarm a stale hold if we've left TRACKS view while
  // armed; the commit (top-row handler) is gated on TRACKS too.
  if( seq_ui_sel_view != SEQ_UI_SEL_VIEW_TRACKS ) {
    pull_held_track = 0xff;
    pull_src_column = 0xff;
  } else if( pull_held_track == 0xff ) {
    if( !depressed && button_state == 0xffff ) {
      // sole press: arm the hold (do not consume — stock handling continues)
      pull_held_track = (u8)sel_button;
      pull_src_column = 0xff;
      pull_letter = 0xff;
      pull_status[0] = 0;
    }
  } else if( sel_button == pull_held_track ) {
    if( depressed )
      pull_held_track = 0xff; // disarm (do not consume)
  } else if( !seq_ui_button_state.PATTERN_PRESSED ) {
    // another select press while held -> source column pick. Maintain
    // button_state exactly as the normal handler would (see the stuck-bit
    // note on the PATTERN intercept above).
    if( depressed ) {
      button_state |= (1 << sel_button);
    } else {
      button_state &= ~(1 << sel_button);
      seq_ui_button_state.TAKE_OVER_SEL_VIEW = 0;
      pull_src_column = (u8)sel_button;
    }
    return 0;
  }

  u8 selbuttons_available = seq_hwcfg_blm8x8.dout_gp_mapping == 3;

  if( depressed ) {
    button_state |= (1 << sel_button);
    if( !selbuttons_available )
      return 0; // no error
  } else {
    button_state &= ~(1 << sel_button);
  }

  u8 visible_track = SEQ_UI_VisibleTrackGet();

  if( selbuttons_available ) {
    if( !depressed ) // selection button has been pressed while Bookm/Step/Track/Param/Trigger/Instr/Mute/Phrase button pressed: don't take over new sel view anymore
      seq_ui_button_state.TAKE_OVER_SEL_VIEW = 0;

    // for selection buttons of Antilog PCB
    switch( seq_ui_sel_view ) {
      case SEQ_UI_SEL_VIEW_BOOKMARKS:
	SEQ_UI_BOOKMARKS_Button_Handler((seq_ui_button_t)sel_button, depressed);
	break;
      case SEQ_UI_SEL_VIEW_STEPS:
	SEQ_UI_STEPSEL_Button_Handler((seq_ui_button_t)sel_button, depressed);
	break;
      case SEQ_UI_SEL_VIEW_TRACKS: {
	if( depressed )
	  return 0; // no error

	if( button_state == (~(1 << sel_button) & 0xffff) ) {
	  // if only one select button pressed: radio-button function (1 of 16)
	  ui_selected_tracks = 1 << sel_button;
	  ui_selected_group = sel_button / 4;
	} else {
	  // if more than one select button pressed: toggle function (16 of 16)
	  ui_selected_tracks ^= 1 << sel_button;
	}
      } break;
      case SEQ_UI_SEL_VIEW_PAR:
	SEQ_UI_PARSEL_Button_Handler((seq_ui_button_t)sel_button, depressed);
	break;
      case SEQ_UI_SEL_VIEW_TRG:
	SEQ_UI_TRGSEL_Button_Handler((seq_ui_button_t)sel_button, depressed);
	break;
      case SEQ_UI_SEL_VIEW_INS:
	// the B-row is the live play-surface (drum pads / keyboard) — or the
	// instrument selector when the play option is off (handled inside).
	SEQ_UI_INSSEL_SelectRow_Button((seq_ui_button_t)sel_button, depressed);
	break;
      case SEQ_UI_SEL_VIEW_MUTE: {
	if( depressed )
	  return 0; // no error

	u16 mask = 1 << sel_button;
	u16 *mute_flags = seq_ui_button_state.MUTE_PRESSED ? &seq_core_trk[visible_track].layer_muted : &seq_core_trk_muted;
	portENTER_CRITICAL();
	if( *mute_flags & mask ) {
	  *mute_flags &= ~mask;

          if( seq_ui_options.SELECT_UNMUTED_TRACK ) {
            if( seq_ui_button_state.MUTE_PRESSED ) {
              // simplified usage: select the par layer
	      ui_selected_par_layer = sel_button;
	    } else {
	      // simplified usage: select the track
	      ui_selected_tracks = 1 << sel_button;
	      ui_selected_group = sel_button/4;
	    }
          }
	} else {
	  *mute_flags |= mask;
	}
	portEXIT_CRITICAL();
      } break;
      case SEQ_UI_SEL_VIEW_PHRASE: {
	// PHRASES — the snapshot library. The PHRASE-view row is N waypoints:
	// tap GP n = RECALL phrase n (restore the whole organism, bar-aligned,
	// generators resume engaged); hold GP n >= PHRASE_CAPTURE_HOLD_MS =
	// CAPTURE the live organism into phrase n. Armed on press, fired by
	// measured duration at release (mirrors the SELECT+BOOKMARK anchor
	// gesture). Recall of an un-captured phrase refuses with a flash.
	// (Stage A: this overloads the old song-pos fetch on this view — classic
	// song-step editing stays on the SONG page. Gesture is provisional, tuned
	// by ear with hardware in hand per the FEARLESS precedent.)
	static u32 phrase_t0 = 0;
	static u8  phrase_armed_btn = 0xff;

	// SELECT + tap a waypoint = ARM a POSTURE-MORPH toward that phrase (the
	// continuous transition: ride the feel toward B, then the structural jump
	// is the ordinary bar-aligned recall — two complementary gestures, §10).
	// Tap-alone stays recall, hold stays capture; SELECT + the armed slot again
	// disarms. Acts on release; consumes the press so the hold timer never fires.
	if( seq_ui_button_state.SELECT_PRESSED ) {
	  phrase_armed_btn = 0xff;                 // SELECT-held -> never a capture
	  if( depressed ) {                        // act on RELEASE
	    u8 n = (u8)sel_button;
	    ui_selected_phrase = n;
	    if( SEQ_PATTERN_PhraseMorphTarget() == (s32)n ) {
	      SEQ_PATTERN_PhraseMorphCancel();     // re-arm same slot = toggle off
	      SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "posture morph", "off");
	    } else if( SEQ_PATTERN_PhrasePresent(n) <= 0 ) {
	      char lbl[21];
	      SEQ_UI_PhraseName_MsgLabel(n, lbl);
	      SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1500, lbl, "can't morph: empty");
	    } else {
	      s32 r = SEQ_PATTERN_PhraseMorphArm(n);
	      if( r >= 0 ) {
		morph_armed_page = ui_page; // scope the GP/datawheel/LED controls to here
		char lbl[21];
		SEQ_UI_PhraseName_MsgLabel(n, lbl);
		SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, lbl, "morph armed");
	      } else
		SEQ_UI_SDCardErrMsg(2000, r);
	    }
	    seq_ui_display_update_req = 1;
	  }
	  return 0;
	}

	if( !depressed ) {                         // PRESS: arm the hold timer
	  phrase_t0 = (u32)MIOS32_TIMESTAMP_Get();
	  phrase_armed_btn = (u8)sel_button;
	  return 0;
	}
	if( phrase_armed_btn != (u8)sel_button )   // RELEASE of a non-armed button
	  return 0;
	phrase_armed_btn = 0xff;

	u8 n = (u8)sel_button;
	ui_selected_phrase = n; // row highlight follows the touched waypoint
	if( (u32)MIOS32_TIMESTAMP_GetDelay(phrase_t0) >= PHRASE_CAPTURE_HOLD_MS ) {
	  s32 r = SEQ_PATTERN_PhraseCapture(n);
	  if( r >= 0 ) {
	    // Captured. Naming is OPT-IN now (2026-06-22, by-ear): do NOT auto-open the
	    // keypad — that auto-open made every capture pay a name-commit write on EXIT
	    // plus a modal interruption. A bare capture is now just the four group-record
	    // writes. Confirm with a message; a deliberate rename can be wired later.
	    char lbl[21];
	    SEQ_UI_PhraseName_MsgLabel(n, lbl);
	    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, lbl, "phrase captured");
	    seq_ui_display_update_req = 1;
	  } else
	    SEQ_UI_SDCardErrMsg(2000, r);
	} else if( SEQ_PATTERN_PhrasePresent(n) <= 0 ) {
	  char lbl[21];
	  SEQ_UI_PhraseName_MsgLabel(n, lbl);
	  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1500, lbl, "empty slot");
	} else {
	  s32 r = SEQ_PATTERN_PhraseRecall(n);
	  if( r >= 0 ) {
	    char lbl[21];
	    SEQ_UI_PhraseName_MsgLabel(n, lbl);
	    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, lbl, "recalled");
	  } else
	    SEQ_UI_SDCardErrMsg(2000, r);
	}
      } break;
      case SEQ_UI_SEL_VIEW_PROC: {
	// PROC — the visible track's processor rack on the B-row. One key per rack
	// ROW (PTCH / VOIC / TENS / PGEN / TGEN / Echo…); keys past the rack dark.
	// Tap = focus (the GP encoders then operate that row). Double-tap = the row's
	// on/off gesture: the param-driven stack rows (Ptch/Tension) reset every
	// param to pass-through (bypass) — and Ptch, which absorbed the ChordMask row
	// (2026-07-12), ALSO returns a ChordMask-playmode track to Normal (the old
	// row's add/remove toggle; its slot presence IS the playmode); an EMISSION row
	// (Echo) toggles its native disable bit, preserving the dialled value. All via
	// SEQ_CC_Set / the setter. Acts on RELEASE, like the other views.
	static u32 proc_tap_t0 = 0;
	static u8  proc_tap_slot = 0xff;
	if( depressed )
	  return 0; // act on release
	if( sel_button >= PROC_NUM_ROWS )
	  return 0; // keys past the rack unused

	u8 slot = (u8)sel_button;
	u8 dbl = (slot == proc_tap_slot) &&
	         ((u32)MIOS32_TIMESTAMP_GetDelay(proc_tap_t0) < 350);
	proc_tap_slot = slot;
	proc_tap_t0 = (u32)MIOS32_TIMESTAMP_Get();

	ui_focused_proc_slot = slot; // a single tap always focuses
	ui_proc_plane = 0;           // focusing a row always lands on its primary plane

	if( dbl ) {
	  if( proc_rows[slot].rowkind == PROC_ROW_EMISSION ) {
	    // Emission row: bypass keeps the dialled config. The enable bit is either a mask
	    // in occ_cc (Echo 0x40 / Groove/LFO 0x80 — flip it) or a separate enable_cc
	    // (Robotize ACTIVE — toggle it). Reads the row's descriptor, not a hardcoded CC.
	    u8 bypassed;
	    if( proc_rows[slot].disable_mask ) {
	      u8 cc = proc_rows[slot].disable_cc ? proc_rows[slot].disable_cc : proc_rows[slot].occ_cc;
	      u8 dm = proc_rows[slot].disable_mask;
	      u8 raw = SEQ_CC_Get(visible_track, cc) ^ dm;
	      SEQ_CC_Set(visible_track, cc, raw);
	      bypassed = (raw & dm) ? 1 : 0;
	    } else { // enable_cc split (Robotize)
	      u8 en = !SEQ_CC_Get(visible_track, proc_rows[slot].enable_cc);
	      SEQ_CC_Set(visible_track, proc_rows[slot].enable_cc, en);
	      bypassed = !en;
	    }
	    // The merged Voic row's Limit half is INDEPENDENT of the bypass (decided
	    // 2026-07-12, reversing the same-day "bypass + limit off" draft): the 0x80
	    // bit only gates the voicing dials in the DSP, and the double-tap leaves
	    // Lo/Hi alone — an active clamp keeps applying (and keeps the row's LED
	    // green/enabled via the RowState arm). Kill the clamp by dialling — or
	    // encoder-pushing — Lo and Hi to 0.
	    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, (char *)SEQ_UI_PROC_SlotName(slot),
	               bypassed ? "  bypassed" : "   enabled");
	  } else if( proc_rows[slot].rowkind == PROC_ROW_GENERATOR ) {
	    // Generator row: ENGAGE <-> DISENGAGE — a pool-slot alloc/stop, not a CC flip.
	    // DISENGAGE keeps the slot + loop (config preserved, same spirit as bypass);
	    // ENGAGE (re-)allocates and seeds, surfacing its own failure reasons (pool full /
	    // bad track / no target layer assigned) like the stock PITCHGEN page does.
	    // PitchGen and TrigGen are both this rowkind but separate key-spaces (G3).
	    u8 instr = SEQ_UI_PROC_GenInstr(visible_track);
	    if( SEQ_UI_PROC_IsTrigGen(slot) ) {
	      if( SEQ_GENERATOR_TrgIsEngaged(visible_track, instr) ) {
	        SEQ_GENERATOR_TrgDisengage(visible_track, instr);
	        SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "TrigGen", "disengaged");
	      } else {
	        s32 r = SEQ_GENERATOR_EngageTrigger(visible_track, instr,
	                  SEQ_UI_PROC_TrgLayer(visible_track), SEQ_GENERATOR_DEFAULT_DENSITY);
	        switch( r ) {
	        case 0:  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "TrigGen", "   ENGAGED"); break;
	        case -1: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "TrigGen", "pool full"); break;
	        case -2: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "TrigGen", "bad trk/line"); break;
	        case -3: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "TrigGen", "need Gate layer"); break;
	        default: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "TrigGen", "ENGAGE failed");
	        }
	      }
	    } else if( SEQ_GENERATOR_IsEngaged(visible_track, instr) ) {
	      SEQ_GENERATOR_Disengage(visible_track, instr);
	      SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "PitchGen", "disengaged");
	    } else {
	      s32 r = SEQ_GENERATOR_Engage(visible_track, instr, SEQ_UI_PROC_GenParLayer(visible_track));
	      switch( r ) {
	      case 0:  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "PitchGen", "   ENGAGED"); break;
	      case -1: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "PitchGen", "pool full"); break;
	      case -2: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "PitchGen", "bad trk/line"); break;
	      case -3: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "PitchGen", "need Note layer"); break;
	      default: SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 2000, "PitchGen", "ENGAGE failed");
	      }
	    }
	  } else {
	    SEQ_UI_PROC_SlotReset(visible_track, slot);
	    // The merged Ptch row also owns ChordMask, whose slot presence IS the track
	    // playmode: the dial reset alone (Str->0) leaves the mode engaged at pass-
	    // through, so the double-tap also returns the track to Normal (the dissolved
	    // ChordMask row's remove half; painted mask CCs survive, like an FX bypass).
	    if( slot == PROC_ROW_PTCH &&
	        SEQ_CC_Get(visible_track, SEQ_CC_MODE) == SEQ_CORE_TRKMODE_ChordMask )
	      SEQ_CC_Set(visible_track, SEQ_CC_MODE, SEQ_CORE_TRKMODE_Normal);
	    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, (char *)SEQ_UI_PROC_SlotName(slot), "  bypassed");
	  }
	}
      } break;
      }
  } else {
    // common behaviour
    if( button_state == (~(1 << sel_button) & 0xffff) ) {
      // if only one select button pressed: radio-button function (1 of 16)
      ui_selected_tracks = 1 << sel_button;
      ui_selected_group = sel_button / 4;
    } else {
      // if more than one select button pressed: toggle function (16 of 16)
      ui_selected_tracks ^= 1 << sel_button;
    }
  }

  // set/clear encoder fast function if required
  SEQ_UI_InitEncSpeed(1); // auto config

  return 0; // no error
}

static s32 SEQ_UI_Button_ParLayerSel(s32 depressed)
{
  // static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;
  // also used by seq_ui_parsel.c

  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_PAR;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;

    if( ui_page == SEQ_UI_PAGE_MUTE || ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_SONG )
      SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT); // this selection only makes sense in EDIT page
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( seq_hwcfg_button_beh.par_layer ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.PAR_LAYER_SEL ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.PAR_LAYER_SEL ^= 1; // toggle PARSEL status (will also be released once GP button has been pressed)
  } else {
    seq_ui_button_state.PAR_LAYER_SEL = depressed ? 0 : 1;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    if( seq_ui_button_state.PAR_LAYER_SEL ) {
      if( ui_page != SEQ_UI_PAGE_PARSEL ) {
        ui_parlayer_prev_page = ui_page;
        SEQ_UI_PageSet(SEQ_UI_PAGE_PARSEL);
      }
    } else {
      if( ui_page == SEQ_UI_PAGE_PARSEL )
        SEQ_UI_PageSet(ui_parlayer_prev_page);
    }
  }

  // set/clear encoder fast function if required
  SEQ_UI_InitEncSpeed(1); // auto config

  return 0; // no error
}

static s32 SEQ_UI_Button_ParLayer(s32 depressed, u32 par_layer)
{
  static u8 layer_c_pressed = 0;

  if( par_layer >= 3 ) return -2; // max. 3 parlayer buttons

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 num_p_layers = SEQ_PAR_NumLayersGet(visible_track);

  // in song page: parameter layer buttons are used to select the cursor position
  if( ui_page == SEQ_UI_PAGE_SONG ) {
    ui_selected_item = par_layer;
    return 0;
  }

  // drum mode in edit page: print parameter layer as long as button is pressed
  if( ui_page == SEQ_UI_PAGE_EDIT ) {
    u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      ui_hold_msg_ctr = depressed ? 0 : ~0; // show value for at least 65 seconds... enough?
      if( ui_hold_msg_ctr )
	ui_hold_msg_ctr_drum_edit = 1; // ensure that layer will be displayed
    }
  }

  // holding Layer C button allows to increment/decrement layer with A/B button
  if( par_layer == 2 )
    layer_c_pressed = !depressed;

  if( layer_c_pressed && par_layer == 0 ) {
    if( depressed ) return -1; // ignore when button depressed
    // increment layer
    if( ++ui_selected_par_layer >= num_p_layers )
      ui_selected_par_layer = 0;
  } else if( layer_c_pressed && par_layer == 1 ) {
    if( depressed ) return -1; // ignore when button depressed
    // decrement layer
    if( ui_selected_par_layer == 0 )
      ui_selected_par_layer = num_p_layers - 1;
    else
      --ui_selected_par_layer;
  } else {
    if( num_p_layers <= 3 ) {
      // 3 layers: direct selection with LayerA/B/C button
      if( depressed ) return -1; // ignore when button depressed
      if( par_layer >= num_p_layers ) {
	char str1[21];
	sprintf(str1, "Parameter Layer %c", 'A'+par_layer);
	SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, str1, "not available!");
	ui_hold_msg_ctr = 0;
      } else {
	seq_ui_button_state.PAR_LAYER_SEL = 0;
	ui_selected_par_layer = par_layer;
      }
    } else if( num_p_layers <= 4 ) {
      // 4 layers: LayerC Button toggles between C and D
      if( depressed ) return -1; // ignore when button depressed
      seq_ui_button_state.PAR_LAYER_SEL = 0;
      if( par_layer == 2 )
	ui_selected_par_layer = (ui_selected_par_layer == 2) ? 3 : 2;
      else
	ui_selected_par_layer = par_layer;
    } else {
      // >4 layers: LayerA/B button selects directly, Layer C button enters layer selection page
      if( par_layer <= 1 ) {
	if( depressed ) return -1; // ignore when button depressed
	seq_ui_button_state.PAR_LAYER_SEL = 0;
	ui_selected_par_layer = par_layer;

	if( ui_page == SEQ_UI_PAGE_PARSEL )
	  SEQ_UI_PageSet(ui_parlayer_prev_page);
      } else {
	return SEQ_UI_Button_ParLayerSel(depressed);
      }
    }
  }

  // set/clear encoder fast function if required
  SEQ_UI_InitEncSpeed(1); // auto config

  return 0; // no error
}

static s32 SEQ_UI_Button_TrgLayerSel(s32 depressed)
{
  // static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;
  // also used by seq_ui_trgsel.c

  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_TRG;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;

    if( ui_page == SEQ_UI_PAGE_MUTE || ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_SONG )
      SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT); // this selection only makes sense in EDIT page
  } else {
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( seq_hwcfg_button_beh.trg_layer ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.TRG_LAYER_SEL ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.TRG_LAYER_SEL ^= 1; // toggle TRGSEL status (will also be released once GP button has been pressed)
  } else {
    seq_ui_button_state.TRG_LAYER_SEL = depressed ? 0 : 1;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    if( seq_ui_button_state.TRG_LAYER_SEL ) {
      if( ui_page != SEQ_UI_PAGE_TRGSEL ) {
        ui_trglayer_prev_page = ui_page;
        SEQ_UI_PageSet(SEQ_UI_PAGE_TRGSEL);
      }
    } else {
      if( ui_page == SEQ_UI_PAGE_TRGSEL )
        SEQ_UI_PageSet(ui_trglayer_prev_page);
    }
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_TrgLayer(s32 depressed, u32 trg_layer)
{
  static u8 layer_c_pressed = 0;

  if( trg_layer >= 3 ) return -2; // max. 3 trglayer buttons

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
  u8 num_t_layers = SEQ_TRG_NumLayersGet(visible_track);

  // drum mode in edit page: ensure that trigger layer is print again
  if( ui_page == SEQ_UI_PAGE_EDIT ) {
    u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      if( !depressed )
	ui_hold_msg_ctr = 0;
    }
  }

  // holding Layer C button allows to increment/decrement layer with A/B button
  if( trg_layer == 2 )
    layer_c_pressed = !depressed;

  if( layer_c_pressed && trg_layer == 0 ) {
    if( depressed ) return -1; // ignore when button depressed
    // increment layer
    if( ++ui_selected_trg_layer >= num_t_layers )
      ui_selected_trg_layer = 0;
  } else if( layer_c_pressed && trg_layer == 1 ) {
    if( depressed ) return -1; // ignore when button depressed
    // decrement layer
    if( ui_selected_trg_layer == 0 )
      ui_selected_trg_layer = num_t_layers - 1;
    else
      --ui_selected_trg_layer;
  } else {
    if( event_mode != SEQ_EVENT_MODE_Drum && num_t_layers <= 3 ) {
      // 3 layers: direct selection with LayerA/B/C button
      if( depressed ) return -1; // ignore when button depressed
      if( trg_layer >= num_t_layers ) {
	char str1[21];
	sprintf(str1, "Trigger Layer %c", 'A'+trg_layer);
	SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, str1, "not available!");
      } else {
	seq_ui_button_state.TRG_LAYER_SEL = 0;
	ui_selected_trg_layer = trg_layer;
      }
    } else if( event_mode != SEQ_EVENT_MODE_Drum && num_t_layers <= 4 ) {
      // 4 layers: LayerC Button toggles between C and D
      if( depressed ) return -1; // ignore when button depressed
      seq_ui_button_state.TRG_LAYER_SEL = 0;
      if( trg_layer == 2 )
	ui_selected_trg_layer = (ui_selected_trg_layer == 2) ? 3 : 2;
      else
	ui_selected_trg_layer = trg_layer;
    } else {
      // >4 layers or drum mode: LayerA/B button selects directly, Layer C button enters trigger selection page
      // also used for drum tracks
      if( trg_layer <= 1 ) {
	if( depressed ) return -1; // ignore when button depressed
	seq_ui_button_state.TRG_LAYER_SEL = 0;
	ui_selected_trg_layer = trg_layer;

	if( ui_page == SEQ_UI_PAGE_TRGSEL )
	  SEQ_UI_PageSet(ui_trglayer_prev_page);
      } else {
	return SEQ_UI_Button_TrgLayerSel(depressed);
      }
    }
  }

  return 0; // no error
}


static s32 SEQ_UI_Button_InsSel(s32 depressed)
{
  // static seq_ui_page_t prev_page = SEQ_UI_PAGE_NONE;
  // also used by seq_ui_insel.c

  static seq_ui_sel_view_t prev_sel_view = SEQ_UI_SEL_VIEW_NONE;

  if( !depressed ) {
    prev_sel_view = seq_ui_sel_view;
    seq_ui_sel_view = SEQ_UI_SEL_VIEW_INS;
    seq_ui_button_state.TAKE_OVER_SEL_VIEW = 1;

    if( ui_page == SEQ_UI_PAGE_MUTE || ui_page == SEQ_UI_PAGE_PATTERN || ui_page == SEQ_UI_PAGE_SONG )
      SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT); // this selection only makes sense in EDIT page
  } else {
    // Release. A clean re-tap while ALREADY in INS sel-view (no B-row pressed during
    // the hold -> TAKE_OVER still set) toggles the B-row between instrument-select
    // and the live play-surface (keyboard / drum pads). Holding INSTR + tapping a
    // B-row key (the drum silent-retarget gesture) clears TAKE_OVER, so it never
    // toggles; the first tap that ENTERS the view (prev != INS) just enters.
    if( seq_ui_button_state.TAKE_OVER_SEL_VIEW && prev_sel_view == SEQ_UI_SEL_VIEW_INS ) {
      seq_ui_options.INSSEL_DRUM_TRIGGER ^= 1;
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "INSTR row:",
		 seq_ui_options.INSSEL_DRUM_TRIGGER ? "Play surface (keys/pads)" : "Select instrument");
    }
    if( !seq_ui_button_state.TAKE_OVER_SEL_VIEW )
      seq_ui_sel_view = prev_sel_view;
  }

  if( seq_hwcfg_button_beh.ins_sel ) {
    if( depressed ) return -1; // ignore when button depressed
    if( !seq_ui_button_state.INS_SEL ) // due to page change: button going to be set, clear other toggle buttons
      seq_ui_button_state.PAGE_CHANGE_BUTTON_FLAGS = 0;
    seq_ui_button_state.INS_SEL ^= 1; // toggle TRGSEL status (will also be released once GP button has been pressed)
  } else {
    seq_ui_button_state.INS_SEL = depressed ? 0 : 1;
  }

  if( !seq_hwcfg_button_beh.simplified_antilog_frontpanel ) {
    if( seq_ui_button_state.INS_SEL ) {
      if( ui_page != SEQ_UI_PAGE_INSSEL ) {
        ui_inssel_prev_page = ui_page;
        SEQ_UI_PageSet(SEQ_UI_PAGE_INSSEL);
      }
    } else {
      if( ui_page == SEQ_UI_PAGE_INSSEL )
        SEQ_UI_PageSet(ui_inssel_prev_page);
    }
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_Mixer(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_MIXER);

  return 0; // no error
}

static s32 SEQ_UI_Button_Save(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  u8 group = ui_selected_group;
  seq_pattern_t pattern = seq_pattern[group];
  s32 status;
  if( (status=SEQ_PATTERN_Save(group, pattern)) < 0 ) {
    SEQ_UI_SDCardErrMsg(2000, status);
  } else {
    char str1[21];
    char str2[21];
    sprintf(str1, "Track Group G%d", group + 1);
    sprintf(str2, "stored into %d:%c%d", pattern.bank+1, (pattern.lower ? 'a' : 'A') + pattern.group, pattern.num + 1);
    SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, str1, str2);
  }

  return 0; // no error
}

static s32 SEQ_UI_Button_SaveAll(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Complete Session", "stored!");
  seq_ui_saveall_req = 1;

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackMode(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKMODE);

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackGroove(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKGRV);

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackLength(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKLEN);

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackDirection(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKDIR);

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackMorph(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKMORPH);

  return 0; // no error
}

static s32 SEQ_UI_Button_TrackTranspose(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  SEQ_UI_PageSet(SEQ_UI_PAGE_TRKTRAN);

  return 0; // no error
}

static s32 SEQ_UI_Button_Fx(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  // sibling toggle: when already on one of the deep FX-area pages,
  // flip directly to its sibling instead of re-opening the submenu.
  if( ui_page == SEQ_UI_PAGE_FX_ROBOTIZE )
    SEQ_UI_PageSet(SEQ_UI_PAGE_ROBOLOOP);
  else if( ui_page == SEQ_UI_PAGE_ROBOLOOP )
    SEQ_UI_PageSet(SEQ_UI_PAGE_FX_ROBOTIZE);
  else
    SEQ_UI_PageSet(SEQ_UI_PAGE_FX);

  return 0; // no error
}

static s32 SEQ_UI_Button_MuteAllTracks(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed
  seq_core_trk_muted = 0xffff;
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "All Tracks", "muted");
  return 0; // no error
}

static s32 SEQ_UI_Button_MuteTrackLayers(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  seq_core_trk[visible_track].layer_muted = 0xffff;
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "All Layers", "of current Track muted");
  return 0; // no error
}

static s32 SEQ_UI_Button_MuteAllTracksAndLayers(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed
  int track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track)
    seq_core_trk[track].layer_muted = 0xffff;
  seq_core_trk_muted = 0xffff;
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "All Layers", "and Tracks muted");
  return 0; // no error
}

static s32 SEQ_UI_Button_UnMuteAllTracks(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed
  seq_core_trk_muted = 0x0000;
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "All Tracks", "unmuted");
  return 0; // no error
}

static s32 SEQ_UI_Button_UnMuteTrackLayers(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  seq_core_trk[visible_track].layer_muted = 0x0000;
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "All Layers", "of current Track unmuted");
  return 0; // no error
}

static s32 SEQ_UI_Button_UnMuteAllTracksAndLayers(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed
  int track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track)
    seq_core_trk[track].layer_muted = 0x0000;
  seq_core_trk_muted = 0x0000;
  SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "All Layers", "and Tracks unmuted");
  return 0; // no error
}

// only used by keyboard remote function
static s32 SEQ_UI_Button_ToggleGate(s32 depressed)
{
  if( depressed ) return -1; // ignore when button depressed

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 gate = SEQ_TRG_GateGet(visible_track, ui_selected_step, ui_selected_instrument) ? 0 : 1;
  SEQ_TRG_GateSet(visible_track, ui_selected_step, ui_selected_instrument, gate);

  return 0; // no error
}

static s32 SEQ_UI_Button_FootSwitch(s32 depressed)
{
  // static variables (only used here, therefore local)
  static u32 fs_time_control = 0; // timestamp of last operation

  // this is used as a constant value
  u32 fs_time_delay = 500; // mS - should this be configurable?

  // Clear track check
  if( depressed ) {
    // if footswitch time passed between pressed and depressed is less than fs_time_delay miliseconds, clear track.
    if( ( MIOS32_TIMESTAMP_GetDelay(fs_time_control) < fs_time_delay ) && ( fs_time_control != 0 ) ) {
      SEQ_UI_UTIL_ClearButton(0); // button pressed
      SEQ_UI_UTIL_ClearButton(1); // button depressed
      if( seq_ui_button_state.SELECT_PRESSED )
	SEQ_UI_Msg_Layer("cleared");
      else
	SEQ_UI_Msg_Track("cleared");
    }
  } else {
    // store pressed timestamp
    fs_time_control = MIOS32_TIMESTAMP_Get();
  }

  // PUNCH_IN, PUNCH_OUT
  if( depressed ) {
    // disable recording
    SEQ_RECORD_Enable(0, 1);
  } else {
    // enable recording
    SEQ_RECORD_Enable(1, 1);
    seq_record_state.ARMED_TRACKS = ui_selected_tracks; // not used yet, just preparation for future changes
  }

  return 0; // no error
}


static s32 SEQ_UI_Button_EncBtnFwd(s32 depressed)
{
  seq_ui_button_state.ENC_BTN_FWD_PRESSED = depressed == 0;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Button handler
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Button_Handler(u32 pin, u32 pin_value)
{
  int i;

  // send MIDI event in remote mode and exit
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return SEQ_MIDI_SYSEX_REMOTE_Client_SendButton(pin, pin_value);

  // ignore as long as hardware config hasn't been read
  if( !SEQ_FILE_HW_ConfigLocked() )
    return -1;

  // ignore during a backup or format is created
  if( seq_ui_backup_req || seq_ui_format_req )
    return -1;

  // ensure that selections are matching with track constraints
  SEQ_UI_CheckSelections();

  // request display update
  seq_ui_display_update_req = 1;

  // stop current message if (new) button has been pressed
  if( pin_value == 0 )
    SEQ_UI_MsgStop();


  // MEMO: we could also use a jump table with references to the functions
  // here, but this "spagetthi code" simplifies the configuration and
  // the resulting ASM doesn't look that bad!

  for(i=0; i<SEQ_HWCFG_NUM_GP; ++i)
    if( pin == seq_hwcfg_button.gp[i] )
      return SEQ_UI_Button_GP(pin_value, i);

  for(i=0; i<SEQ_HWCFG_NUM_TRACK; ++i)
    if( pin == seq_hwcfg_button.track[i] )
      return SEQ_UI_Button_Track(pin_value, i);

  for(i=0; i<SEQ_HWCFG_NUM_DIRECT_TRACK; ++i)
    if( pin == seq_hwcfg_button.direct_track[i] )
      return SEQ_UI_Button_DirectTrack(pin_value, i);

  for(i=0; i<SEQ_HWCFG_NUM_DIRECT_BOOKMARK; ++i)
    if( pin == seq_hwcfg_button.direct_bookmark[i] )
      return SEQ_UI_Button_DirectBookmark(pin_value, i);

  for(i=0; i<SEQ_HWCFG_NUM_ENCODERS; ++i)
    if( pin == seq_hwcfg_button.enc[i] )
      return SEQ_UI_Button_Enc(pin_value, i);

  if( pin == seq_hwcfg_button.track_sel )
    return SEQ_UI_Button_TrackSel(pin_value);

  for(i=0; i<SEQ_HWCFG_NUM_GROUP; ++i)
    if( pin == seq_hwcfg_button.group[i] )
      return SEQ_UI_Button_Group(pin_value, i);

  for(i=0; i<SEQ_HWCFG_NUM_PAR_LAYER; ++i)
    if( pin == seq_hwcfg_button.par_layer[i] )
      return SEQ_UI_Button_ParLayer(pin_value, i);
  if( pin == seq_hwcfg_button.par_layer_sel )
    return SEQ_UI_Button_ParLayerSel(pin_value);

  for(i=0; i<SEQ_HWCFG_NUM_TRG_LAYER; ++i)
    if( pin == seq_hwcfg_button.trg_layer[i] )
      return SEQ_UI_Button_TrgLayer(pin_value, i);
  if( pin == seq_hwcfg_button.trg_layer_sel )
    return SEQ_UI_Button_TrgLayerSel(pin_value);
  if( pin == seq_hwcfg_button.ins_sel )
    return SEQ_UI_Button_InsSel(pin_value);

  if( pin == seq_hwcfg_button.left )
    return SEQ_UI_Button_Left(pin_value);
  if( pin == seq_hwcfg_button.right )
    return SEQ_UI_Button_Right(pin_value);
  if( pin == seq_hwcfg_button.down )
    return SEQ_UI_Button_Down(pin_value);
  if( pin == seq_hwcfg_button.up )
    return SEQ_UI_Button_Up(pin_value);

  if( pin == seq_hwcfg_button.scrub )
    return SEQ_UI_Button_Scrub(pin_value);
  if( pin == seq_hwcfg_button.metronome )
    return SEQ_UI_Button_Freeze(pin_value);

  if( pin == seq_hwcfg_button.record )
    return SEQ_UI_Button_Record(pin_value);
  if( pin == seq_hwcfg_button.jam_live )
    return SEQ_UI_Button_JamLive(pin_value);
  if( pin == seq_hwcfg_button.jam_step )
    return SEQ_UI_Button_JamStep(pin_value);
  if( pin == seq_hwcfg_button.live )
    return SEQ_UI_Button_Live(pin_value);

  if( pin == seq_hwcfg_button.stop )
    return SEQ_UI_Button_Stop(pin_value);
  if( pin == seq_hwcfg_button.pause )
    return SEQ_UI_Button_Pause(pin_value);
  if( pin == seq_hwcfg_button.play )
    return SEQ_UI_Button_Play(pin_value);
  if( pin == seq_hwcfg_button.rew )
    return SEQ_UI_Button_Rew(pin_value);
  if( pin == seq_hwcfg_button.fwd )
    return SEQ_UI_Button_Fwd(pin_value);
  if( pin == seq_hwcfg_button.loop )
    return SEQ_UI_Button_Loop(pin_value);
  if( pin == seq_hwcfg_button.follow )
    return SEQ_UI_Button_Follow(pin_value);

  if( pin == seq_hwcfg_button.utility )
    return SEQ_UI_Button_Utility(pin_value);
  if( pin == seq_hwcfg_button.copy )
    return SEQ_UI_Button_Copy(pin_value);
  if( pin == seq_hwcfg_button.paste )
    return SEQ_UI_Button_Paste(pin_value);
  if( pin == seq_hwcfg_button.clear )
    return SEQ_UI_Button_Clear(pin_value);
  if( pin == seq_hwcfg_button.undo )
    return SEQ_UI_Button_Undo(pin_value);
  if( pin == seq_hwcfg_button.move )
    return SEQ_UI_Button_Move(pin_value);
  if( pin == seq_hwcfg_button.scroll )
    return SEQ_UI_Button_Scroll(pin_value);

  if( pin == seq_hwcfg_button.menu )
    return SEQ_UI_Button_Menu(pin_value);
  if( pin == seq_hwcfg_button.bookmark )
    return SEQ_UI_Button_Bookmark(pin_value);
  if( pin == seq_hwcfg_button.select )
    return SEQ_UI_Button_Select(pin_value);
  if( pin == seq_hwcfg_button.exit )
    return SEQ_UI_Button_Exit(pin_value);

  if( pin == seq_hwcfg_button.tap_tempo )
    return SEQ_UI_Button_TapTempo(pin_value);
  if( pin == seq_hwcfg_button.tempo_preset )
    return SEQ_UI_Button_TempoPreset(pin_value);
  if( pin == seq_hwcfg_button.ext_restart )
    return SEQ_UI_Button_ExtRestart(pin_value);

  if( pin == seq_hwcfg_button.edit )
    return SEQ_UI_Button_Edit(pin_value);
  if( pin == seq_hwcfg_button.mute )
    return SEQ_UI_Button_Mute(pin_value);
  if( pin == seq_hwcfg_button.pattern )
    return SEQ_UI_Button_Pattern(pin_value);
  if( pin == seq_hwcfg_button.song )
    return SEQ_UI_Button_Song(pin_value);
  if( pin == seq_hwcfg_button.phrase )
    return SEQ_UI_Button_Phrase(pin_value);

  if( pin == seq_hwcfg_button.solo )
    return SEQ_UI_Button_Solo(pin_value);
  if( pin == seq_hwcfg_button.fast )
    return SEQ_UI_Button_Fast(pin_value);
  if( pin == seq_hwcfg_button.fast2 )
    return SEQ_UI_Button_Fast2(pin_value);
  if( pin == seq_hwcfg_button.all )
    return SEQ_UI_Button_All(pin_value);

  if( pin == seq_hwcfg_button.step_view )
    return SEQ_UI_Button_StepView(pin_value);

  if( pin == seq_hwcfg_button.mixer )
    return SEQ_UI_Button_Mixer(pin_value);

  if( pin == seq_hwcfg_button.save )
    return SEQ_UI_Button_Save(pin_value);
  if( pin == seq_hwcfg_button.save_all )
    return SEQ_UI_Button_SaveAll(pin_value);

  if( pin == seq_hwcfg_button.track_mode )
    return SEQ_UI_Button_TrackMode(pin_value);
  if( pin == seq_hwcfg_button.track_groove )
    return SEQ_UI_Button_TrackGroove(pin_value);
  if( pin == seq_hwcfg_button.track_length )
    return SEQ_UI_Button_TrackLength(pin_value);
  if( pin == seq_hwcfg_button.track_direction )
    return SEQ_UI_Button_TrackDirection(pin_value);
  if( pin == seq_hwcfg_button.track_morph )
    return SEQ_UI_Button_TrackMorph(pin_value);
  if( pin == seq_hwcfg_button.track_transpose )
    return SEQ_UI_Button_TrackTranspose(pin_value);
  if( pin == seq_hwcfg_button.fx )
    return SEQ_UI_Button_Fx(pin_value);
  if( pin == seq_hwcfg_button.footswitch )
    return SEQ_UI_Button_FootSwitch(pin_value);
  if( pin == seq_hwcfg_button.enc_btn_fwd )
    return SEQ_UI_Button_EncBtnFwd(pin_value);
  if( pin == seq_hwcfg_button.pattern_remix )
    return SEQ_UI_Button_Pattern_Remix(pin_value);

  if( pin == seq_hwcfg_button.mute_all_tracks )
    return SEQ_UI_Button_MuteAllTracks(pin_value);
  if( pin == seq_hwcfg_button.mute_track_layers )
    return SEQ_UI_Button_MuteTrackLayers(pin_value);
  if( pin == seq_hwcfg_button.mute_all_tracks_and_layers )
    return SEQ_UI_Button_MuteAllTracksAndLayers(pin_value);
  if( pin == seq_hwcfg_button.unmute_all_tracks )
    return SEQ_UI_Button_UnMuteAllTracks(pin_value);
  if( pin == seq_hwcfg_button.unmute_track_layers )
    return SEQ_UI_Button_UnMuteTrackLayers(pin_value);
  if( pin == seq_hwcfg_button.unmute_all_tracks_and_layers )
    return SEQ_UI_Button_UnMuteAllTracksAndLayers(pin_value);

  // always print debugging message
#if 1
  MUTEX_MIDIOUT_TAKE;
  if( pin < 32*8 ) {
    DEBUG_MSG("[SEQ_UI_Button_Handler] Button SR:%d, Pin:D%d not mapped, it has been %s.\n", 
	      (pin / 8) + 1,
	      pin % 8,
	      pin_value ? "depressed" : "pressed");
  } else {
    DEBUG_MSG("[SEQ_UI_Button_Handler] Button SR:M%d%c, Pin:D%d not mapped, it has been %s.\n", 
	      1 + (((pin-32*8) / 8) % 8),
	      'A' + ((pin-32*8) / (8*8)),
	      pin % 8,
	      pin_value ? "depressed" : "pressed");
  }
  MUTEX_MIDIOUT_GIVE;
#endif

  return -1; // button not mapped
}


/////////////////////////////////////////////////////////////////////////////
// BLM Button handler
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_BLM_Button_Handler(u32 row, u32 pin, u32 pin_value)
{
  // send MIDI event in remote mode and exit
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return SEQ_MIDI_SYSEX_REMOTE_Client_Send_BLM_Button(row, pin, pin_value);

  // ignore as long as hardware config hasn't been read
  if( !SEQ_FILE_HW_ConfigLocked() )
    return -1;

  // ignore during a backup or format is created
  if( seq_ui_backup_req || seq_ui_format_req )
    return -1;

  if( row >= SEQ_CORE_NUM_TRACKS_PER_GROUP )
    return -1; // more than 4 tracks not supported (yet) - could be done in this function w/o much effort

  if( pin >= 16 )
    return -1; // more than 16 step buttons not supported (yet) - could be done by selecting the step view

  // select track depending on row
  ui_selected_tracks = 1 << (row + 4*ui_selected_group);

  // ensure that selections are matching with track constraints
  SEQ_UI_CheckSelections();

  // request display update
  seq_ui_display_update_req = 1;

  // emulate general purpose button
  if( seq_hwcfg_blm.buttons_no_ui ) {
    s32 status = SEQ_UI_EDIT_Button_Handler(pin, pin_value);
    ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;
    return status;
  }

  return SEQ_UI_Button_GP(pin_value, pin); // no error, pin_value and pin are swapped for this function due to consistency reasons
}


/////////////////////////////////////////////////////////////////////////////
// Encoder handler
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Encoder_Handler(u32 encoder, s32 incrementer)
{
  // send MIDI event in remote mode and exit
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return SEQ_MIDI_SYSEX_REMOTE_Client_SendEncoder(encoder, incrementer);

  // ignore as long as hardware config hasn't been read
  if( !SEQ_FILE_HW_ConfigLocked() )
    return -1;

  // ignore during a backup or format is created
  if( seq_ui_backup_req || seq_ui_format_req )
    return -1;

  if( encoder >= SEQ_HWCFG_NUM_ENCODERS )
    return -1; // encoder doesn't exist

  // ensure that selections are matching with track constraints
  SEQ_UI_CheckSelections();

  // stop current message
  SEQ_UI_MsgStop();

  // limit incrementer
  if( incrementer > 3 )
    incrementer = 3;
  else if( incrementer < -3 )
    incrementer = -3;

  // encoder 17 increments BPM
  if( encoder == 17 ) {
    u16 value = (u16)(seq_core_bpm_preset_tempo[seq_core_bpm_preset_num]*10);
    if( SEQ_UI_Var16_Inc(&value, 25, 3000, incrementer) ) { // at 384ppqn, the minimum BPM rate is ca. 2.5
      // set new BPM
      seq_core_bpm_preset_tempo[seq_core_bpm_preset_num] = (float)value/10.0;
      SEQ_CORE_BPM_Update(seq_core_bpm_preset_tempo[seq_core_bpm_preset_num], seq_core_bpm_preset_ramp[seq_core_bpm_preset_num]);
      //store_file_required = 1;
      seq_ui_display_update_req = 1;      
    }
    return 0;
  }

  if( seq_ui_button_state.SCRUB && encoder == 0 ) {
    // if sequencer isn't already running, continue it (don't restart)
    if( !SEQ_BPM_IsRunning() )
      SEQ_BPM_Cont();
    ui_seq_pause = 0; // clear pause mode

    // Scrub sequence back or forth
    portENTER_CRITICAL(); // should be atomic
    SEQ_CORE_Scrub(incrementer);
    portEXIT_CRITICAL();
  } else if( seq_ui_button_state.MENU_PRESSED ) {
    // encoder selects menu page like GP button
    if( encoder >= 1 && encoder <= 16 )
      SEQ_UI_PageSet(SEQ_UI_PAGES_MenuShortcutPageGet(encoder-1));
  } else if( phrase_name_edit ) {
    // PHRASES name editor: GP encoders 1..16 scroll the keypad (datawheel ignored)
    if( encoder >= 1 && encoder <= 16 )
      SEQ_UI_PhraseName_Input((seq_ui_encoder_t)(encoder-1), incrementer);
  } else if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_PHRASE && ui_page == morph_armed_page &&
	     SEQ_PATTERN_PhraseMorphTarget() >= 0 && encoder == 0 ) {
    // POSTURE-MORPH fine throw: the datawheel rides the morph position 0..MAX
    // (0 = pass-through to the live posture, MAX = the armed phrase's posture).
    // The GP row mirrors it as a 16-segment bar; both route through MorphSet.
    // Armed-page-scoped (see the GP-bar intercept) so it doesn't steal the
    // datawheel from EDIT/other pages while PHRASE view is latched on top.
    u8 p = SEQ_PATTERN_PhraseMorphValue();
    if( SEQ_UI_Var8_Inc(&p, 0, PHRASE_MORPH_MAX, incrementer) ) {
      SEQ_PATTERN_PhraseMorphSet(p);
      seq_ui_display_update_req = 1;
    }
  } else if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_PHRASE && encoder == 0 ) {
    // PHRASE view, datawheel (when no morph is armed — the morph branch above
    // takes precedence): ride the global SWITCH-QUANTIZE grid. The phrase surface
    // is the live hub — collect / morph / switch / set the switch-quantize from
    // the one wheel. grid>0 implies synched switching, 0 (Instant) = immediate.
    u8 g = seq_core_options.SWITCH_QUANTIZE_GRID;
    if( SEQ_UI_Var8_Inc(&g, 0, 8, incrementer) ) {
      static const char *const q_name[9] = {
        "Instant", "1/16", "1/8", "1/4 beat", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" };
      seq_core_options.SWITCH_QUANTIZE_GRID = g;
      seq_core_options.SYNCHED_PATTERN_CHANGE = (g > 0) ? 1 : 0;
      SEQ_UI_Msg(SEQ_UI_MSG_USER_R, 1000, "Switch Quant", (char *)q_name[g]);
      seq_ui_display_update_req = 1;
    }
  } else if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_INS && SEQ_UI_INSSEL_KeyboardActive() &&
	     (encoder == 0 || encoder == 1) ) {
    // INS view, melodic keyboard on the B-row: the datawheel scrolls the row ±1
    // semitone and the GP1 encoder sets the isomorphic Jump (SELECT held: the
    // play/record velocity). Handled here (not in the INSSEL page's encoder
    // callback) so they work from ANY page while the keyboard is played on the
    // latched INS sel-view. encoder 0 = datawheel, 1 = GP1.
    if( (encoder == 0) ? SEQ_UI_INSSEL_KeyboardScroll(incrementer)
		       : (seq_ui_button_state.SELECT_PRESSED ? SEQ_UI_INSSEL_KeyboardVelocity(incrementer)
							     : SEQ_UI_INSSEL_KeyboardJump(incrementer)) )
      seq_ui_display_update_req = 1;
  } else if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_PROC ) {
    // PROC view: the GP encoders operate the focused processor (invariant 3),
    // from ANY page while the rack is latched on the B-row. The focused slot's
    // param list drives the mapping: GP1..GPn = params 0..n-1. The DATAWHEEL
    // switches TRACKS (2026-07-13 — it previously duplicated GP1's headline
    // ride): the B-row is claimed by the rack, so the wheel is how you walk the
    // matrix — same row, same plane, next track; the GxTy block top-right is the
    // feedback. Every dial write lands on SEQ_CC_Set (or the param's setter) so
    // the slot re-syncs and the track re-renders. Encoders past the param count
    // are swallowed (no page fall-through).
    if( encoder == 0 ) {
      SEQ_UI_GxTyInc(incrementer);
    } else {
      u8 track = SEQ_UI_VisibleTrackGet();
      const proc_param_t *params; u8 nparams;
      SEQ_UI_PROC_SlotParams(ui_focused_proc_slot, &params, &nparams);
      s32 idx = (s32)encoder - 1;
      if( idx >= 0 && idx < (s32)nparams )
        SEQ_UI_PROC_ParamInc(track, &params[idx], incrementer);
    }
  } else if( ui_encoder_callback != NULL ) {
    if( seq_ui_button_state.ENC_BTN_FWD_PRESSED ) {
      // encoder emulates a GP button
      if( ui_button_callback ) {
	ui_button_callback(encoder-1, 0);
      }
      if( ui_button_callback ) { // previous call could remove the callback!
	ui_button_callback(encoder-1, 1);
      }
      ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0; // ensure that value is visible when it has been changed
    } else {
      // common handling
      ui_encoder_callback((encoder == 0) ? SEQ_UI_ENCODER_Datawheel : (encoder-1), incrementer);
      ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0; // ensure that value is visible when it has been changed
    }
  }

  // request display update
  seq_ui_display_update_req = 1;

  return 0; // no error
}



/////////////////////////////////////////////////////////////////////////////
// Receives a MIDI package from APP_NotifyReceivedEvent (-> app.c)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_REMOTE_MIDI_Receive(mios32_midi_port_t port, mios32_midi_package_t midi_package)
{
#if 1
  // check for active remote mode
  if( seq_midi_sysex_remote_active_mode != SEQ_MIDI_SYSEX_REMOTE_MODE_SERVER )
    return 0; // no error
#endif

  if( (seq_midi_sysex_remote_port == DEFAULT && seq_midi_sysex_remote_active_port != port) ||
      (seq_midi_sysex_remote_port != DEFAULT && port != seq_midi_sysex_remote_port) )
    return 0; // wrong port

  // for easier parsing: convert Note Off -> Note On with velocity 0
  if( midi_package.event == NoteOff ) {
    midi_package.event = NoteOn;
    midi_package.velocity = 0;
  }

  switch( midi_package.event ) {
    case NoteOn: {
      switch( midi_package.chn ) {
        case Chn1:
	  SEQ_UI_Button_Handler(midi_package.note + 0x00, midi_package.velocity ? 0 : 1);
	  break;
        case Chn2:
	  SEQ_UI_Button_Handler(midi_package.note + 0x80, midi_package.velocity ? 0 : 1);
	  break;
        case Chn3:
	  SEQ_UI_BLM_Button_Handler(midi_package.note >> 5, midi_package.note & 0x1f, midi_package.velocity ? 0 : 1);
	  break;
      }	
    } break;

    case CC: {
      if( midi_package.cc_number >= 15 && midi_package.cc_number <= 31 )
	SEQ_UI_Encoder_Handler(midi_package.cc_number-15, (int)midi_package.value - 0x40);
    } break;
  }

  return 1; // MIDI event has been taken for remote function -> don't forward to router/MIDI event parser
}


/////////////////////////////////////////////////////////////////////////////
// MIDI Remote Keyboard Function (called from SEQ_MIDI_IN)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_REMOTE_MIDI_Keyboard(u8 key, u8 depressed)
{
#if 0
  MIOS32_MIDI_SendDebugMessage("SEQ_MIDI_SYSEX_REMOTE_MIDI_Keyboard(%d, %d)\n", key, depressed);
#endif

  switch( key ) {
    case 0x24: // C-2
      return SEQ_UI_Button_GP(depressed, 0);
    case 0x25: // C#2
      return SEQ_UI_Button_Track(depressed, 0);
    case 0x26: // D-2
      return SEQ_UI_Button_GP(depressed, 1);
    case 0x27: // D#2
      return SEQ_UI_Button_Track(depressed, 1);
    case 0x28: // E-2
      return SEQ_UI_Button_GP(depressed, 2);
    case 0x29: // F-2
      return SEQ_UI_Button_GP(depressed, 3);
    case 0x2a: // F#2
      return SEQ_UI_Button_Track(depressed, 2);
    case 0x2b: // G-2
      return SEQ_UI_Button_GP(depressed, 4);
    case 0x2c: // G#2
      return SEQ_UI_Button_Track(depressed, 3);
    case 0x2d: // A-2
      return SEQ_UI_Button_GP(depressed, 5);
    case 0x2e: // A#2
      return SEQ_UI_Button_ParLayer(depressed, 0);
    case 0x2f: // B-2
      return SEQ_UI_Button_GP(depressed, 6);

    case 0x30: // C-3
      return SEQ_UI_Button_GP(depressed, 7);
    case 0x31: // C#3
      return SEQ_UI_Button_ParLayer(depressed, 1);
    case 0x32: // D-3
      return SEQ_UI_Button_GP(depressed, 8);
    case 0x33: // D#3
      return SEQ_UI_Button_ParLayer(depressed, 2);
    case 0x34: // E-3
      return SEQ_UI_Button_GP(depressed, 9);
    case 0x35: // F-3
      return SEQ_UI_Button_GP(depressed, 10);
    case 0x36: // F#3
      return SEQ_UI_Button_TrgLayer(depressed, 0);
    case 0x37: // G-3
      return SEQ_UI_Button_GP(depressed, 11);
    case 0x38: // G#3
      return SEQ_UI_Button_TrgLayer(depressed, 1);
    case 0x39: // A-3
      return SEQ_UI_Button_GP(depressed, 12);
    case 0x3a: // A#3
      return SEQ_UI_Button_TrgLayer(depressed, 2);
    case 0x3b: // B-3
      return SEQ_UI_Button_GP(depressed, 13);
  
    case 0x3c: // C-4
      return SEQ_UI_Button_GP(depressed, 14);
    case 0x3d: // C#4
      return SEQ_UI_Button_Group(depressed, 0);
    case 0x3e: // D-4
      return SEQ_UI_Button_GP(depressed, 15);
    case 0x3f: // D#4
      return SEQ_UI_Button_Group(depressed, 1);
    case 0x40: // E-4
      return 0; // ignore
    case 0x41: // F-4
      return SEQ_UI_Button_StepView(depressed);
    case 0x42: // F#4
      return SEQ_UI_Button_Group(depressed, 2);
    case 0x43: // G-4
      return 0; // ignore
    case 0x44: // G#4
      return SEQ_UI_Button_Group(depressed, 3);
    case 0x45: // A-4
      return SEQ_UI_Button_Left(depressed);
    case 0x46: // A#4
      return SEQ_UI_Button_ToggleGate(depressed);
    case 0x47: // B-4
      return SEQ_UI_Button_Right(depressed);
  
    case 0x48: // C-5
      return SEQ_UI_Button_Edit(depressed);
    case 0x49: // C#5
      return SEQ_UI_Button_Solo(depressed);
    case 0x4a: // D-5
      return SEQ_UI_Button_Mute(depressed);
    case 0x4b: // D#5
      return SEQ_UI_Button_All(depressed);
    case 0x4c: // E-5
      return SEQ_UI_Button_Pattern(depressed);
    case 0x4d: // F-5
      return SEQ_UI_Button_Song(depressed);
    case 0x4e: // F#5
      return SEQ_UI_Button_Fast(depressed);
    case 0x4f: // G-5
      return SEQ_UI_Button_Freeze(depressed);
    case 0x50: // G#5
      return SEQ_UI_Button_ExtRestart(depressed);
    case 0x51: // A-5
      return SEQ_UI_Button_ParLayerSel(depressed);
    case 0x52: // A#5
      return SEQ_UI_Button_TrackSel(depressed);
    case 0x53: // B-5
      return SEQ_UI_Button_Stop(depressed);
  
    case 0x54: // C-6
      return SEQ_UI_Button_Play(depressed);
    case 0x55: // C#6
      return SEQ_UI_Button_Pause(depressed);
    case 0x56: // D-6
      return SEQ_UI_Button_Rew(depressed);
    case 0x57: // D#6
      return SEQ_UI_Button_Fwd(depressed);
    case 0x58: // E-6
      return SEQ_UI_Button_Utility(depressed);
    case 0x59: // F-6
      return SEQ_UI_Button_TempoPreset(depressed);
    case 0x5a: // F#6
      return 0; // ignore
    case 0x5b: // G-6
      return SEQ_UI_Button_Menu(depressed);
    case 0x5c: // G#6
      return SEQ_UI_Button_Select(depressed);
    case 0x5d: // A-6
      return SEQ_UI_Button_Exit(depressed);
    case 0x5e: // A#6
      return SEQ_UI_Button_Down(depressed);
    case 0x5f: // B-6
      return SEQ_UI_Button_Up(depressed);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Update LCD messages
// Usually called from background task
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_LCD_Handler(void)
{
  static u8 boot_animation_wait_ctr = 0;
  static u8 boot_animation_lcd_pos = 0;
  static u8 screen_saver_was_active = 0;

  // special handling in remote client mode
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return SEQ_UI_LCD_Update();

  if( seq_ui_display_init_req ) {
    seq_ui_display_init_req = 0; // clear request

    // clear force update of LCD
    SEQ_LCD_Clear();
    SEQ_LCD_Update(1);

    // select first menu item
    ui_selected_item = 0;

    // call init function of current page
    SEQ_UI_PAGES_CallInit(ui_page);

    // request display update
    seq_ui_display_update_req = 1;
  }

  // print boot screen as long as hardware config hasn't been read or screensaver is active
  if( !SEQ_FILE_HW_ConfigLocked() ) {
    if( boot_animation_lcd_pos < (40-3) ) {
      if( ++boot_animation_wait_ctr >= 75 ) {
	boot_animation_wait_ctr = 0;

	if( boot_animation_lcd_pos == 0 ) {
	  SEQ_LCD_Clear();
	  SEQ_LCD_CursorSet(0, 0);
	  SEQ_LCD_PrintString(MIOS32_LCD_BOOT_MSG_LINE1 " " MIOS32_LCD_BOOT_MSG_LINE2);
	  SEQ_LCD_CursorSet(0, 1);
	  SEQ_LCD_PrintString("Searching for SD Card...");
	}
	
	// logo is print on second LCD
	SEQ_LCD_LOGO_Print(40, boot_animation_lcd_pos++);
      }
    }
  } else if( seq_ui_backup_req || seq_ui_format_req ) {
    SEQ_LCD_Clear();
    SEQ_LCD_CursorSet(0, 0);
    //                     <-------------------------------------->
    //                     0123456789012345678901234567890123456789
    if( seq_ui_backup_req )
      SEQ_LCD_PrintString("Copy Files - please wait!!!");
    else if( seq_ui_format_req )
      SEQ_LCD_PrintString("Creating Files - please wait!!!");
    else
      SEQ_LCD_PrintString("Don't know what I'm doing! :-/");

    if( seq_file_backup_notification != NULL ) {
      int i;

      SEQ_LCD_CursorSet(0, 1);
      SEQ_LCD_PrintFormattedString("Creating %s", seq_file_backup_notification);

      SEQ_LCD_CursorSet(40+3, 0);
      SEQ_LCD_PrintString("Total: [");
      for(i=0; i<20; ++i)
	SEQ_LCD_PrintChar((i>(seq_file_backup_percentage/5)) ? ' ' : '#');
      SEQ_LCD_PrintFormattedString("] %3d%%", seq_file_backup_percentage);

      SEQ_LCD_CursorSet(40+3, 1);
      SEQ_LCD_PrintString("File:  [");
      for(i=0; i<20; ++i)
	SEQ_LCD_PrintChar((i>(file_copy_percentage/5)) ? ' ' : '#');
      SEQ_LCD_PrintFormattedString("] %3d%%", file_copy_percentage);
    }

  } else if( SEQ_LCD_LOGO_ScreenSaver_IsActive() ) {
    screen_saver_was_active = 1;
    SEQ_LCD_LOGO_ScreenSaver_Print();
  } else if( seq_ui_button_state.MENU_PRESSED && !seq_ui_button_state.MENU_FIRST_PAGE_SELECTED ) {
    SEQ_LCD_CursorSet(0, 0);
    //                   <-------------------------------------->
    //                   0123456789012345678901234567890123456789
    SEQ_LCD_PrintString("Menu Shortcuts:");
    SEQ_LCD_PrintSpaces(25 + 40);
    SEQ_LCD_CursorSet(0, 1);

    int i;
    for(i=0; i<16; ++i) {
      SEQ_LCD_PrintString(SEQ_UI_PAGES_MenuShortcutNameGet(i));
    }
  } else if( seq_ui_button_state.PATTERN_PRESSED ) {
    // Capture gesture overlay. Source = visible track. GP1-8 = destination group,
    // GP9-16 = destination pattern (commits, persisted), lower select row = the
    // destination track within the slot (default = source's track). Group
    // defaults to current.
    u8 src_track = SEQ_UI_VisibleTrackGet();
    u8 src_group = src_track / SEQ_CORE_NUM_TRACKS_PER_GROUP;
    u8 cur_letter = seq_pattern[src_group].group;
    u8 eff_dst_track = (pattern_capture_dst_track != 0xff) ? pattern_capture_dst_track : src_track;

    // LCD 1 row 0: title — source track -> effective destination track.
    SEQ_LCD_CursorSet(0, 0);
    //                                "1234567890123456789012345678901234567890"
    SEQ_LCD_PrintFormattedString("CAPTURE T%-2d -> T%-2d (sel=dest trk)     ",
                                 src_track + 1, eff_dst_track + 1);

    // LCD 1 row 1: destination group letters A..H (GP1-8). Picked group
    // bracketed; current playing letter shows a dot prefix.
    SEQ_LCD_CursorSet(0, 1);
    {
      int i;
      for(i=0; i<8; ++i) {
        char letter = 'A' + i;
        char prefix = (i == cur_letter) ? '.' : ' ';
        if( i == pattern_capture_group )
          SEQ_LCD_PrintFormattedString("%c[%c] ", prefix, letter);
        else
          SEQ_LCD_PrintFormattedString("%c %c  ", prefix, letter);
      }
    }

    // LCD 2 row 0: after a commit, show the result (the SEQ_UI_Msg popup is
    // masked by this overlay while PATTERN is held); otherwise the layout hint.
    SEQ_LCD_CursorSet(40, 0);
    if( pattern_capture_status[0] )
      SEQ_LCD_PrintFormattedString("%-40s", pattern_capture_status);
    else
      SEQ_LCD_PrintString("GP1-8 grp GP9-16 pat  sel=trk           ");

    // LCD 2 row 1: destination pattern numbers 1..8 (GP9-16). The group's
    // currently-loaded pattern number is dotted as a reference.
    SEQ_LCD_CursorSet(40, 1);
    {
      u8 cur_num = seq_pattern[src_group].num;
      int i;
      for(i=0; i<8; ++i) {
        char prefix = (i == cur_num) ? '.' : ' ';
        SEQ_LCD_PrintFormattedString("%c%d   ", prefix, i + 1);
      }
    }
  } else if( capture_util_held ) {
    // Retroactive CAPTURE overlay (north-star play-then-keep). Source = the ring's
    // recording track; the select row picks the destination; GP-n grabs the last n
    // LOOPS of src (n = GP index) and commits. A "loop" = the src track's full length
    // (one global measure for a whole-measure track, N for an N-measure track). The GP
    // LED row is a depth thermometer in loops.
    u8 cap_src = SEQ_CORE_CaptureRingTrack();
    u8 cap_max_k = SEQ_CORE_CaptureMaxK(cap_src); // par/trg-aware: lit LEDs == grabbable
    char cap_buf[41];

    if( cap_src >= SEQ_CORE_NUM_TRACKS )
      sprintf(cap_buf, "CAPTURE: no ring (play, then STOP)");
    else if( capture_dst_track != 0xff )
      sprintf(cap_buf, "CAPTURE T%d -> T%d   max %d loops", cap_src + 1, capture_dst_track + 1, cap_max_k);
    else
      sprintf(cap_buf, "CAPTURE T%d -> ?   max %d loops", cap_src + 1, cap_max_k);
    SEQ_LCD_CursorSet(0, 0);
    SEQ_LCD_PrintFormattedString("%-40s", cap_buf);

    SEQ_LCD_CursorSet(0, 1);
    SEQ_LCD_PrintFormattedString("%-40s", "sel=dest trk   GP-n=grab last n loops");

    SEQ_LCD_CursorSet(40, 0);
    if( capture_status[0] )
      SEQ_LCD_PrintFormattedString("%-40s", capture_status);
    else
      SEQ_LCD_PrintFormattedString("%-40s", "STOP first. GP-n=last n loops (max 16)");

    SEQ_LCD_CursorSet(40, 1);
    SEQ_LCD_PrintSpaces(40);
  } else if( pull_held_track != 0xff &&
             (pull_src_column != 0xff || pull_letter != 0xff || pull_status[0]) ) {
    // Pull gesture overlay (mirror of the capture overlay above). Destination =
    // the held select-row track; the top row aims the SOURCE pattern. Gated on
    // the first aim input so a quick track-select tap doesn't flash the page.
    u8 src_col = (pull_src_column != 0xff) ? pull_src_column : pull_held_track;
    u8 src_bank = src_col / SEQ_CORE_NUM_TRACKS_PER_GROUP;
    u8 src_section = src_col % SEQ_CORE_NUM_TRACKS_PER_GROUP;
    u8 cur_letter = seq_pattern[src_bank].group;

    // LCD 1 row 0: title — source column -> the held (destination) track.
    SEQ_LCD_CursorSet(0, 0);
    //                                "1234567890123456789012345678901234567890"
    SEQ_LCD_PrintFormattedString("PULL %d:S%d -> T%-2d  (sel=src col)       ",
                                 src_bank + 1, src_section + 1, pull_held_track + 1);

    // LCD 1 row 1: source pattern letters A..H (GP1-8). Picked letter
    // bracketed; the column group's loaded letter shows a dot prefix.
    SEQ_LCD_CursorSet(0, 1);
    {
      int i;
      for(i=0; i<8; ++i) {
        char letter = 'A' + i;
        char prefix = (i == cur_letter) ? '.' : ' ';
        if( i == pull_letter )
          SEQ_LCD_PrintFormattedString("%c[%c] ", prefix, letter);
        else
          SEQ_LCD_PrintFormattedString("%c %c  ", prefix, letter);
      }
    }

    // LCD 2 row 0: last commit result, else the layout hint.
    SEQ_LCD_CursorSet(40, 0);
    if( pull_status[0] )
      SEQ_LCD_PrintFormattedString("%-40s", pull_status);
    else
      SEQ_LCD_PrintString("GP1-8 ltr GP9-16 pat  sel=src col       ");

    // LCD 2 row 1: source pattern numbers 1..8 (GP9-16). The column group's
    // currently-loaded number is dotted as a reference.
    SEQ_LCD_CursorSet(40, 1);
    {
      u8 cur_num = seq_pattern[src_bank].num;
      int i;
      for(i=0; i<8; ++i) {
        char prefix = (i == cur_num) ? '.' : ' ';
        SEQ_LCD_PrintFormattedString("%c%d   ", prefix, i + 1);
      }
    }
  } else if( phrase_name_edit ) {
    // PHRASES name editor overlay (keypad). Mirrors the SAVE-page label editor,
    // but for a phrase's name. GP/step row = keypad chars, GP16 or EXIT = save.
    u8 n = phrase_name_edit_slot;
    char *buf = SEQ_PATTERN_PhraseName(n);

    SEQ_LCD_CursorSet(0, 0);
    SEQ_LCD_PrintFormattedString("Name phrase %2d  <", n + 1);
    {
      int i;
      if( buf != NULL )
        for(i=0; i<20; ++i)
          SEQ_LCD_PrintChar(buf[i]);
      else
        SEQ_LCD_PrintSpaces(20);
    }
    SEQ_LCD_PrintChar('>');
    SEQ_LCD_PrintSpaces(2); // fill to col 40

    SEQ_LCD_CursorSet(40, 0);
    SEQ_LCD_PrintFormattedString("%-40s", "multi-tap keys; GP16/EXIT = save name");

    // flashing cursor over the edited char (name starts at col 17)
    if( ui_cursor_flash && buf != NULL ) {
      SEQ_LCD_CursorSet(17 + ui_edit_name_cursor, 0);
      SEQ_LCD_PrintChar('*');
    }

    SEQ_UI_KeyPad_LCD_Msg();       // line 1: 69 chars (charsets + Char <> Del Ins)
    SEQ_LCD_PrintString("       SAVE"); // remaining 11 chars (GP15 unused, GP16=SAVE)
  } else {
    // re-init special chars
    if( screen_saver_was_active ) {
      screen_saver_was_active = 0;
      SEQ_LCD_ReInitSpecialChars();
    }

    // perform high priority LCD update request
    if( ui_lcd_callback != NULL )
      ui_lcd_callback(1); // high_prio

    // perform low priority LCD update request if requested
    if( seq_ui_display_update_req ) {
      seq_ui_display_update_req = 0; // clear request

      // ensure that selections are matching with track constraints
      SEQ_UI_CheckSelections();

      if( ui_lcd_callback != NULL )
	ui_lcd_callback(0); // no high_prio
    }
  }

  // transfer all changed characters to LCD
  SEQ_UI_LCD_Update();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Called from SEQ_UI_LCD_Handler(), but optionally also from other tasks
// to update the LCD screen immediately
/////////////////////////////////////////////////////////////////////////////
// for newer GCC versions it's important to declare constant arrays outside a function
//                                             00112233
static const char animation_l_arrows[2*4+1] = "   >>>> ";
//                                             00112233
static const char animation_r_arrows[2*4+1] = "  < << <";
//                                               00112233
static const char animation_l_brackets[2*4+1] = "   )))) ";
//                                               00112233
static const char animation_r_brackets[2*4+1] = "  ( (( (";
//                                            00112233
static const char animation_l_stars[2*4+1] = "   **** ";
//                                            00112233
static const char animation_r_stars[2*4+1] = "  * ** *";
s32 SEQ_UI_LCD_Update(void)
{
  // special handling in remote client mode
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT ) {
    MIOS32_IRQ_Disable();
    u8 force = seq_midi_sysex_remote_force_lcd_update;
    seq_midi_sysex_remote_force_lcd_update = 0;
    MIOS32_IRQ_Enable();
    return SEQ_LCD_Update(force);
  }

  // if UI message active: copy over the text
  if( ui_msg_ctr ) {
    char *animation_l_ptr;
    char *animation_r_ptr;
    u8 msg_x = 0;
    u8 right_aligned = 0;
    u8 disable_message = 0;

    switch( ui_msg_type ) {
      case SEQ_UI_MSG_SDCARD: {
	animation_l_ptr = (char *)animation_l_arrows;
	animation_r_ptr = (char *)animation_r_arrows;
	msg_x = 0; // MEMO: print such important information at first LCD for the case the user hasn't connected the second LCD yet
	right_aligned = 0;
      } break;

      case SEQ_UI_MSG_DELAYED_ACTION:
      case SEQ_UI_MSG_DELAYED_ACTION_R: {
	animation_l_ptr = (char *)animation_l_brackets;
	animation_r_ptr = (char *)animation_r_brackets;
	if( ui_msg_type == SEQ_UI_MSG_DELAYED_ACTION_R ) {
	  msg_x = 40; // right LCD
	  right_aligned = 0;
	} else {
	  msg_x = 39; // left LCD
	  right_aligned = 1;
	}

	if( ui_delayed_action_callback == NULL ) {
	  disable_message = 1; // button has been depressed before delay
	} else {
	  int seconds = (ui_delayed_action_ctr / 1000) + 1;
	  if( seconds == 1 )
	    sprintf(ui_msg[0], "Hold for 1 second ");
	  else
	    sprintf(ui_msg[0], "Hold for %d seconds", seconds);
	}
      } break;

      case SEQ_UI_MSG_USER_R: {
	animation_l_ptr = (char *)animation_l_stars;
	animation_r_ptr = (char *)animation_r_stars;
	msg_x = 40; // right display
	right_aligned = 0;
      } break;

      default: {
	animation_l_ptr = (char *)animation_l_stars;
	animation_r_ptr = (char *)animation_r_stars;
	msg_x = 39;
	right_aligned = 1;
      } break;

    }

    if( !disable_message ) {
      int anum = (ui_msg_ctr % 1000) / 250;

      int len[2];
      len[0] = strlen((char *)ui_msg[0]);
      len[1] = strlen((char *)ui_msg[1]);
      int len_max = len[0];
      if( len[1] > len_max )
	len_max = len[1];

      if( right_aligned )
	msg_x -= (9 + len_max);

      int line;
      for(line=0; line<2; ++line) {
	SEQ_LCD_CursorSet(msg_x, line);

	// ensure that both lines are padded with same number of spaces
	int end_pos = len[line];
	while( end_pos < len_max )
	  ui_msg[line][end_pos++] = ' ';
	ui_msg[line][end_pos] = 0;

	SEQ_LCD_PrintFormattedString(" %c%c| %s |%c%c ",
				     *(animation_l_ptr + 2*anum + 0), *(animation_l_ptr + 2*anum + 1),
				     (char *)ui_msg[line], 
				     *(animation_r_ptr + 2*anum + 0), *(animation_r_ptr + 2*anum + 1));
      }
    }
  }

  // transfer all changed characters to LCD
  // SEQ_LCD_Update provides a MUTEX handling to allow updates from different tasks
  return SEQ_LCD_Update(0);
}


/////////////////////////////////////////////////////////////////////////////
// Update all LEDs
// Usually called from background task
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_LED_Handler(void)
{
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  static u8 remote_led_sr[SEQ_LED_NUM_SR];

  // ignore in remote client mode
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return 0; // no error

  // ignore as long as hardware config hasn't been read
  if( !SEQ_FILE_HW_ConfigLocked() )
    return -1;

  // for special LED handling of Antilog Frontpanel
  u8 selbuttons_available = seq_hwcfg_blm8x8.dout_gp_mapping == 3;

  // ---------------------------------------------------------------------------
  // Fork LED conventions (keep new modes consistent with these):
  //   STEADY  = "you are here / holding this now" — the page you're on, or the key
  //             you're physically holding for a gesture.
  //   FLASH   = "an armed/live MODE is engaged" — e.g. a POSTURE-MORPH is armed
  //             (PHRASE), or the INSSEL play-surface is hot (INSTR). Uses the shared
  //             ui_cursor_flash timebase; ON during the visible phase (!ui_cursor_flash)
  //             so all indicators blink in phase with the GP cursor. A STEADY term
  //             overrides the flash where both apply.
  //   GP ROW  = for the hold-then-paint gesture family (UTILITY / PATTERN / B-row
  //             PULL), the GP row reflects the gesture's LIVE AIM (a depth
  //             thermometer or a letter cursor) — painted in the GP block below.
  // Keep per-pass cost trivial: read RAM/state only, never SD or heavy queries here.
  // ---------------------------------------------------------------------------

  // track LEDs
  // in pattern page: track buttons are used as group buttons
  if( ui_page == SEQ_UI_PAGE_PATTERN ) {
    SEQ_LED_PinSet(seq_hwcfg_led.track[0], (ui_selected_group == 0));
    SEQ_LED_PinSet(seq_hwcfg_led.track[1], (ui_selected_group == 1));
    SEQ_LED_PinSet(seq_hwcfg_led.track[2], (ui_selected_group == 2));
    SEQ_LED_PinSet(seq_hwcfg_led.track[3], (ui_selected_group == 3));
  } else if( ui_page == SEQ_UI_PAGE_SONG ) {
    // in song page: track and group buttons are used to select the cursor position
    SEQ_LED_PinSet(seq_hwcfg_led.track[0], (ui_selected_item == 3));
    SEQ_LED_PinSet(seq_hwcfg_led.track[1], (ui_selected_item == 4));
    SEQ_LED_PinSet(seq_hwcfg_led.track[2], (ui_selected_item == 5));
    SEQ_LED_PinSet(seq_hwcfg_led.track[3], (ui_selected_item == 6));
  } else {
    u8 selected_tracks = ui_selected_tracks >> (4*ui_selected_group);
    SEQ_LED_PinSet(seq_hwcfg_led.track[0], (selected_tracks & (1 << 0)));
    SEQ_LED_PinSet(seq_hwcfg_led.track[1], (selected_tracks & (1 << 1)));
    SEQ_LED_PinSet(seq_hwcfg_led.track[2], (selected_tracks & (1 << 2)));
    SEQ_LED_PinSet(seq_hwcfg_led.track[3], (selected_tracks & (1 << 3)));
  }

  SEQ_LED_PinSet(seq_hwcfg_led.track_sel, ui_page == SEQ_UI_PAGE_TRACKSEL || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_TRACKS));
  
  // parameter layer LEDs
  // in song page: layer buttons are used to select the cursor position
  if( ui_page == SEQ_UI_PAGE_SONG ) {
    SEQ_LED_PinSet(seq_hwcfg_led.par_layer[0], ui_selected_item == 0);
    SEQ_LED_PinSet(seq_hwcfg_led.par_layer[1], ui_selected_item == 1);
    SEQ_LED_PinSet(seq_hwcfg_led.par_layer[2], ui_selected_item == 2);
  } else {
    SEQ_LED_PinSet(seq_hwcfg_led.par_layer[0], (ui_selected_par_layer == 0));
    SEQ_LED_PinSet(seq_hwcfg_led.par_layer[1], (ui_selected_par_layer == 1));
    SEQ_LED_PinSet(seq_hwcfg_led.par_layer[2], (ui_selected_par_layer >= 2) || seq_ui_button_state.PAR_LAYER_SEL);
  }
  SEQ_LED_PinSet(seq_hwcfg_led.par_layer_sel, ui_page == SEQ_UI_PAGE_PARSEL || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_PAR));
  
  // group LEDs
  // in song page: track and group buttons are used to select the cursor position
  if( ui_page == SEQ_UI_PAGE_SONG ) {
    SEQ_LED_PinSet(seq_hwcfg_led.group[0], (ui_selected_item == 3));
    SEQ_LED_PinSet(seq_hwcfg_led.group[1], (ui_selected_item == 4));
    SEQ_LED_PinSet(seq_hwcfg_led.group[2], (ui_selected_item == 5));
    SEQ_LED_PinSet(seq_hwcfg_led.group[3], (ui_selected_item == 6));
  } else {
    SEQ_LED_PinSet(seq_hwcfg_led.group[0], (ui_selected_group == 0));
    SEQ_LED_PinSet(seq_hwcfg_led.group[1], (ui_selected_group == 1));
    SEQ_LED_PinSet(seq_hwcfg_led.group[2], (ui_selected_group == 2));
    SEQ_LED_PinSet(seq_hwcfg_led.group[3], (ui_selected_group == 3));
  }
  
  // trigger layer LEDs
  SEQ_LED_PinSet(seq_hwcfg_led.trg_layer[0], (ui_selected_trg_layer == 0));
  SEQ_LED_PinSet(seq_hwcfg_led.trg_layer[1], (ui_selected_trg_layer == 1));
  SEQ_LED_PinSet(seq_hwcfg_led.trg_layer[2], (ui_selected_trg_layer >= 2) || seq_ui_button_state.TRG_LAYER_SEL);
  SEQ_LED_PinSet(seq_hwcfg_led.trg_layer_sel, ui_page == SEQ_UI_PAGE_TRGSEL || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_TRG));

  // instrument layer LEDs
  // Lit while the INS dimension is active (INSSEL page or INSTR sel-view). The B-row
  // is then either the instrument selector or the live play-surface (keyboard/pads) —
  // FLASH when the play-surface is on so the mode reads at a glance vs. steady =
  // select. Toggle the two with a re-tap of INSTR (SEQ_UI_Button_InsSel).
  {
    u8 ins_view = (ui_page == SEQ_UI_PAGE_INSSEL) || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_INS);
    SEQ_LED_PinSet(seq_hwcfg_led.ins_sel,
		   ins_view && (!seq_ui_options.INSSEL_DRUM_TRIGGER || !ui_cursor_flash));
  }
  
  // remaining LEDs
  SEQ_LED_PinSet(seq_hwcfg_led.edit, ui_page == SEQ_UI_PAGE_EDIT);
  SEQ_LED_PinSet(seq_hwcfg_led.mute, ui_page == SEQ_UI_PAGE_MUTE || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_MUTE));
  // PATTERN: steady on the Pattern page, and also while HELD for the capture-to-slot
  // gesture — so the hold-then-paint family (PATTERN / UTILITY / B-row PULL) all light
  // their held key. The GP helper for this hold is painted in the GP block below.
  SEQ_LED_PinSet(seq_hwcfg_led.pattern, ui_page == SEQ_UI_PAGE_PATTERN || seq_ui_button_state.PATTERN_PRESSED);
  // Repurposed SONG button: its LED now tracks the unified Capture page (fork
  // 2026-06-27). The song page is reached via PHRASE.
  SEQ_LED_PinSet(seq_hwcfg_led.song, ui_page == SEQ_UI_PAGE_CAPTURE);
  // PHRASE: steady while pressed / in PHRASE sel-view / on the song-arrangement page
  // it opens (page consistency with the other page-openers). Additionally FLASH it on
  // ANY page while a POSTURE-MORPH is armed, so a morph riding on the datawheel stays
  // visible even away from the PHRASE view. Steady overrides the flash.
  {
    u8 phrase_steady = seq_ui_button_state.PHRASE_PRESSED
		     || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_PHRASE)
		     || ui_page == SEQ_UI_PAGE_SONG;
    u8 morph_armed = SEQ_PATTERN_PhraseMorphTarget() >= 0;
    SEQ_LED_PinSet(seq_hwcfg_led.phrase, phrase_steady || (morph_armed && !ui_cursor_flash));
  }
  SEQ_LED_PinSet(seq_hwcfg_led.mixer, ui_page == SEQ_UI_PAGE_MIXER);

  SEQ_LED_PinSet(seq_hwcfg_led.track_mode, ui_page == SEQ_UI_PAGE_TRKMODE);
  SEQ_LED_PinSet(seq_hwcfg_led.track_groove, ui_page == SEQ_UI_PAGE_TRKGRV);
  SEQ_LED_PinSet(seq_hwcfg_led.track_length, ui_page == SEQ_UI_PAGE_TRKLEN);
  SEQ_LED_PinSet(seq_hwcfg_led.track_direction, ui_page == SEQ_UI_PAGE_TRKDIR);
  SEQ_LED_PinSet(seq_hwcfg_led.track_morph, ui_page == SEQ_UI_PAGE_TRKMORPH);
  SEQ_LED_PinSet(seq_hwcfg_led.track_transpose, ui_page == SEQ_UI_PAGE_TRKTRAN);
  SEQ_LED_PinSet(seq_hwcfg_led.fx, ui_page == SEQ_UI_PAGE_FX);
  
  SEQ_LED_PinSet(seq_hwcfg_led.solo, seq_ui_button_state.SOLO);
  SEQ_LED_PinSet(seq_hwcfg_led.fast, seq_ui_button_state.FAST_ENCODERS);
  SEQ_LED_PinSet(seq_hwcfg_led.fast2, seq_ui_button_state.FAST2_ENCODERS);
  SEQ_LED_PinSet(seq_hwcfg_led.all, seq_ui_button_state.CHANGE_ALL_STEPS);
  
  u8 seq_running = SEQ_BPM_IsRunning() && (!seq_core_slaveclk_mute || ((seq_core_state.ref_step & 3) == 0));
  // note: no bug: we added check for ref_step&3 for flashing the LEDs to give a sign of activity in slave mode with slaveclk_muted
  SEQ_LED_PinSet(seq_hwcfg_led.play, seq_running);
  SEQ_LED_PinSet(seq_hwcfg_led.stop, !seq_running && !ui_seq_pause);
  SEQ_LED_PinSet(seq_hwcfg_led.pause, ui_seq_pause && (!seq_core_slaveclk_mute || ui_cursor_flash));

  SEQ_LED_PinSet(seq_hwcfg_led.rew, seq_ui_button_state.REW);
  SEQ_LED_PinSet(seq_hwcfg_led.fwd, seq_ui_button_state.FWD);

  SEQ_LED_PinSet(seq_hwcfg_led.loop, seq_core_state.LOOP);
  SEQ_LED_PinSet(seq_hwcfg_led.follow, seq_core_state.FOLLOW);
  
  SEQ_LED_PinSet(seq_hwcfg_led.step_view, ui_page == SEQ_UI_PAGE_STEPSEL || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_STEPS));

  SEQ_LED_PinSet(seq_hwcfg_led.select, seq_ui_button_state.SELECT_PRESSED);
  SEQ_LED_PinSet(seq_hwcfg_led.menu, seq_ui_button_state.MENU_PRESSED);
  // BOOKMARK: steady on the Bookmarks page / sel-view. Also, while SELECT is held (the
  // CHECKPOINT/REVERT modifier), light it if a checkpoint exists this session — so the
  // REVERT safety net is visible exactly when the SELECT+BOOKMARK gesture is armed, and
  // dark when there's nothing to revert to.
  SEQ_LED_PinSet(seq_hwcfg_led.bookmark,
		 ui_page == SEQ_UI_PAGE_BOOKMARKS
		 || (selbuttons_available && seq_ui_sel_view == SEQ_UI_SEL_VIEW_BOOKMARKS)
		 || (seq_ui_button_state.SELECT_PRESSED && SEQ_PATTERN_CheckpointValid()));

  // handle double functions
  if( seq_ui_button_state.MENU_PRESSED ) {
    SEQ_LED_PinSet(seq_hwcfg_led.scrub, seq_core_state.LOOP);
    SEQ_LED_PinSet(seq_hwcfg_led.exit, seq_core_state.FOLLOW);
    SEQ_LED_PinSet(seq_hwcfg_led.metronome, seq_core_state.EXT_RESTART_REQ);
  } else {
    SEQ_LED_PinSet(seq_hwcfg_led.scrub, seq_ui_button_state.SCRUB);
    SEQ_LED_PinSet(seq_hwcfg_led.exit, ui_page == SEQ_UI_PAGE_MENU);
    SEQ_LED_PinSet(seq_hwcfg_led.metronome, seq_core_state.FREEZE); // repurposed: lit = FROZEN
  }

  SEQ_LED_PinSet(seq_hwcfg_led.record, seq_record_state.ENABLED);
  // The LIVE lamp is the "in PROC mode" indicator (lit while on the Processor Rack page).
  SEQ_LED_PinSet(seq_hwcfg_led.live, ui_page == SEQ_UI_PAGE_PROC);
  SEQ_LED_PinSet(seq_hwcfg_led.jam_live, ui_page == SEQ_UI_PAGE_TRKJAM && !seq_record_options.STEP_RECORD);
  SEQ_LED_PinSet(seq_hwcfg_led.jam_step, ui_page == SEQ_UI_PAGE_TRKJAM && seq_record_options.STEP_RECORD);

  SEQ_LED_PinSet(seq_hwcfg_led.utility, capture_util_held || ui_page == SEQ_UI_PAGE_UTIL);
  SEQ_LED_PinSet(seq_hwcfg_led.copy, seq_ui_button_state.COPY);
  SEQ_LED_PinSet(seq_hwcfg_led.paste, seq_ui_button_state.PASTE);
  SEQ_LED_PinSet(seq_hwcfg_led.undo, seq_ui_button_state.UNDO);
  SEQ_LED_PinSet(seq_hwcfg_led.clear, seq_ui_button_state.CLEAR);
  SEQ_LED_PinSet(seq_hwcfg_led.move, seq_ui_button_state.MOVE);
  SEQ_LED_PinSet(seq_hwcfg_led.scroll, seq_ui_button_state.SCROLL);

  SEQ_LED_PinSet(seq_hwcfg_led.tap_tempo, seq_ui_button_state.TAP_TEMPO);
  SEQ_LED_PinSet(seq_hwcfg_led.tempo_preset, ui_page == SEQ_UI_PAGE_BPM_PRESETS);
  SEQ_LED_PinSet(seq_hwcfg_led.ext_restart, seq_core_state.EXT_RESTART_REQ);

  SEQ_LED_PinSet(seq_hwcfg_led.down, seq_ui_button_state.DOWN);
  SEQ_LED_PinSet(seq_hwcfg_led.up, seq_ui_button_state.UP);

  SEQ_LED_PinSet(seq_hwcfg_led.mute_all_tracks, seq_core_trk_muted == 0xffff);
  SEQ_LED_PinSet(seq_hwcfg_led.mute_track_layers, seq_core_trk[visible_track].layer_muted == 0xffff);
  SEQ_LED_PinSet(seq_hwcfg_led.unmute_all_tracks, seq_core_trk_muted == 0x0000);
  SEQ_LED_PinSet(seq_hwcfg_led.unmute_track_layers, seq_core_trk[visible_track].layer_muted == 0x0000);
  // only consume CPU time if LEDs really assigned...
  if( seq_hwcfg_led.mute_all_tracks_and_layers || seq_hwcfg_led.unmute_all_tracks_and_layers ) {
    u16 all_layers_muted_mask = 0;
    int track;
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track)
      all_layers_muted_mask |= seq_core_trk[track].layer_muted;

    SEQ_LED_PinSet(seq_hwcfg_led.mute_all_tracks_and_layers, seq_core_trk_muted == 0xffff && all_layers_muted_mask == 0xffff);
    SEQ_LED_PinSet(seq_hwcfg_led.unmute_all_tracks_and_layers, seq_core_trk_muted == 0x0000 && all_layers_muted_mask == 0x0000);
  }

  ui_gp_leds2 = 0x0000; // 2nd-color overlay: only the gen STEPS faces set it below

  // in MENU page: overrule GP LEDs as long as MENU button is pressed/active
  if( seq_ui_button_state.MENU_PRESSED || seq_hwcfg_blm.gp_always_select_menu_page ) {
    if( ui_cursor_flash ) // if flashing flag active: no LED flag set
      ui_gp_leds = 0x0000;
    else {
      int i;
      u16 new_ui_gp_leds = 0x0000;
      for(i=0; i<16; ++i)
	if( ui_page == SEQ_UI_PAGES_MenuShortcutPageGet(i) )
	  new_ui_gp_leds |= (1 << i);
      ui_gp_leds = new_ui_gp_leds;
    }
  } else {
    // note: the background function is permanently interrupted - therefore we write the GP pattern
    // into a temporary variable, and take it over once completed
    u16 new_ui_gp_leds = 0x0000;
    // request GP LED values from current menu page
    // will be transfered to DOUT registers in SEQ_UI_LED_Handler_Periodic
    new_ui_gp_leds = 0x0000;

    if( ui_led_callback != NULL )
      ui_led_callback(&new_ui_gp_leds);

    ui_gp_leds = new_ui_gp_leds;

    // Hold-then-paint gesture overlays: while a morph/capture/pull gesture is engaged,
    // the GP row reflects the gesture's LIVE AIM instead of the page's own LEDs. Only
    // one gesture is active at a time; the if/else-if ladder makes that explicit and
    // fixes precedence (morph > UTILITY-grab > PATTERN-capture > B-row PULL).
    if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_PHRASE && ui_page == morph_armed_page &&
	SEQ_PATTERN_PhraseMorphTarget() >= 0 ) {
      // POSTURE-MORPH bar: 16-segment thermometer of the morph position. Armed-page-
      // scoped to match the GP/datawheel intercepts (don't paint over EDIT's GP LEDs
      // while PHRASE view is latched on top of another page).
      u8 k = (SEQ_PATTERN_PhraseMorphValue() * 16) / PHRASE_MORPH_MAX;
      ui_gp_leds = k ? (u16)((1 << k) - 1) : 0x0000;
    }
    else if( capture_util_held ) {
      // Retroactive CAPTURE (UTILITY held): thermometer of grabbable depth in LOOPS —
      // the max grabbable K lit from GP1. Tap GP-n to grab n loops.
      u8 cap_k = SEQ_CORE_CaptureMaxK(SEQ_CORE_CaptureRingTrack()); // par/trg-aware grabbable max
      if( cap_k > 16 ) cap_k = 16;
      ui_gp_leds = cap_k ? (u16)((1 << cap_k) - 1) : 0x0000;
    }
    else if( seq_ui_button_state.PATTERN_PRESSED ) {
      // CAPTURE-to-slot (PATTERN held): GP1-8 = letter cursor on the aimed destination
      // group letter (default = the source pattern's current letter). GP9-16 are the
      // commit targets — press a number to freeze the visible track into that slot.
      u8 src_group = SEQ_UI_VisibleTrackGet() / SEQ_CORE_NUM_TRACKS_PER_GROUP;
      u8 grp = (pattern_capture_group != 0xff) ? (pattern_capture_group & 0x07)
					       : (u8)(seq_pattern[src_group].group & 0x07);
      ui_gp_leds = (u16)(1 << grp);
    }
    else if( pull_held_track != 0xff && seq_ui_sel_view == SEQ_UI_SEL_VIEW_TRACKS ) {
      // PULL / RECOMBINE (B-row track held): mirror of the capture push — GP1-8 = letter
      // cursor on the aimed SOURCE pattern (default = the source column's current
      // letter). GP9-16 commit the pull onto the held track.
      u8 src_col  = (pull_src_column != 0xff) ? pull_src_column : pull_held_track;
      u8 src_bank = src_col / SEQ_CORE_NUM_TRACKS_PER_GROUP;
      u8 letter   = (pull_letter != 0xff) ? (pull_letter & 0x07)
					  : (u8)(seq_pattern[src_bank].group & 0x07);
      ui_gp_leds = (u16)(1 << letter);
    }
    else if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_PROC ) {
      // PROC view: the GP row paints the focused processor's 16-object shape.
      // The merged Ptch row's shape is the 12-PC mask — keys 1..12 = pitch classes
      // C..B, keys 13..16 dark. Read from the CCs (not the live slot) since the
      // 2026-07-12 merge: the mask is ALWAYS shown — the Self mask (editable) or the
      // live source-bus chord (read-only) — engaged or not, mirroring the LCD
      // keyboard, so LEDs, keyboard, and paintability all agree.
      // Groove's shape is its 16-step template lane (below). Any other focused row
      // has no 16-object surface -> dark.
      u16 gp = 0x0000;
      u16 gp2 = 0x0000; // 2nd color (locks on the gen STEPS faces)
      if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_CHORDMASK_SELF ) {
        u8 vt = SEQ_UI_VisibleTrackGet();
        u8 cmbus = SEQ_CC_Get(vt, SEQ_CC_CHORDMASK_BUS);
        gp = (cmbus & 0x04) // Self: the editable static mask; else the live bus chord
           ? ((((u16)SEQ_CC_Get(vt, SEQ_CC_CHORDMASK_MASK_H) << 8)
               | SEQ_CC_Get(vt, SEQ_CC_CHORDMASK_MASK_L)) & 0x0fff)
           : (SEQ_MIDI_IN_BusPCSetGet(cmbus & 0x03) & 0x0fff);
      } else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_GROOVE_PAINT ) {
        // Groove: the 16-step template shape for the SELECTED lane (Dly/Len/Vel) — a
        // lit key = that step's cell is non-zero. Custom templates are paintable (GP
        // buttons toggle); presets show read-only; off = dark.
        const s8 *lane = SEQ_UI_PROC_GrooveLane(SEQ_UI_VisibleTrackGet());
        if( lane ) {
          int i;
          for(i=0; i<16; ++i)
            if( lane[i] )
              gp |= (1u << i);
        }
      } else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_LFO_PALETTE ) {
        // LFO: the waveform palette — light the one key whose palette entry matches the
        // current waveform (bit-7 disable masked off). No match (a non-palette waveform
        // set via the stock page) -> dark.
        u8 wave = SEQ_CC_Get(SEQ_UI_VisibleTrackGet(), SEQ_CC_LFO_WAVEFORM) & 0x7f;
        int i;
        for(i=0; i<16; ++i)
          if( lfo_wave_palette[i] == wave )
            gp |= (1u << i);
      } else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_ROBOLOOP ) {
        // Robotize LOOP: the anchor pool (0..palette-1) lit — tap any to reroll — with the
        // currently-playing anchor WINKING (only when a loop is running, cyc>0).
        u8 vt = SEQ_UI_VisibleTrackGet();
        u8 pal = SEQ_CC_Get(vt, SEQ_CC_ROBOTIZE_PALETTE_LENGTH);
        if( pal == 0 || pal > 16 ) pal = 16;
        int i;
        for(i=0; i<pal; ++i)
          gp |= (1u << i);
        if( SEQ_CC_Get(vt, SEQ_CC_ROBOTIZE_LOOP_CYCLES) && ui_cursor_flash )
          gp &= ~(1u << SEQ_UI_PROC_RoboPlayingAnchor(vt)); // wink the playhead
      } else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_PITCHGEN_STEPS ||
                 SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_TRIGGEN_STEPS ) {
        // Gen STEPS (2026-07-13, duo-color): color 1 = the window's TRIGGERED steps
        // (gate on — what sounds; visible pre-ENGAGE too, so a punched-in phrase
        // shows before the generator exists), color 2 = the LOCKS; both = the blend.
        // Mirrors the LCD activity strip. Steps past the track length stay dark.
        u8 is_trg = (SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_TRIGGEN_STEPS);
        u8 vt = SEQ_UI_VisibleTrackGet();
        u8 instr = SEQ_UI_PROC_GenInstr(vt);
        seq_generator_t *g = is_trg ? SEQ_UI_PROC_TGenGet(vt) : SEQ_UI_PROC_GenGet(vt);
        u16 num_steps = SEQ_TRG_NumStepsGet(vt);
        int i;
        for(i=0; i<16; ++i) {
          u16 st = (u16)(proc_gen_step_window*16 + i);
          if( st >= num_steps )
            continue;
          if( SEQ_TRG_GateGet(vt, st, instr) )
            gp |= (1u << i);
          if( g && SEQ_GENERATOR_LockGet(g, st) )
            gp2 |= (1u << i);
        }
      } else if( SEQ_UI_PROC_CurFace(ui_focused_proc_slot) == PROC_FACE_TENSION_ZONES ) {
        // Tension zones: light the CURRENT zone's button (GP9-15) — where you'd land if
        // you tapped it again — plus GP16 (Rslv) as a steady discoverability hint.
        u8 zi = tension_zone_index(seq_core_tension_gravity);
        gp = (u16)((1u << (8 + zi)) | (1u << 15));
      }
      ui_gp_leds = gp;
      ui_gp_leds2 = gp2;
    }
  }

  // update BLM LEDs
  SEQ_BLM_LED_Update();

  // send LED changes in remote server mode
  if( seq_midi_sysex_remote_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_SERVER || seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_SERVER ) {
    int first_sr = -1;
    int last_sr = -1;
    int sr;
    for(sr=0; sr<SEQ_LED_NUM_SR; ++sr) {
      u8 value = SEQ_LED_SRGet(sr);
      if( value != remote_led_sr[sr] ) {
	if( first_sr == -1 )
	  first_sr = sr;
	last_sr = sr;
	remote_led_sr[sr] = value;
      }
    }

    MIOS32_IRQ_Disable();
    if( seq_midi_sysex_remote_force_led_update ) {
      first_sr = 0;
      last_sr = SEQ_LED_NUM_SR-1;
    }
    seq_midi_sysex_remote_force_led_update = 0;
    MIOS32_IRQ_Enable();

    if( first_sr >= 0 )
      SEQ_MIDI_SYSEX_REMOTE_Server_SendLED(first_sr, (u8 *)&remote_led_sr[first_sr], last_sr-first_sr+1);
  }


  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// updates high-prio LED functions (GP LEDs and Beat LED)
// called each mS
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_LED_Handler_Periodic()
{
  // ignore in remote client mode
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return 0; // no error

  // ignore as long as hardware config hasn't been read
  if( !SEQ_FILE_HW_ConfigLocked() )
    return -1;

  // GP LEDs are updated when ui_gp_leds (either color) has changed
  static u16 prev_ui_gp_leds = 0x0000;
  static u16 prev_ui_gp_leds2 = 0x0000;
  u8 sequencer_running = SEQ_BPM_IsRunning();

  // beat LED
  u8 beat_led_on = sequencer_running && ((seq_core_state.ref_step % 4) == 0);
  u8 measure_led_on = sequencer_running && ((seq_core_state.ref_step % (seq_core_steps_per_measure+1)) == 0);
  if( seq_hwcfg_led.measure <= 0x7fff ) {
    SEQ_LED_PinSet(seq_hwcfg_led.beat, (seq_hwcfg_led.measure && measure_led_on) ? 0 : beat_led_on);
  } else {
    SEQ_LED_PinSet(seq_hwcfg_led.beat, beat_led_on);
  }

#if !defined(MIOS32_DONT_USE_BOARD_LED)
  // mirror to green status LED (inverted, so that LED is normaly on)
  MIOS32_BOARD_LED_Set(0x00000001, sequencer_running ? (beat_led_on ? 1 : 0) : 1);
#endif

  // measure LED
  SEQ_LED_PinSet(seq_hwcfg_led.measure, measure_led_on);

#if !defined(MIOS32_DONT_USE_BOARD_LED)
  // mirror to red status LED
  //MIOS32_BOARD_LED_Set(0x00000002, measure_led_on ? 2 : 0);
  // now used for SD Card indicator
  MIOS32_BOARD_LED_Set(0x00000002, FILE_SDCardAvailable() ? 2 : 0);
#endif


  // MIDI IN/OUT LEDs
  SEQ_LED_PinSet(seq_hwcfg_led.midi_in_combined, seq_midi_port_in_combined_ctr);
#if !defined(MIOS32_DONT_USE_BOARD_LED)
  MIOS32_BOARD_LED_Set(0x00000004, seq_midi_port_in_combined_ctr ? 4 : 0);
#endif
  SEQ_LED_PinSet(seq_hwcfg_led.midi_out_combined, seq_midi_port_out_combined_ctr);
#if !defined(MIOS32_DONT_USE_BOARD_LED)
  MIOS32_BOARD_LED_Set(0x00000008, seq_midi_port_out_combined_ctr ? 8 : 0);
#endif

  // don't continue if no new step has been generated and GP LEDs haven't changed
  if( !seq_core_step_update_req && prev_ui_gp_leds == ui_gp_leds
      && prev_ui_gp_leds2 == ui_gp_leds2 && sequencer_running ) // sequencer running check: workaround - as long as sequencer not running, we won't get an step update request!
    return 0;
  seq_core_step_update_req = 0; // requested from SEQ_CORE if any step has been changed
  prev_ui_gp_leds = ui_gp_leds; // take over new GP pattern
  prev_ui_gp_leds2 = ui_gp_leds2;

  // for song position marker (supports 16 LEDs, check for selected step view)
  u16 pos_marker_mask = 0x0000;
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 played_step = seq_core_trk[visible_track].step;

  if( seq_core_slaveclk_mute != SEQ_CORE_SLAVECLK_MUTE_Enabled ) { // Off and OffOnNextMeasure
    if( ui_page == SEQ_UI_PAGE_STEPSEL ) {
      // in STEPSEL page: pos marker correlated to zoom ratio
      if( sequencer_running )
	pos_marker_mask = 1 << (played_step / (SEQ_TRG_NumStepsGet(visible_track)/16));
    } else {
      if( sequencer_running && (played_step >> 4) == ui_selected_step_view )
	pos_marker_mask = 1 << (played_step & 0xf);
    }
  }


  // follow step position if enabled
  if( seq_core_state.FOLLOW ) {
    u8 trk_step = seq_core_trk[visible_track].step;
    if( (trk_step & 0xf0) != (16*ui_selected_step_view) ) {
      ui_selected_step_view = trk_step / 16;
      ui_selected_step = (ui_selected_step % 16) + 16*ui_selected_step_view;
      seq_ui_display_update_req = 1;
    }
  }

  // transfer to GP LEDs. The 2nd color carries the pos marker XOR the ui_gp_leds2
  // overlay (gen STEPS locks, 2026-07-13) — the playhead inverts as it sweeps a lock.
  // Single-color hardware folds the overlay into the one channel instead.
  if( seq_hwcfg_led.gp_dout_l_sr ) {
    if( seq_hwcfg_led.gp_dout_l2_sr )
      SEQ_LED_SRSet(seq_hwcfg_led.gp_dout_l_sr-1, (ui_gp_leds >> 0) & 0xff);
    else
      SEQ_LED_SRSet(seq_hwcfg_led.gp_dout_l_sr-1, (((ui_gp_leds | ui_gp_leds2) ^ pos_marker_mask) >> 0) & 0xff);
  }

  if( seq_hwcfg_led.gp_dout_r_sr ) {
    if( seq_hwcfg_led.gp_dout_r2_sr )
      SEQ_LED_SRSet(seq_hwcfg_led.gp_dout_r_sr-1, (ui_gp_leds >> 8) & 0xff);
    else
      SEQ_LED_SRSet(seq_hwcfg_led.gp_dout_r_sr-1, (((ui_gp_leds | ui_gp_leds2) ^ pos_marker_mask) >> 8) & 0xff);
  }

  if( seq_hwcfg_led.gp_dout_l2_sr )
    SEQ_LED_SRSet(seq_hwcfg_led.gp_dout_l2_sr-1, ((pos_marker_mask ^ ui_gp_leds2) >> 0) & 0xff);
  if( seq_hwcfg_led.gp_dout_r2_sr )
    SEQ_LED_SRSet(seq_hwcfg_led.gp_dout_r2_sr-1, ((pos_marker_mask ^ ui_gp_leds2) >> 8) & 0xff);

  // transfer to optional track LEDs
  if( seq_hwcfg_led.tracks_dout_l_sr )
    SEQ_LED_SRSet(seq_hwcfg_led.tracks_dout_l_sr-1, (ui_selected_tracks >> 0) & 0xff);
  if( seq_hwcfg_led.tracks_dout_r_sr )
    SEQ_LED_SRSet(seq_hwcfg_led.tracks_dout_r_sr-1, (ui_selected_tracks >> 8) & 0xff);

#if !defined(MIOS32_DONT_USE_BLM)
  if( seq_hwcfg_blm.enabled ) {
    // Red LEDs (position marker)
    int track_ix;
    for(track_ix=0; track_ix<4; ++track_ix) {
      u8 track = 4*ui_selected_group + track_ix;

      // determine position marker
      u16 pos_marker_mask = 0x0000;
      if( sequencer_running ) {
	u8 played_step = seq_core_trk[track].step;
	if( (played_step >> 4) == ui_selected_step_view )
	  pos_marker_mask = 1 << (played_step & 0xf);
      }

      // Prepare Green LEDs (triggers)
      // re-used from BLM_SCALAR code
      u16 green_pattern = blm_scalar_master_leds_green[track];

      // Red LEDs (position marker)
      if( seq_hwcfg_blm.dout_duocolour ) {
	BLM_DOUT_SRSet(1, 2*track_ix+0, pos_marker_mask);
	BLM_DOUT_SRSet(1, 2*track_ix+1, pos_marker_mask >> 8);

	if( seq_hwcfg_blm.dout_duocolour == 2 ) {
	  // Colour Mode 2: clear green LED, so that only one LED is lit
	  green_pattern &= ~pos_marker_mask;
	}
      } else {
	// If Duo-LEDs not enabled: invert Green LEDs
	green_pattern ^= pos_marker_mask;
      }

      // Set Green LEDs
      BLM_DOUT_SRSet(0, 2*track_ix+0, green_pattern);
      BLM_DOUT_SRSet(0, 2*track_ix+1, green_pattern >> 8);
    }
  }
#endif

  if( seq_hwcfg_blm8x8.enabled ) {
    if( seq_hwcfg_blm8x8.dout_gp_mapping == 1 ) {
      // for wilba's frontpanel

      // BLM_X DOUT -> GP LED mapping
      // 0 = 15,16	1 = 13,14	2 = 11,12	3 = 9,10
      // 4 = 1,2	5 = 3,4		6 = 5,6		7 = 7,8

      // bit 7: first green (i.e. GP1-G)
      // bit 6: first red (i.e. GP1-R)
      // bit 5: second green (i.e. GP2-G)
      // bit 4: second red (i.e. GP2-R)

      // this mapping routine takes ca. 5 uS
      // since it's only executed when ui_gp_leds or gp_mask has changed, it doesn't really hurt

      u16 modified_gp_leds = ui_gp_leds;

      // extra: red LED is lit exclusively for higher contrast
      if( !seq_ui_options.GP_LED_DONT_XOR_POS ) {
	modified_gp_leds &= ~pos_marker_mask;
      }

      u16 leds_colour1 = !seq_ui_options.SWAP_GP_LED_COLOURS ? modified_gp_leds : pos_marker_mask;
      u16 leds_colour2 = !seq_ui_options.SWAP_GP_LED_COLOURS ? pos_marker_mask : modified_gp_leds;

      int sr;
      const u8 blm_x_sr_map[8] = {4, 5, 6, 7, 3, 2, 1, 0};
      u16 gp_mask = 1 << 0;
      for(sr=0; sr<8; ++sr) {
	u8 pattern = 0;

	if( leds_colour1 & gp_mask )
	  pattern |= 0x80;
	if( leds_colour2 & gp_mask )
	  pattern |= 0x40;
	gp_mask <<= 1;
	if( leds_colour1 & gp_mask )
	  pattern |= 0x20;
	if( leds_colour2 & gp_mask )
	  pattern |= 0x10;
	gp_mask <<= 1;

	u8 mapped_sr = blm_x_sr_map[sr];
	seq_blm8x8_led_row[0][mapped_sr] = (seq_blm8x8_led_row[0][mapped_sr] & 0x0f) | pattern;
      }
    } else if( seq_hwcfg_blm8x8.dout_gp_mapping == 3 ) {
      // for Antilog frontpanel

      // default view
      if( seq_ui_sel_view == SEQ_UI_SEL_VIEW_NONE )
	seq_ui_sel_view = SEQ_UI_SEL_VIEW_TRACKS;

      // BLM_X DOUT -> GP LED mapping
      // left/right half offsets; green,red
      // 0 = 9,8        1 = 11,10       2 = 13,12       3 = 15,14
      // 4 = 41,40      2 = 43,42       3 = 45,44       4 = 47,46

      u16 modified_gp_leds = ui_gp_leds;

      // extra: red LED is lit exclusively for higher contrast
      if( !seq_ui_options.GP_LED_DONT_XOR_POS ) {
	modified_gp_leds &= ~pos_marker_mask;
      }

      u16 leds_colour1 = !seq_ui_options.SWAP_GP_LED_COLOURS ? modified_gp_leds : pos_marker_mask;
      u16 leds_colour2 = !seq_ui_options.SWAP_GP_LED_COLOURS ? pos_marker_mask : modified_gp_leds;

      // GP row, first quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 0) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 0) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 1) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 1) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 2) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 2) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 3) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 3) ) value |= (1 << 6);

	seq_blm8x8_led_row[0][1] = value;
      }

      // GP row, second quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 4) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 4) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 5) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 5) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 6) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 6) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 7) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 7) ) value |= (1 << 6);

	seq_blm8x8_led_row[0][5] = value;
      }

      // GP row, third quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 8) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 8) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 9) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 9) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 10) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 10) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 11) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 11) ) value |= (1 << 6);

	seq_blm8x8_led_row[1][1] = value;
      }

      // GP row, fourth quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 12) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 12) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 13) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 13) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 14) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 14) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 15) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 15) ) value |= (1 << 6);

	seq_blm8x8_led_row[1][5] = value;
      }


      // BLM_X DOUT -> Select LED mapping
      // like above, just next SR

      u16 select_leds_green = 0x0000;
      u16 select_leds_red   = 0x0000;

      switch( seq_ui_sel_view ) {
      case SEQ_UI_SEL_VIEW_BOOKMARKS:
	select_leds_green = 1 << ui_selected_bookmark;
	break;
      case SEQ_UI_SEL_VIEW_STEPS: {
	int num_steps = SEQ_TRG_NumStepsGet(visible_track);

	if( num_steps > 128 )
	  select_leds_green = 1 << ui_selected_step_view;
	else if( num_steps > 64 )
	  select_leds_green = 3 << (2*ui_selected_step_view);
	else
	  select_leds_green = 15 << (4*ui_selected_step_view);

	if( sequencer_running ) {
	  u8 played_step_view = (seq_core_trk[visible_track].step / 16);
	  if( num_steps > 128 )
	    select_leds_red = 1 << played_step_view;
	  else if( num_steps > 64 )
	    select_leds_red = 3 << (2*played_step_view);
	  else
	    select_leds_red = 15 << (4*played_step_view);
	}

	// ensure that green LEDs are off if overlapped by red LEDs
	//select_leds_green &= ~select_leds_red;
	// disabled: overlapping looks better with red/green LEDs
      } break;
      case SEQ_UI_SEL_VIEW_TRACKS:
	if( ui_page == SEQ_UI_PAGE_CAPTURE ) {
	  // Capture page: the B-row shows the DESTINATION track (green = its
	  // group, red = the track), not the live selection (= the source).
	  u8 d = SEQ_UI_CAPTURE_DstTrackGet();
	  select_leds_green = 0xf << (4*(d/4));
	  select_leds_red = 1 << d;
	} else {
	  select_leds_green = 0xf << (4*ui_selected_group);
	  select_leds_red = ui_selected_tracks;
	}
	break;
      case SEQ_UI_SEL_VIEW_PAR:
	select_leds_green = 1 << ui_selected_par_layer;
	break;
      case SEQ_UI_SEL_VIEW_TRG:
	select_leds_green = 1 << ui_selected_trg_layer;
	break;
      case SEQ_UI_SEL_VIEW_INS: {
	// Melodic keyboard play-surface: green = in-scale keys, amber = root/tonic.
	// Otherwise (drum pads / play off): the plain instrument cursor.
	u16 kbd_green, kbd_red;
	if( SEQ_UI_INSSEL_KeyboardLeds(&kbd_green, &kbd_red) ) {
	  select_leds_green = kbd_green;
	  select_leds_red   = kbd_red;
	} else {
	  select_leds_green = 1 << ui_selected_instrument;
	}
      } break;
      case SEQ_UI_SEL_VIEW_MUTE:
	if( seq_ui_button_state.MUTE_PRESSED ) {
	  select_leds_green = seq_core_trk[visible_track].layer_muted | seq_core_trk[visible_track].layer_muted_from_midi;
	} else {
	  select_leds_green = seq_core_trk_muted;
	}

	if( seq_ui_options.INVERT_MUTE_LEDS )
	  select_leds_green ^= 0xffff;
	break;
      case SEQ_UI_SEL_VIEW_PHRASE:
	// PHRASES navigation map: every occupied waypoint lights in one colour,
	// the current (last-recalled) waypoint in the other so it reads as "you
	// are here" (bicolor/amber on the slot, since you can only recall an
	// occupied one). Pure display of the session-scoped state — no new global.
	select_leds_green = SEQ_PATTERN_PhrasePresentMask();
	{
	  s32 cur = SEQ_PATTERN_PhraseLastRecalled();
	  if( cur >= 0 ) {
	    // DRIFT: when the organism has been deliberately edited since this
	    // waypoint was recalled/captured (SEQ_PATTERN_PhraseDrifted — excludes
	    // ambient generator wandering), wink the current slot's RED bit on
	    // ui_cursor_flash so it blinks amber<->green: "you've moved off this
	    // waypoint — recall to snap back, or capture to commit." The green
	    // occupancy bit stays solid, so the slot never reads as un-occupied.
	    if( !(SEQ_PATTERN_PhraseDrifted() && ui_cursor_flash) )
	      select_leds_red = 1 << cur;
	  }
	}
	break;
      case SEQ_UI_SEL_VIEW_PROC: {
	// The visible track's rack: green = row occupied (doing anything), red on the
	// focused row (reads as amber = "you are here"). A row that is occupied but at
	// true pass-through (bypassed or strength 0) winks its green on ui_cursor_flash,
	// so a processor that's doing something reads solid and a bypassed one blinks —
	// invariant 4's "dark = pass-through" inside the 2-colour select plane. Occupancy
	// comes from SEQ_UI_PROC_RowState (stack slot OR emission-FX CCs). Rows past the
	// rack stay dark.
	int s;
	for(s=0; s<PROC_NUM_ROWS; ++s) {
	  proc_rowstate_t st = SEQ_UI_PROC_RowState(visible_track, s);
	  if( !st.occupied )
	    continue;
	  u8 passthru = !st.enabled || !st.strength;
	  if( !(passthru && ui_cursor_flash) )
	    select_leds_green |= (1 << s);
	}
	select_leds_red = 1 << ui_focused_proc_slot;
	} break;
      }

      leds_colour1 = !seq_ui_options.SWAP_SELECT_LED_COLOURS ? select_leds_green : select_leds_red;
      leds_colour2 = !seq_ui_options.SWAP_SELECT_LED_COLOURS ? select_leds_red : select_leds_green;

      // Select row, first quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 0) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 0) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 1) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 1) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 2) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 2) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 3) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 3) ) value |= (1 << 6);

	seq_blm8x8_led_row[0][2] = value;
      }

      // Select row, second quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 4) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 4) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 5) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 5) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 6) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 6) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 7) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 7) ) value |= (1 << 6);

	seq_blm8x8_led_row[0][6] = value;
      }

      // Select row, third quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 8) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 8) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 9) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 9) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 10) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 10) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 11) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 11) ) value |= (1 << 6);

	seq_blm8x8_led_row[1][2] = value;
      }

      // Select row, fourth quarter
      {
	u8 value = 0;

	if( leds_colour1 & (1 << 12) ) value |= (1 << 1);
	if( leds_colour2 & (1 << 12) ) value |= (1 << 0);

	if( leds_colour1 & (1 << 13) ) value |= (1 << 3);
	if( leds_colour2 & (1 << 13) ) value |= (1 << 2);

	if( leds_colour1 & (1 << 14) ) value |= (1 << 5);
	if( leds_colour2 & (1 << 14) ) value |= (1 << 4);

	if( leds_colour1 & (1 << 15) ) value |= (1 << 7);
	if( leds_colour2 & (1 << 15) ) value |= (1 << 6);

	seq_blm8x8_led_row[1][6] = value;
      }
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// for menu handling (e.g. flashing cursor, doubleclick counter, etc...)
// called each mS
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_MENU_Handler_Periodic()
{
  // ignore in remote client mode
  if( seq_midi_sysex_remote_active_mode == SEQ_MIDI_SYSEX_REMOTE_MODE_CLIENT )
    return 0; // no error

  if( ++ui_cursor_flash_ctr >= SEQ_UI_CURSOR_FLASH_CTR_MAX ) {
    ui_cursor_flash_ctr = 0;
    ++ui_cursor_flash_overrun_ctr;
    seq_ui_display_update_req = 1;
  }

  // important: flash flag has to be recalculated on each invocation of this
  // handler, since counter could also be reseted outside this function
  u8 old_ui_cursor_flash = ui_cursor_flash;
  if( ui_page == SEQ_UI_PAGE_EDIT )
    ui_cursor_flash = ui_cursor_flash_ctr >= SEQ_UI_CURSOR_FLASH_CTR_LED_OFF_EDIT_PAGE;
  else
    ui_cursor_flash = ui_cursor_flash_ctr >= SEQ_UI_CURSOR_FLASH_CTR_LED_OFF;

  if( old_ui_cursor_flash != ui_cursor_flash )
    seq_ui_display_update_req = 1;

  // used in some pages for temporary messages
  if( ui_hold_msg_ctr ) {
    --ui_hold_msg_ctr;

    if( !ui_hold_msg_ctr )
      seq_ui_display_update_req = 1;
  }

  // used for temporary messages
  if( ui_msg_ctr ) {
    --ui_msg_ctr;

    // On expiry, request a page redraw so the LCD buffer's previously-
    // overlaid cells get overwritten with the underlying page content.
    // Without this, the popup's text stays visible in the LCD until the
    // next time the page's LCD_Handler is otherwise scheduled — which the
    // PITCHGEN harness tests surfaced by snapshotting the LCD right after
    // a popup nominally cleared and seeing the stale overlay. Matches the
    // ui_hold_msg_ctr pattern just above.
    if( !ui_msg_ctr )
      seq_ui_display_update_req = 1;
  }

  // VU meters (used in MUTE menu, could also be available as LED matrix...)
  static u8 vu_meter_prediv = 0; // predivider for VU meters

  if( ++vu_meter_prediv >= 4 ) {
    vu_meter_prediv = 0;


    portENTER_CRITICAL();

    u8 track;
    seq_core_trk_t *t = &seq_core_trk[0];
    for(track=0; track<SEQ_CORE_NUM_TRACKS; ++t, ++track)
      if( t->vu_meter )
	--t->vu_meter;

    int i;
    u8 *vu_meter = (u8 *)&seq_layer_vu_meter[0];
    for(i=0; i<sizeof(seq_layer_vu_meter); ++i, ++vu_meter) {
      if( *vu_meter && !(*vu_meter & 0x80) ) // if bit 7 set: static value
	*vu_meter -= 1;
    }

    portEXIT_CRITICAL();
  }

  // delayed action will be triggered once counter reached 0
  if( !ui_delayed_action_callback )
    ui_delayed_action_ctr = 0;
  else if( ui_delayed_action_ctr ) {
    if( --ui_delayed_action_ctr == 0 ) {
      // must be atomic
      MIOS32_IRQ_Disable();
      s32 (*_ui_delayed_action_callback)(u32 parameter);
      _ui_delayed_action_callback = ui_delayed_action_callback;
      u32 parameter = ui_delayed_action_parameter;
      ui_delayed_action_callback = NULL;
      MIOS32_IRQ_Enable();
      _ui_delayed_action_callback(parameter); // note: it's allowed that the delayed action generates a new delayed action
    }
  }

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Should be regulary called to check if the layer/instrument/step selection
// is valid for the current track
// At least executed before button/encoder and LCD function calls
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CheckSelections(void)
{
  if( ((ui_selected_tracks >> (4*ui_selected_group)) & 0xf) == 0 )
    ui_selected_tracks = 1 << (4*ui_selected_group);

  u8 visible_track = SEQ_UI_VisibleTrackGet();

  if( seq_ui_options.RESTORE_TRACK_SELECTIONS && visible_track != seq_ui_track_setup_visible_track ) {
    seq_ui_track_setup_t *s = &seq_ui_track_setup[visible_track];
    ui_selected_instrument = s->selected_instrument;
    ui_selected_par_layer  = s->selected_par_layer;
    ui_selected_trg_layer  = s->selected_trg_layer;
    ui_selected_step_view  = s->selected_step_view;

    // ensure that selected step is within view
    ui_selected_step = 16*ui_selected_step_view + (ui_selected_step % 16);
  }

  if( ui_selected_instrument >= SEQ_PAR_NumInstrumentsGet(visible_track) )
    ui_selected_instrument = 0;

  if( ui_selected_par_layer >= SEQ_PAR_NumLayersGet(visible_track) )
    ui_selected_par_layer = 0;

  if( ui_selected_trg_layer >= SEQ_TRG_NumLayersGet(visible_track) )
    ui_selected_trg_layer = 0;

  if( ui_selected_step >= SEQ_TRG_NumStepsGet(visible_track) )
    ui_selected_step = 0;

  if( ui_selected_step_view >= (SEQ_TRG_NumStepsGet(visible_track)/16) ) {
    ui_selected_step_view = 0;
    ui_selected_step %= 16;
  }

  if( !seq_ui_button_state.CHANGE_ALL_STEPS ) { // don't change the view if ALL function is active, otherwise the ramp can't be changed over multiple views
    if( ui_selected_step < (16*ui_selected_step_view) || 
	ui_selected_step >= (16*(ui_selected_step_view+1)) )
      ui_selected_step_view = ui_selected_step / 16;
  }

  // store settings for restore function
  seq_ui_track_setup_visible_track = visible_track;
  {
    seq_ui_track_setup_t *s = &seq_ui_track_setup[visible_track];
    s->selected_instrument = ui_selected_instrument;
    s->selected_par_layer  = ui_selected_par_layer;
    s->selected_trg_layer  = ui_selected_trg_layer;
    s->selected_step_view  = ui_selected_step_view;
  }

  // send selected track via MIDI if it has been changed
  if( seq_ui_track_cc.mode && seq_ui_sent_cc_track != visible_track ) {
    seq_ui_sent_cc_track = visible_track;

    switch( seq_ui_track_cc.mode ) {
    case 1: {
      MIOS32_MIDI_SendCC(seq_ui_track_cc.port, seq_ui_track_cc.chn, seq_ui_track_cc.cc, visible_track);
    } break;
    case 2: {
      MIOS32_MIDI_SendCC(seq_ui_track_cc.port, seq_ui_track_cc.chn, (seq_ui_track_cc.cc + visible_track) & 0x7f, 0x7f);
    } break;
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Returns the currently visible track
/////////////////////////////////////////////////////////////////////////////
u8 SEQ_UI_VisibleTrackGet(void)
{
  u8 offset = 0;

  u8 selected_tracks = ui_selected_tracks >> (4*ui_selected_group);
  if( selected_tracks & (1 << 3) )
    offset = 3;
  if( selected_tracks & (1 << 2) )
    offset = 2;
  if( selected_tracks & (1 << 1) )
    offset = 1;
  if( selected_tracks & (1 << 0) )
    offset = 0;

  return 4*ui_selected_group + offset;
}


/////////////////////////////////////////////////////////////////////////////
// Returns 1 if 'track' is selected
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_IsSelectedTrack(u8 track)
{
  return (ui_selected_tracks & (1 << track)) ? 1 : 0;
}


/////////////////////////////////////////////////////////////////////////////
// Sets a new selected step and updates the step view
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_SelectedStepSet(u8 step)
{
  ui_selected_step = step;
  SEQ_UI_CheckSelections();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Increments the selected tracks/groups
// OUT: 1 if value has been changed, otherwise 0
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_GxTyInc(s32 incrementer)
{
  int gxty = SEQ_UI_VisibleTrackGet();
  int prev_gxty = gxty;

  if( incrementer >= 0 ) {
    if( (gxty += incrementer) >= SEQ_CORE_NUM_TRACKS )
      gxty = SEQ_CORE_NUM_TRACKS-1;
  } else {
    if( (gxty += incrementer) < 0 )
      gxty = 0;
  }

  if( gxty == prev_gxty )
    return 0; // no change

  ui_selected_tracks = 1 << gxty;
  ui_selected_group = gxty / 4;

  return 1; // value changed
}


/////////////////////////////////////////////////////////////////////////////
// Increments a 16bit variable within given min/max range
// OUT: 1 if value has been changed, otherwise 0
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Var16_Inc(u16 *value, u16 min, u16 max, s32 incrementer)
{
  int new_value = *value;
  int prev_value = new_value;

  // extra: in fast mode increment 16bit values faster!
  if( max > 0x100 && (seq_ui_button_state.FAST_ENCODERS || seq_ui_button_state.FAST2_ENCODERS) )
    incrementer *= 10;

  if( incrementer >= 0 ) {
    if( (new_value += incrementer) >= max )
      new_value = max;
  } else {
    if( (new_value += incrementer) < min )
      new_value = min;
  }

  if( new_value == prev_value )
    return 0; // no change

  *value = new_value;

  return 1; // value changed
}

/////////////////////////////////////////////////////////////////////////////
// Increments an 8bit variable within given min/max range
// OUT: 1 if value has been changed, otherwise 0
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Var8_Inc(u8 *value, u16 min, u16 max, s32 incrementer)
{
  u16 tmp = *value;
  if( SEQ_UI_Var16_Inc(&tmp, min, max, incrementer) ) {
    *value = tmp;
    return 1; // value changed
  }

  return 0; // value hasn't been changed
}


/////////////////////////////////////////////////////////////////////////////
// Sends the current CC parameter of the given track to seq_midi_in_ext_ctrl_out_port
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CC_SendParameter(u8 track, u8 cc)
{
  if( seq_midi_in_ext_ctrl_asg[SEQ_MIDI_IN_EXT_CTRL_NRPN_ENABLED] &&
      seq_midi_in_ext_ctrl_out_port &&
      seq_midi_in_ext_ctrl_channel ) {
    mios32_midi_port_t port = seq_midi_in_ext_ctrl_out_port;
    mios32_midi_chn_t chn = seq_midi_in_ext_ctrl_channel - 1;
    u8 mapped_cc;
    s32 value = SEQ_CC_MIDI_Get(track, cc, &mapped_cc);

    if( value >= 0 ) {
      MUTEX_MIDIOUT_TAKE;
      MIOS32_MIDI_SendCC(port, chn, 0x63, track);
      MIOS32_MIDI_SendCC(port, chn, 0x62, mapped_cc);
      MIOS32_MIDI_SendCC(port, chn, 0x06, value & 0x7f);
      MUTEX_MIDIOUT_GIVE;
    }
  }

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Sets a CC value on all selected tracks
// OUT: 1 if value has been changed, otherwise 0
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CC_Set(u8 cc, u8 value)
{
  // set same value for all selected tracks
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    if( SEQ_UI_IsSelectedTrack(track) ) {
      SEQ_RECORD_CtrlCC(track, cc, value);
      
      int prev_value = SEQ_CC_Get(track, cc);
      if( value == prev_value )
	continue; // no change

      SEQ_CC_Set(track, cc, value);
      SEQ_UI_CC_SendParameter(track, cc);
    }
  }

  return 1; // value changed
}


/////////////////////////////////////////////////////////////////////////////
// Increments a CC within given min/max range
// OUT: 1 if value has been changed, otherwise 0
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CC_Inc(u8 cc, u8 min, u8 max, s32 incrementer)
{
  u8 visible_track = SEQ_UI_VisibleTrackGet();
  int new_value = SEQ_CC_Get(visible_track, cc);

  if( incrementer >= 0 ) {
    if( (new_value += incrementer) >= max )
      new_value = max;
  } else {
    if( (new_value += incrementer) < min )
      new_value = min;
  }

  // set value
  SEQ_UI_CC_Set(cc, new_value);

  return 1; // value changed
}


/////////////////////////////////////////////////////////////////////////////
// Modifies a bitfield in a CC value to a given value
// OUT: 1 if value has been changed, otherwise 0
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_CC_SetFlags(u8 cc, u8 flag_mask, u8 value)
{
  // do same modification for all selected tracks
  u8 track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    if( SEQ_UI_IsSelectedTrack(track) ) {
      int new_value = SEQ_CC_Get(track, cc);
      int prev_value = new_value;
      new_value = (new_value & ~flag_mask) | value;

      SEQ_RECORD_CtrlCC(track, cc, new_value);
      
      if( new_value == prev_value )
	continue; // no change

      SEQ_CC_Set(track, cc, new_value);
      SEQ_UI_CC_SendParameter(track, cc);
    }
  }

  return 1; // value changed
}


/////////////////////////////////////////////////////////////////////////////
// Print temporary user messages (e.g. warnings, errors)
// expects mS delay and two lines, each up to 20 characters
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Msg(seq_ui_msg_type_t msg_type, u16 delay, char *line1, char *line2)
{
  ui_msg_type = msg_type;
  ui_msg_ctr = delay;
  strncpy((char *)ui_msg[0], line1, UI_MSG_MAX_CHAR-1);
  strncpy((char *)ui_msg[1], line2, UI_MSG_MAX_CHAR-1);

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Stops temporary message if no SD card warning
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_MsgStop(void)
{
  if( ui_msg_type != SEQ_UI_MSG_SDCARD )
    ui_msg_ctr = 0;

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
// Prints a temporary error messages after file operation
// Expects error status number (as defined in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_SDCardErrMsg(u16 delay, s32 status)
{
  // send error message to MIOS terminal
  MUTEX_MIDIOUT_TAKE;
  FILE_SendErrorMessage(status);
  MUTEX_MIDIOUT_GIVE;

  // print on LCD
  char str[21];
  sprintf(str, "E%3d (FatFs: D%3d)", -status, file_dfs_errno < 1000 ? file_dfs_errno : 999);
  return SEQ_UI_Msg(SEQ_UI_MSG_SDCARD, delay, "!! SD Card Error !!!", str);
}


/////////////////////////////////////////////////////////////////////////////
// Prints a temporary message when MIDI learn has been activated/deactivated
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_MIDILearnMessage(seq_ui_msg_type_t msg_type, u8 on_off)
{
  if( on_off ) {
    char tmp[20];

    u8 learn_chn = 0;
    mios32_midi_port_t learn_port = 0;

    // pick up first matching port which is in Play mode
    int bus;
    for(bus=0; bus<SEQ_MIDI_IN_NUM_BUSSES; ++bus) {
      if( seq_midi_in_options[bus].MODE_PLAY && (learn_chn = seq_midi_in_channel[bus]) ) {
	learn_port = seq_midi_in_port[bus];
	break;
      }
    }

    if( learn_chn == 0 ) {
      sprintf(tmp, "disable (config in REC page!)");
    } else if( learn_chn > 16 ) {
      sprintf(tmp, "Port: %s  Chn All", SEQ_MIDI_PORT_InNameGet(SEQ_MIDI_PORT_InIxGet(learn_port)));
    } else {
      sprintf(tmp, "Port: %s  Chn #%2d", SEQ_MIDI_PORT_InNameGet(SEQ_MIDI_PORT_InIxGet(learn_port)), learn_chn);
    }
    SEQ_UI_Msg(msg_type, 1000, "MIDI Learn active:", tmp);
  } else {
    SEQ_UI_Msg(msg_type, 1000, "MIDI Learn", "deactivated");
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Help function to browse through a list (e.g. directory)
// incrementer: forwarded from encoder handler
// num_items: number of items in list
// max_items_on_screen: how many items are displayed on screen?
// *selected_item_on_screen: selected item on screen
// *view_offset: pointer to view offset variable
//
// Returns 1 if list has to be updated due to new offset
// Returns 0 if no update required
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_SelectListItem(s32 incrementer, u8 num_items, u8 max_items_on_screen, u8 *selected_item_on_screen, u8 *view_offset)
{
  u8 prev_view_offset = *view_offset;
  int prev_cursor = *view_offset + *selected_item_on_screen;
  int new_cursor = prev_cursor + incrementer;

  if( incrementer > 0 ) {
    if( new_cursor >= num_items ) {
#if 0
      // with overrun
      *selected_item_on_screen = 0;
      *view_offset = 0;
#else
      // no overrun
      if( num_items > max_items_on_screen ) {
	*view_offset = num_items - max_items_on_screen;
	*selected_item_on_screen = max_items_on_screen - 1;
      } else {
	*view_offset = 0;
	*selected_item_on_screen = num_items - 1;
      }
#endif
    } else if( (new_cursor - *view_offset) >= max_items_on_screen ) {
      *selected_item_on_screen = max_items_on_screen - 1;
      *view_offset = new_cursor - *selected_item_on_screen;
    } else {
      *selected_item_on_screen = new_cursor - *view_offset;
    }
  } else if( incrementer < 0 ) {
    if( new_cursor < 0 ) {
#if 0
      // with overrun
      *selected_item_on_screen = max_items_on_screen - 1;
      if( *selected_item_on_screen >= (num_items-1) ) {
	*selected_item_on_screen = num_items - 1;
	*view_offset = 0;
      } else {
	*view_offset = num_items - max_items_on_screen - 1;
      }
#else
      // without overrun
      *selected_item_on_screen = 0;
      *view_offset = 0;
#endif
    } else if( new_cursor < *view_offset ) {
      *selected_item_on_screen = 0;
      *view_offset = new_cursor;
    } else {
      *selected_item_on_screen = new_cursor - *view_offset;
    }
  }

  return prev_view_offset != *view_offset;
}


/////////////////////////////////////////////////////////////////////////////
// Help functions for the "keypad" editor to edit names
/////////////////////////////////////////////////////////////////////////////

static const char ui_keypad_charsets_upper[10][6] = {
  ".,!1~",
  "ABC2~",
  "DEF3~",
  "GHI4~",
  "JKL5~",
  "MNO6~",
  "PQRS7",
  "TUV8~",
  "WXYZ9",
  "-_ 0~",
};


static const char ui_keypad_charsets_lower[10][6] = {
  ".,!1~",
  "abc2~",
  "def3~",
  "ghi4~",
  "jkl5~",
  "mno6~",
  "pqrs7",
  "tuv8~",
  "wxyz9",
  "-_ 0~",
};

static u8 ui_keypad_select_charset_lower;
static s8 ui_keypad_last_key;

s32 SEQ_UI_KeyPad_Init(void)
{
  ui_keypad_select_charset_lower = 0;
  ui_keypad_last_key = -1;
  ui_edit_name_cursor = 0;

  return 0; // no error
}

// called by delayed action (after 0.75 second) to increment cursor after keypad entry
static s32 SEQ_UI_KeyPad_IncCursor(u32 len)
{
  if( ++ui_edit_name_cursor >= len )
    ui_edit_name_cursor = len - 1;

  ui_keypad_select_charset_lower = 1;
  ui_keypad_last_key = -1;

  ui_cursor_flash_ctr = ui_cursor_flash_overrun_ctr = 0;

  return 0; // no error
}

// handles the 16 GP buttons/encoders
s32 SEQ_UI_KeyPad_Handler(seq_ui_encoder_t encoder, s32 incrementer, char *edit_str, u8 len)
{
  char *edit_char = (char *)&edit_str[ui_edit_name_cursor];

  if( encoder <= SEQ_UI_ENCODER_GP10 ) {

    if( ui_keypad_last_key != -1 && ui_keypad_last_key != encoder ) {
      SEQ_UI_KeyPad_IncCursor(len);
      edit_char = (char *)&edit_str[ui_edit_name_cursor];
    }
    ui_keypad_last_key = encoder;

    char *charset = ui_keypad_select_charset_lower
      ? (char *)&ui_keypad_charsets_lower[encoder]
      : (char *)&ui_keypad_charsets_upper[encoder];

    int pos;
    if( incrementer >= 0 ) {
      for(pos=0; pos<5; ++pos) {
	if( *edit_char == charset[pos] ) {
	  ++pos;
	  break;
	}
      }

      if( charset[pos] == '~' || charset[pos] == 0 )
	pos = 0;
    } else {
      for(pos=4; pos>=0; --pos) {
	if( *edit_char == charset[pos] ) {
	  --pos;
	  break;
	}
      }

      if( pos == 0 )
	pos = 4;
      if( charset[pos] == '~' )
	pos = 3;
    }

    // set new char
    *edit_char = charset[pos];

    // a delayed action increments the cursor
    SEQ_UI_InstallDelayedActionCallback(SEQ_UI_KeyPad_IncCursor, 750, len);

    return 1;
  }

  SEQ_UI_UnInstallDelayedActionCallback(SEQ_UI_KeyPad_IncCursor);

  switch( encoder ) {
    case SEQ_UI_ENCODER_GP11: // change character directly with encoder or toggle upper/lower chars with button
      if( !incrementer ) {
	ui_keypad_select_charset_lower ^= 1;
	return 1;
      }
      return SEQ_UI_Var8_Inc((u8 *)&edit_str[ui_edit_name_cursor], 32, 127, incrementer);

    case SEQ_UI_ENCODER_GP12: // move cursor
      if( !incrementer )
	incrementer = 1;

      if( SEQ_UI_Var8_Inc(&ui_edit_name_cursor, 0, len-1, incrementer) >= 1 ) {
	edit_char = (char *)&edit_str[ui_edit_name_cursor];
	if( *edit_char == ' ' || (*edit_char >= 'A' && *edit_char <= 'Z') )
	  ui_keypad_select_charset_lower = 0;
	else
	  ui_keypad_select_charset_lower = 1;
      }
      return 0;

    case SEQ_UI_ENCODER_GP13: { // delete previous char
      if( ui_edit_name_cursor > 0 )
	--ui_edit_name_cursor;

      int i;
      int field_start = ui_edit_name_cursor;
      int field_end = len - 1;

      for(i=field_start; i<field_end; ++i)
	edit_str[i] = edit_str[i+1];
      edit_str[field_end] = ' ';

      if( ui_edit_name_cursor == 0 )
	ui_keypad_select_charset_lower = 0;

      return 1;
    } break;

    case SEQ_UI_ENCODER_GP14: { // insert char
      int i;
      int field_start = ui_edit_name_cursor;
      int field_end = len - 1;

      for(i=field_end; i>field_start; --i)
	edit_str[i] = edit_str[i-1];
      edit_str[field_start] = ' ';
      return 1;
    } break;
  }

  return -1; // unsupported encoder function
}


// to print lower line of keypad editor (only 69 chars, the remaining 11 chars have to be print from caller)
s32 SEQ_UI_KeyPad_LCD_Msg(void)
{
  int i;

  SEQ_LCD_CursorSet(0, 1);
  for(i=0; i<10; ++i) {
    char *charset = ui_keypad_select_charset_lower
      ? (char *)&ui_keypad_charsets_lower[i]
      : (char *)&ui_keypad_charsets_upper[i];

    if( i == 7 || i == 9 ) // if previous item had 5 chars
      SEQ_LCD_PrintChar(' ');
    else if( i == 8 ) // change to right LCD
      SEQ_LCD_CursorSet(40, 1);

    int pos;
    for(pos=0; pos<5; ++pos) {
      SEQ_LCD_PrintChar(charset[pos] == '~' ? ' ' : charset[pos]);
    }
  }

  SEQ_LCD_PrintString(" Char <>  Del Ins ");
  
  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// stores a bookmark
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Bookmark_Store(u8 bookmark)
{
  if( bookmark >= SEQ_UI_BOOKMARKS_NUM )
    return -1;

  seq_ui_bookmark_t *bm = (seq_ui_bookmark_t *)&seq_ui_bookmarks[bookmark];

  // note: name, enable flags and flags.LOCKED not overwritten!

  bm->flags.SOLO = seq_ui_button_state.SOLO;
  bm->flags.CHANGE_ALL_STEPS = seq_ui_button_state.CHANGE_ALL_STEPS;
  bm->flags.FAST = seq_ui_button_state.FAST_ENCODERS;
  bm->flags.METRONOME = seq_core_state.METRONOME;
  bm->flags.LOOP = seq_core_state.LOOP;
  bm->flags.FOLLOW = seq_core_state.FOLLOW;
  bm->page = (u8)ui_bookmarks_prev_page;
  bm->group = ui_selected_group;
  bm->par_layer = ui_selected_par_layer;
  bm->trg_layer = ui_selected_trg_layer;
  bm->instrument = ui_selected_instrument;
  bm->step_view = ui_selected_step_view;
  bm->step = ui_selected_step;
  bm->edit_view = seq_ui_edit_view;
  bm->tracks = ui_selected_tracks;
  bm->mutes = seq_core_trk_muted;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// restores a bookmark
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_Bookmark_Restore(u8 bookmark)
{
  if( bookmark >= SEQ_UI_BOOKMARKS_NUM )
    return -1;

  seq_ui_bookmark_t *bm = (seq_ui_bookmark_t *)&seq_ui_bookmarks[bookmark];

  // should be atomic
  portENTER_CRITICAL();
  if( bm->enable.SOLO )              seq_ui_button_state.SOLO = bm->flags.SOLO;
  if( bm->enable.CHANGE_ALL_STEPS )  seq_ui_button_state.CHANGE_ALL_STEPS = bm->flags.CHANGE_ALL_STEPS;
  if( bm->enable.FAST )              seq_ui_button_state.FAST_ENCODERS = bm->flags.FAST;
  if( bm->enable.METRONOME )         seq_core_state.METRONOME = bm->flags.METRONOME;
  if( bm->enable.LOOP )              seq_core_state.LOOP = bm->flags.LOOP;
  if( bm->enable.FOLLOW )            seq_core_state.FOLLOW = bm->flags.FOLLOW;
  if( bm->enable.GROUP )             ui_selected_group = bm->group;
  if( bm->enable.PAR_LAYER )         ui_selected_par_layer = bm->par_layer;
  if( bm->enable.TRG_LAYER )         ui_selected_trg_layer = bm->trg_layer;
  if( bm->enable.INSTRUMENT )        ui_selected_instrument = bm->instrument;
  if( bm->enable.STEP_VIEW )         ui_selected_step_view = bm->step_view;
  if( bm->enable.STEP )              ui_selected_step = bm->step;
  if( bm->enable.EDIT_VIEW )         seq_ui_edit_view = bm->edit_view;
  if( bm->enable.TRACKS )            ui_selected_tracks = bm->tracks;
  if( bm->enable.MUTES )             seq_core_trk_muted = bm->mutes;
  portEXIT_CRITICAL();

  // enter new page if enabled
  if( bm->enable.PAGE )
    SEQ_UI_PageSet((seq_ui_page_t)bm->page);

  return 0; // no error
}
