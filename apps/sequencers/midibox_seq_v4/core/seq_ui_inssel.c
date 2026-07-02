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
// Local LED handler function
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  if( ui_cursor_flash ) // if flashing flag active: no LED flag set
    return 0;

  *gp_leds = 1 << ui_selected_instrument;

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
// One-row isomorphic keyboard (melodic tracks)
//
// When the play option is enabled and a melodic (non-Drum) track is selected,
// the 16 GP buttons become a playable row. Three layouts (OPT "Melodic keyboard
// layout"):
//   - Chromatic (isomorphic): GP1 = base, each next button +1 semitone.
//   - Scale degrees: GP1 = the track's tonic, each next button = the next note
//     of the track's scale.
//   - Diatonic chords: GP_k = the in-key triad stacked on scale degree k
//     (degrees k, k+2, k+4) - up to 3 notes per button.
// Scale/root are read live via SEQ_CORE_FTS_GetScaleAndRoot. Plays like a pad
// (preview always; records into the track when REC is armed) via the same
// MIDI-in-style routing as the drum surface, so the track's transpose /
// force-to-scale / FX apply to both the live preview and playback. SELECT +
// GP1/GP16 shift the whole row by an octave.
/////////////////////////////////////////////////////////////////////////////
#define INSSEL_KBD_BASE_NOTE 0x3c            // GP1 at octave offset 0 (middle C); multiple of 12
#define INSSEL_KBD_TRIGGER_VELOCITY 100
#define INSSEL_KBD_MAX_NOTES 3               // up to a triad per key

static s8 inssel_kbd_octave = 0;             // -5..+5, shifted via SELECT + GP1/GP16
static u8 inssel_kbd_held[16][INSSEL_KBD_MAX_NOTES]; // notes held per key (0xff = empty), for note-off
static u8 inssel_kbd_recorded_chord[16];     // 1 = this key's press used the atomic chord recorder (release just stops the live monitor)

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
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
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

  // press: build the note list for this key
  u8 notes[INSSEL_KBD_MAX_NOTES];
  u8 num_notes = 0;

  if( seq_ui_options.INSSEL_KBD_SCALE_DEGREE || seq_ui_options.INSSEL_KBD_CHORD ) {
    u8 scale, root_selection, root;
    SEQ_CORE_FTS_GetScaleAndRoot(track, seq_core_trk[track].step, 0, tcc, &scale, &root_selection, &root);
    int base = (int)INSSEL_KBD_BASE_NOTE + 12*(int)inssel_kbd_octave + (int)root; // tonic in anchor octave
    if( base < 0 )   base = 0;
    if( base > 127 ) base = 127;

    if( seq_ui_options.INSSEL_KBD_CHORD ) {
      // diatonic triad on scale degree 'key' (degrees key, key+2, key+4)
      notes[num_notes++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)key);
      notes[num_notes++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)(key + 2));
      notes[num_notes++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)(key + 4));
    } else {
      // single scale degree: GP_k = k scale steps above the tonic
      notes[num_notes++] = (u8)SEQ_SCALE_WalkScale((u8)base, scale, root, (s8)key);
    }
  } else {
    // chromatic isomorphic row: GP1 = base, +1 semitone per button
    int n = (int)INSSEL_KBD_BASE_NOTE + 12*(int)inssel_kbd_octave + (int)key;
    if( n < 0 )   n = 0;
    if( n > 127 ) n = 127;
    notes[num_notes++] = (u8)n;
  }

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
s32 SEQ_UI_INSSEL_Button_Handler(seq_ui_button_t button, s32 depressed)
{
#if 0
  // leads to: comparison is always true due to limited range of data type
  if( button >= SEQ_UI_BUTTON_GP1 && button <= SEQ_UI_BUTTON_GP16 ) {
#else
  if( button <= SEQ_UI_BUTTON_GP16 ) {
#endif
    u8 visible_track = SEQ_UI_VisibleTrackGet();
    u8 event_mode = SEQ_CC_Get(visible_track, SEQ_CC_MIDI_EVENT_MODE);

    // play surface (option enabled): GP buttons play the track instead of selecting
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
	// Melodic track: one-row keyboard. SELECT + GP1/GP16 shift the whole row
	// down/up an octave (press only). Pop a transient readout so the shift is
	// visible - it is otherwise silent until the next note is played.
	if( seq_ui_button_state.SELECT_PRESSED ) {
	  if( depressed ) return 0;
	  s8 prev_octave = inssel_kbd_octave;
	  if( button == SEQ_UI_BUTTON_GP1  && inssel_kbd_octave > -5 ) --inssel_kbd_octave;
	  if( button == SEQ_UI_BUTTON_GP16 && inssel_kbd_octave <  5 ) ++inssel_kbd_octave;
	  if( inssel_kbd_octave != prev_octave ) {
	    char buf[21];
	    int mag = (inssel_kbd_octave < 0) ? -inssel_kbd_octave : inssel_kbd_octave;
	    sprintf(buf, "Octave %c%d", (inssel_kbd_octave < 0) ? '-' : '+', mag);
	    SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Keyboard transpose", buf);
	    return 1;
	  }
	  return 0;
	}
	return SEQ_UI_INSSEL_KbdNote(visible_track, (u8)button, depressed);
      }
    }

    // classic behaviour: select instrument (act on press only)
    if( depressed ) return 0;
    return Encoder_Handler(button, 0);
  }

  if( depressed ) return 0; // remaining buttons ignored when released

  switch( button ) {
    case SEQ_UI_BUTTON_Select:
      return -1; // unsupported (yet)

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
