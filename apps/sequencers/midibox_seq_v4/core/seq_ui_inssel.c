// $Id$
/*
 * Instrument selection page
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
#include "seq_lcd.h"
#include "seq_ui.h"
#include "seq_hwcfg.h"

#include "seq_trg.h"
#include "seq_par.h"
#include "seq_cc.h"
#include "seq_live.h"
#include "seq_record.h"
#include "seq_core.h"
#include "seq_scale.h"
#include "seq_bpm.h"
#include "tasks.h"


/////////////////////////////////////////////////////////////////////////////
// One-row isomorphic keyboard (melodic tracks) — state & note builder
//
// When the play option is enabled and a melodic (non-Drum) track is selected,
// the 16 GP buttons become a playable row. Three layouts (OPT "Melodic keyboard
// layout"):
//   - Chromatic (isomorphic): key k = base + k*JUMP semitones. JUMP is the
//     interval between adjacent keys (GP1 encoder, 1..12): 1=chromatic, 2=whole
//     tone, 5=fourths, 7=fifths, 12=octaves. Like a Hapax pad layout.
//   - Scale degrees: key k = the k-th note of the track's scale above the tonic.
//   - Diatonic chords: key k = the in-key triad on degree k (k, k+2, k+4).
// The whole row transposes on a shared BASE offset (TRANSPOSE), so one row can
// reach the full range: < / > and the datawheel scroll by 1 semitone, SELECT +
// GP1/GP16 by an octave. Scale/root are read live via SEQ_CORE_FTS_GetScaleAndRoot.
// Plays like a pad (preview always; records when REC is armed) so the track's
// transpose / force-to-scale / FX apply to preview and playback alike.
/////////////////////////////////////////////////////////////////////////////
#define INSSEL_KBD_BASE_NOTE 0x3c            // key 0 at transpose 0 (middle C); multiple of 12
#define INSSEL_KBD_TRIGGER_VELOCITY 100
#define INSSEL_KBD_MAX_NOTES 3               // up to a triad per key
#define INSSEL_KBD_TRANSPOSE_MIN -60         // ±5 octaves of base travel
#define INSSEL_KBD_TRANSPOSE_MAX  60

static u8  inssel_kbd_jump = 1;              // isomorphic interval 1..12 semitones (GP1 encoder)
static s16 inssel_kbd_transpose = 0;         // base offset in semitones (scroll: </> + datawheel = ±1, SELECT+GP1/16 = ±12)
static u8  inssel_kbd_held[16][INSSEL_KBD_MAX_NOTES]; // notes held per key (0xff = empty), for note-off
static u8  inssel_kbd_recorded_chord[16];    // 1 = this key's press used the atomic chord recorder

// Build the note list a key would play, given the current layout + transpose +
// jump. Returns the number of notes (1, or up to 3 for chords). Single source of
// truth so the LEDs and the LCD show exactly what the key sounds.
static u8 inssel_kbd_build_notes(u8 track, u8 key, u8 *notes)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  u8 num = 0;

  if( seq_ui_options.INSSEL_KBD_SCALE_DEGREE || seq_ui_options.INSSEL_KBD_CHORD ) {
    u8 scale, root_selection, root;
    SEQ_CORE_FTS_GetScaleAndRoot(track, seq_core_trk[track].step, 0, tcc, &scale, &root_selection, &root);
    int base = (int)INSSEL_KBD_BASE_NOTE + (int)inssel_kbd_transpose + (int)root; // tonic in anchor octave
    if( base < 0 )   base = 0;
    if( base > 127 ) base = 127;

    if( seq_ui_options.INSSEL_KBD_CHORD ) {
      notes[num++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)key);
      notes[num++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)(key + 2));
      notes[num++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)(key + 4));
    } else {
      notes[num++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)key);
    }
  } else {
    // chromatic isomorphic row: key k = base + k*jump semitones
    int n = (int)INSSEL_KBD_BASE_NOTE + (int)inssel_kbd_transpose + (int)key * (int)inssel_kbd_jump;
    if( n < 0 )   n = 0;
    if( n > 127 ) n = 127;
    notes[num++] = (u8)n;
  }

  return num;
}

// format a MIDI note into buf as e.g. "C-3" / "C#3" (SEQ_LCD_PrintNote naming)
static void inssel_kbd_note_str(u8 note, char *buf)
{
  const char *const nt[12] = { "C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-" };
  sprintf(buf, "%s%d", nt[note % 12], (int)(note / 12) - 2);
}

// 1 if the melodic keyboard play-surface is the active B-row surface (play option
// on + a melodic track). Gates the global INS-sel-view control intercepts so they
// don't steal the datawheel/encoders in instrument-select or drum modes.
s32 SEQ_UI_INSSEL_KeyboardActive(void)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);
  return seq_ui_options.INSSEL_DRUM_TRIGGER && event_mode != SEQ_EVENT_MODE_Drum;
}

// scroll the whole row by <delta> semitones (fine transpose). Returns 1 if moved.
// Pops a transient base-note readout, since the note-name LCD is only visible on
// the INSSEL page and the keyboard is usually played from the latched INS sel-view
// on another page.
s32 SEQ_UI_INSSEL_KeyboardScroll(s32 delta)
{
  s16 old = inssel_kbd_transpose;
  int nv = (int)inssel_kbd_transpose + delta;
  if( nv < INSSEL_KBD_TRANSPOSE_MIN ) nv = INSSEL_KBD_TRANSPOSE_MIN;
  if( nv > INSSEL_KBD_TRANSPOSE_MAX ) nv = INSSEL_KBD_TRANSPOSE_MAX;
  inssel_kbd_transpose = (s16)nv;
  if( inssel_kbd_transpose == old )
    return 0;
  {
    u8 track = SEQ_UI_VisibleTrackGet();
    u8 notes[INSSEL_KBD_MAX_NOTES];
    char nb[8], buf[24];
    inssel_kbd_build_notes(track, 0, notes);
    inssel_kbd_note_str(notes[0], nb);
    sprintf(buf, "Base: %s", nb);
    SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Keyboard scroll", buf);
  }
  return 1;
}

// set the isomorphic jump interval (chromatic layout only). Returns 1 if changed.
s32 SEQ_UI_INSSEL_KeyboardJump(s32 incrementer)
{
  if( seq_ui_options.INSSEL_KBD_SCALE_DEGREE || seq_ui_options.INSSEL_KBD_CHORD )
    return 0; // jump is meaningless when the row walks the scale
  u8 old = inssel_kbd_jump;
  SEQ_UI_Var8_Inc(&inssel_kbd_jump, 1, 12, incrementer);
  if( inssel_kbd_jump == old )
    return 0;
  {
    char buf[24];
    sprintf(buf, "Jump: +%d semitone%s", inssel_kbd_jump, (inssel_kbd_jump == 1) ? "" : "s");
    SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Isomorphic keyboard", buf);
  }
  return 1;
}


/////////////////////////////////////////////////////////////////////////////
// Local LED handler function
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  // The GP row is NOT the play-surface (that lives on the B-row — see
  // SEQ_UI_INSSEL_KeyboardLeds / SEQ_UI_INSSEL_SelectRow_Button). Keep the stock
  // instrument cursor here.
  if( ui_cursor_flash ) // if flashing flag active: no LED flag set
    return 0;

  *gp_leds = 1 << ui_selected_instrument;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// B-row (INSTR sel-view) LED colours for the melodic keyboard play-surface.
// Fills <green> (in-scale keys) and <red> (root/tonic keys — render as amber where
// both planes overlap) and returns 1 when the melodic keyboard is active; returns
// 0 otherwise, so the caller falls back to the plain instrument cursor.
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_INSSEL_KeyboardLeds(u16 *green, u16 *red)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(track, SEQ_CC_MIDI_EVENT_MODE);

  if( !seq_ui_options.INSSEL_DRUM_TRIGGER || event_mode == SEQ_EVENT_MODE_Drum )
    return 0; // not the melodic keyboard surface

  u8 scale, root_selection, root;
  SEQ_CORE_FTS_GetScaleAndRoot(track, seq_core_trk[track].step, 0, &seq_cc_trk[track], &scale, &root_selection, &root);

  u16 g = 0x0000;
  u16 r = 0x0000;
  int key;
  for(key=0; key<16; ++key) {
    u8 notes[INSSEL_KBD_MAX_NOTES];
    if( !inssel_kbd_build_notes(track, (u8)key, notes) )
      continue;
    u8 n = notes[0];
    if( (s32)SEQ_SCALE_NoteValueGet(n, scale, root) == (s32)n )
      g |= (1 << key);            // note is a member of the scale -> green
    if( (n % 12) == (root % 12) )
      r |= (1 << key);            // tonic pitch class -> amber (green+red)
  }

  *green = g;
  *red = r;
  return 1;
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

  // Note: the melodic keyboard's Jump (GP1 enc) and scroll (datawheel) are handled
  // globally in INS sel-view (SEQ_UI_Encoder_Handler), so they work from any page —
  // the keyboard is played on the B-row via the sel-view, not only on this page.

  if( encoder <= SEQ_UI_ENCODER_GP16 ) {
    // select new layer/instrument

    if( encoder >= SEQ_TRG_NumInstrumentsGet(visible_track) )
      return -1;
    ui_selected_instrument = encoder;

    if( seq_hwcfg_button_beh.ins_sel ) {
      // if toggle function active: jump back to previous menu
      // this is especially useful for the emulated MBSEQ, where we can only click on a single button
      // (trigger gets deactivated when clicking on GP button or moving encoder)
      seq_ui_button_state.INS_SEL = 0;
      SEQ_UI_PageSet(ui_inssel_prev_page);
    }

    return 1; // value changed
  } else if( encoder == SEQ_UI_ENCODER_Datawheel ) {
    return SEQ_UI_Var8_Inc(&ui_selected_instrument, 0, SEQ_TRG_NumInstrumentsGet(visible_track)-1, incrementer);
  }

  return -1; // invalid or unsupported encoder
}


/////////////////////////////////////////////////////////////////////////////
// Live drum trigger
//
// When the "Instrument-Sel buttons play drums" option is enabled and a Drum
// track is selected, a GP button taps the matching drum instrument like a pad:
// preview is always heard, and the hit is recorded into the track when REC is
// armed. (Holding SELECT while tapping silently re-targets the selected
// instrument instead - handled in the button callback below.)
//
// Routing mirrors a live note arriving over MIDI-in (see seq_midi_in.c): record
// when armed (which also monitors per the FWD_MIDI option), otherwise preview.
/////////////////////////////////////////////////////////////////////////////
#define INSSEL_DRUM_TRIGGER_VELOCITY 100

static s32 SEQ_UI_INSSEL_DrumTrigger(u8 track, u8 drum, s32 depressed)
{
  if( drum >= SEQ_TRG_NumInstrumentsGet(track) )
    return -1; // no such drum instrument in this kit

  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = NoteOn;
  p.event = NoteOn;
  p.chn = tcc->midi_chn;
  p.note = tcc->lay_const[0*16 + drum] & 0x7f; // drum instrument note (SEQ_CC_LAY_CONST_A1 + drum); masked — lay_const comes from SD and SEQ_LIVE_PlayEvent indexes 128-wide arrays by note (#21)
  p.velocity = depressed ? 0x00 : INSSEL_DRUM_TRIGGER_VELOCITY;

  if( seq_record_state.ENABLED )
    SEQ_RECORD_Receive(p, track);
  else
    SEQ_LIVE_PlayEvent(track, p);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// One-row isomorphic keyboard — live note emission (state/builder above)
/////////////////////////////////////////////////////////////////////////////

// play one note live on the track's port/chn (preview / monitoring)
static void SEQ_UI_INSSEL_PlayLive(u8 track, u8 note, u8 velocity)
{
  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = NoteOn;
  p.event = NoteOn;
  p.chn = seq_cc_trk[track].midi_chn;
  p.note = note;
  p.velocity = velocity;
  SEQ_LIVE_PlayEvent(track, p);
}

// emit one note: record via the stock path when armed (dead-accurate for single
// notes, incl. length capture), else live preview
static void SEQ_UI_INSSEL_EmitNote(u8 track, u8 note, u8 velocity)
{
  if( seq_record_state.ENABLED ) {
    mios32_midi_package_t p;
    p.ALL = 0;
    p.type = NoteOn;
    p.event = NoteOn;
    p.chn = seq_cc_trk[track].midi_chn;
    p.note = note;
    p.velocity = velocity;
    SEQ_RECORD_Receive(p, track);
  } else {
    SEQ_UI_INSSEL_PlayLive(track, note, velocity);
  }
}

// Record a whole chord onto ONE step, atomically. The stock per-note record path
// (SEQ_RECORD_Receive x3) races the running sequencer: the step advances between
// the notes, scattering a triad across steps / dropping the last one. Here we pick
// the target step once (matching how single notes land - the record-quantize
// forward-snap while playing) and write all notes to that step's note layers under
// the MIDI-out mutex, so the chord always lands together.
static void SEQ_UI_INSSEL_RecordChord(u8 track, u8 *notes, u8 num_notes, u8 velocity)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_core_trk_t *t = &seq_core_trk[track];

  // determine the target step — under the MIDI-out mutex, so the +4 emission
  // task (which advances t->step / timestamp_next_step_ref while holding it)
  // can't move the step between the sample and the layer writes below (#23/#52)
  MUTEX_MIDIOUT_TAKE;

  u16 step;
  if( SEQ_BPM_IsRunning() && !seq_record_options.STEP_RECORD ) {
    step = t->step;
    // snap forward to the next step if we're within the record-quantize window
    u32 now = SEQ_BPM_TickGet();
    u8 shift = 0;
    if( t->timestamp_next_step_ref <= now ) {
      shift = 1;
    } else {
      s32 diff = (s32)t->timestamp_next_step_ref - (s32)now;
      s32 tolerance = ((s32)t->step_length * (s32)seq_record_quantize) / 100;
      if( diff < tolerance )
	shift = 1;
    }
    if( shift ) {
      int ns = (int)step + 1;
      if( ns > tcc->length )
	ns = tcc->loop;
      step = (u16)ns;
    }
  } else {
    step = ui_selected_step;
  }

  u8 num_p_layers = SEQ_PAR_NumLayersGet(track);
  u8 ni = 0;
  int pl;

  // fill the note-type layers with the chord notes; clear any extra note layers
  for(pl=0; pl<num_p_layers; ++pl) {
    u8 lt = tcc->lay_const[0*16 + pl];
    if( lt == SEQ_PAR_Type_Note || lt == SEQ_PAR_Type_Chord1 || lt == SEQ_PAR_Type_Chord2 || lt == SEQ_PAR_Type_Chord3 ) {
      SEQ_PAR_Set(track, step, pl, 0, (ni < num_notes) ? notes[ni] : 0x00);
      if( ni < num_notes )
	++ni;
    }
  }
  SEQ_TRG_GateSet(track, step, 0, 1);
  SEQ_TRG_AccentSet(track, step, 0, 0);
  if( tcc->link_par_layer_velocity >= 0 )
    SEQ_PAR_Set(track, step, tcc->link_par_layer_velocity, 0, velocity);
  MUTEX_MIDIOUT_GIVE;
}

static s32 SEQ_UI_INSSEL_KbdNote(u8 track, u8 key, s32 depressed)
{
  int i;

  if( depressed ) {
    // release exactly the notes we started (octave/scale/layout may have moved meanwhile).
    // A chord that was recorded atomically was only monitored live -> just stop the live
    // note; everything else goes back through the same path that started it.
    // Drain the per-key state atomically first — the physical-button and
    // MIDI-remote paths run in different tasks and can interleave on the same
    // key (#22); the note-off sends then work from the local snapshot.
    u8 as_chord;
    u8 held[INSSEL_KBD_MAX_NOTES];
    MIOS32_IRQ_Disable();
    as_chord = inssel_kbd_recorded_chord[key];
    inssel_kbd_recorded_chord[key] = 0;
    for(i=0; i<INSSEL_KBD_MAX_NOTES; ++i) {
      held[i] = inssel_kbd_held[key][i];
      inssel_kbd_held[key][i] = 0xff;
    }
    MIOS32_IRQ_Enable();
    for(i=0; i<INSSEL_KBD_MAX_NOTES; ++i) {
      if( held[i] != 0xff ) {
	if( as_chord )
	  SEQ_UI_INSSEL_PlayLive(track, held[i], 0x00);
	else
	  SEQ_UI_INSSEL_EmitNote(track, held[i], 0x00);
      }
    }
    return 0;
  }

  // press: build the note list for this key (shared with the LED/LCD view)
  u8 notes[INSSEL_KBD_MAX_NOTES];
  u8 num_notes = inssel_kbd_build_notes(track, key, notes);

  // store the exact notes (for note-off) — atomically, same cross-task
  // reentrancy as the release drain (#22)
  MIOS32_IRQ_Disable();
  for(i=0; i<INSSEL_KBD_MAX_NOTES; ++i)
    inssel_kbd_held[key][i] = 0xff;
  for(i=0; i<num_notes; ++i)
    inssel_kbd_held[key][i] = notes[i];
  MIOS32_IRQ_Enable();

  if( seq_record_state.ENABLED && num_notes > 1 ) {
    // recording a chord: write the whole chord to one step atomically (the stock
    // per-note record path races the running step and scatters/drops notes), then
    // monitor it live so you still hear what you played.
    SEQ_UI_INSSEL_RecordChord(track, notes, num_notes, INSSEL_KBD_TRIGGER_VELOCITY);
    inssel_kbd_recorded_chord[key] = 1;
    for(i=0; i<num_notes; ++i)
      SEQ_UI_INSSEL_PlayLive(track, notes[i], INSSEL_KBD_TRIGGER_VELOCITY);
  } else {
    // single note, or preview-only: the proven per-note path
    inssel_kbd_recorded_chord[key] = 0;
    for(i=0; i<num_notes; ++i)
      SEQ_UI_INSSEL_EmitNote(track, notes[i], INSSEL_KBD_TRIGGER_VELOCITY);
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Local button callback function
// Should return:
//   1 if value has been changed
//   0 if value hasn't been changed
//  -1 if invalid or unsupported button
/////////////////////////////////////////////////////////////////////////////
// B-row (INSTR sel-view) button handler — the live play-surface. Called from
// SEQ_UI_Button_DirectTrack when sel_view == INS. With the play option enabled the
// 16 B-row keys are drum pads / a keyboard; otherwise they select the instrument.
s32 SEQ_UI_INSSEL_SelectRow_Button(seq_ui_button_t button, s32 depressed)
{
  if( button > SEQ_UI_BUTTON_GP16 )
    return -1; // only the 16 select-row keys

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);

  // play surface (option enabled): the B-row keys play the track instead of selecting
  if( seq_ui_options.INSSEL_DRUM_TRIGGER ) {
    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      // Drum track: pad row. Holding INSTR while tapping silently re-targets the
      // selected instrument; a bare tap plays. INS_SEL is the hold-mode INSTR
      // latch (true only while the INSTR button is physically held), so this frees
      // SELECT and matches the stock "INSTR + key = pick instrument" muscle memory.
      if( seq_ui_button_state.INS_SEL ) {
	if( depressed ) return 0;
	if( (u8)button < SEQ_TRG_NumInstrumentsGet(visible_track) ) {
	  ui_selected_instrument = (u8)button;
	  return 1;
	}
	return 0;
      }
      // default: play (and record when armed) the drum like a pad
      return SEQ_UI_INSSEL_DrumTrigger(visible_track, (u8)button, depressed);
    } else {
      // Melodic track: one-row keyboard. SELECT + key1/key16 scroll the whole row
      // down/up by an OCTAVE (coarse); < / > + datawheel scroll by a semitone (fine).
      if( seq_ui_button_state.SELECT_PRESSED ) {
	if( depressed ) return 0;
	if( button == SEQ_UI_BUTTON_GP1 )  return SEQ_UI_INSSEL_KeyboardScroll(-12);
	if( button == SEQ_UI_BUTTON_GP16 ) return SEQ_UI_INSSEL_KeyboardScroll(+12);
	return 0;
      }
      return SEQ_UI_INSSEL_KbdNote(visible_track, (u8)button, depressed);
    }
  }

  // play option off: the B-row selects the instrument (stock, on release)
  if( depressed ) return 0;
  return Encoder_Handler(button, 0);
}


s32 SEQ_UI_INSSEL_Button_Handler(seq_ui_button_t button, s32 depressed)
{
  if( button <= SEQ_UI_BUTTON_GP16 ) {
    // GP row = instrument selector. The live play-surface (pads / keyboard) is on
    // the B-row (SEQ_UI_INSSEL_SelectRow_Button), so the GP row no longer triggers
    // notes — it acts on release like the stock instrument picker.
    if( depressed ) return 0;
    return Encoder_Handler(button, 0);
  }

  if( depressed ) return 0; // remaining buttons ignored when released

  switch( button ) {
    case SEQ_UI_BUTTON_Select:
      return -1; // unsupported (yet)

    // < / > (physical nav keys) scroll the keyboard a semitone via the datawheel path
    case SEQ_UI_BUTTON_Right:
    case SEQ_UI_BUTTON_Up:
      return Encoder_Handler(SEQ_UI_ENCODER_Datawheel, 1);

    case SEQ_UI_BUTTON_Left:
    case SEQ_UI_BUTTON_Down:
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
  // layout drum mode (lower line shows drum labels):
  // 00000000001111111111222222222233333333330000000000111111111122222222223333333333
  // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  // <--------------------------------------><-------------------------------------->
  // Select Drum Instrument:                                                         
  //  BD   SD   LT   MT   HT   CP   MA   RS   CB   CY   OH   CH  Smp1 Smp2 Smp3 Smp4 
  // ...horizontal VU meters...

  u8 visible_track = SEQ_UI_VisibleTrackGet();
  u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);

  // Melodic play-surface: a dedicated keyboard readout replaces the instrument
  // list/VU (meaningless for a keyboard) — status on line 0, per-key note names
  // on line 1.
  if( seq_ui_options.INSSEL_DRUM_TRIGGER && event_mode != SEQ_EVENT_MODE_Drum ) {
    if( high_prio )
      return 0; // keyboard view is static between key events; no VU churn

    u8 scale, root_selection, root;
    SEQ_CORE_FTS_GetScaleAndRoot(visible_track, seq_core_trk[visible_track].step, 0, &seq_cc_trk[visible_track], &scale, &root_selection, &root);

    // line 0, left half (cols 0..39): layout + jump + base (leftmost key) note
    u8 k0[INSSEL_KBD_MAX_NOTES];
    inssel_kbd_build_notes(visible_track, 0, k0);
    SEQ_LCD_CursorSet(0, 0);
    if( seq_ui_options.INSSEL_KBD_CHORD )
      SEQ_LCD_PrintStringPadded("Chords", 12);
    else if( seq_ui_options.INSSEL_KBD_SCALE_DEGREE )
      SEQ_LCD_PrintStringPadded("Degrees", 12);
    else {
      char b[13];
      sprintf(b, "Iso Jump+%d", inssel_kbd_jump);
      SEQ_LCD_PrintStringPadded(b, 12);
    }
    SEQ_LCD_PrintString("Base:");
    SEQ_LCD_PrintNote(k0[0]);
    SEQ_LCD_PrintSpaces(20);

    // line 0, right half (cols 40..79): live scale name
    SEQ_LCD_CursorSet(40, 0);
    SEQ_LCD_PrintString("Scale:");
    SEQ_LCD_PrintStringPadded(SEQ_SCALE_NameGet(scale), 34);

    // line 1: the 16 keys' primary note names (3 chars + 2 spaces = 5, x16 = 80)
    SEQ_LCD_CursorSet(0, 1);
    int key;
    for(key=0; key<16; ++key) {
      u8 kn[INSSEL_KBD_MAX_NOTES];
      inssel_kbd_build_notes(visible_track, (u8)key, kn);
      SEQ_LCD_PrintNote(kn[0]);
      SEQ_LCD_PrintString("  ");
    }

    return 0; // no error
  }

  if( high_prio ) {
    ///////////////////////////////////////////////////////////////////////////
    // frequently update VU meters

    SEQ_LCD_CursorSet(0, 1);

    if( event_mode == SEQ_EVENT_MODE_Drum ) {
      u8 drum;
      u8 num_instruments = SEQ_TRG_NumInstrumentsGet(visible_track);
      for(drum=0; drum<num_instruments; ++drum) {
	if( seq_core_trk[visible_track].layer_muted & (1 << drum) )
	  SEQ_LCD_PrintString("Mute ");
	else
	  SEQ_LCD_PrintHBar((seq_layer_vu_meter[drum] >> 3) & 0xf);
      }
    } else {
      seq_core_trk_t *t = &seq_core_trk[visible_track];
      u16 mask = 1 << visible_track;

      if( seq_core_trk_muted & mask ) {
	SEQ_LCD_PrintString("Mute ");
      } else {
	SEQ_LCD_PrintHBar(t->vu_meter >> 3);
      }
    }

    return 0; // no error
  }

  u8 num_instruments = SEQ_TRG_NumInstrumentsGet(visible_track);

  ///////////////////////////////////////////////////////////////////////////
  SEQ_LCD_CursorSet(0, 0);

  int i;
  for(i=0; i<num_instruments; ++i) {
    if( i == ui_selected_instrument && ui_cursor_flash ) {
      SEQ_LCD_PrintSpaces(5);
    } else {
      if( event_mode == SEQ_EVENT_MODE_Drum ) {
	SEQ_LCD_PrintTrackDrum(visible_track, i, (char *)seq_core_trk[visible_track].name);
      } else {
	SEQ_LCD_PrintFormattedString("INS%2d", i+1);
      }
    }
  }
    
  SEQ_LCD_PrintSpaces(80 - (5*num_instruments));

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_UI_INSSEL_Init(u32 mode)
{
  // install callback routines
  SEQ_UI_InstallButtonCallback(SEQ_UI_INSSEL_Button_Handler);
  SEQ_UI_InstallEncoderCallback(Encoder_Handler);
  SEQ_UI_InstallLEDCallback(LED_Handler);
  SEQ_UI_InstallLCDCallback(LCD_Handler);

  // we want to show horizontal VU meters
  SEQ_LCD_InitSpecialChars(SEQ_LCD_CHARSET_HBars);

  // mark all keyboard keys as not-held (0xff), so a release never emits a phantom note
  {
    int k, i;
    for(k=0; k<16; ++k) {
      inssel_kbd_recorded_chord[k] = 0;
      for(i=0; i<INSSEL_KBD_MAX_NOTES; ++i)
	inssel_kbd_held[k][i] = 0xff;
    }
  }

  return 0; // no error
}
