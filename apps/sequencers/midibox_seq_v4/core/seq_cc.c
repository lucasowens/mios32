// $Id$
/*
 * CC layer
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

#include "seq_ui.h"
#include "seq_core.h"
#include "seq_cc.h"
#include "seq_par.h"
#include "seq_layer.h"
#include "seq_morph.h"


/////////////////////////////////////////////////////////////////////////////
// global variables
/////////////////////////////////////////////////////////////////////////////
seq_cc_trk_t seq_cc_trk[SEQ_CORE_NUM_TRACKS];


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_Init(u32 mode)
{
  // initialize all CC parameters
  int track;
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track) {
    seq_cc_trk_t *tcc = &seq_cc_trk[track];

    // clear all CCs
    memset((u8 *)tcc, 0, sizeof(seq_cc_trk_t));

    // initialize robotize probabilities to full
    tcc->robotize_skip_probability = 0;
    tcc->robotize_note_probability = 31;
    tcc->robotize_oct_probability = 31;
    tcc->robotize_vel_probability = 31;
    tcc->robotize_len_probability = 31;

	// special robotize probabilities to zero
    tcc->robotize_sustain_probability = 0;
    tcc->robotize_nofx_probability = 0;
    tcc->robotize_echo_probability = 0;
    tcc->robotize_duplicate_probability = 0;
    
    // initialize robotize mask to all steps selected
    tcc->robotize_mask1 = 0xFF;
    tcc->robotize_mask2 = 0xFF;

    // robotize loop/reseed off by default; xorshift32 substitutes a non-zero
    // default when state is zero so the actual anchor values don't matter here
    tcc->robotize_loop_cycles = 0;
    tcc->robotize_sync_to_master = 0;
    tcc->robotize_palette_length = 16;
    tcc->robotize_loop_start = 0;
    tcc->robotize_loop_rotate = 0;
    tcc->chordmask_strength = 0;
    tcc->chordmask_bus      = 0;
    tcc->chordmask_drum_l   = 0xFF;
    tcc->chordmask_drum_h   = 0xFF;
    tcc->tension_grip       = 0;

    {
      u8 i;
      for(i=0; i<16; ++i)
        tcc->robotize_bar_anchors[i] = 0;
    }


#ifndef MBSEQV4L
    // set parameters which are not changed by SEQ_LAYER_CopyPreset() function
    tcc->midi_chn = track % 16;
    tcc->midi_port = DEFAULT;
    tcc->fx_midi_chn = 0;
    tcc->fx_midi_port = DEFAULT;
    tcc->fx_midi_num_chn = 0; // off!
    tcc->event_mode = SEQ_EVENT_MODE_Note;
#else
    // extra for MBSEQ V4L: use same channel by default
    tcc->midi_chn = 0;
    tcc->midi_port = DEFAULT;
    tcc->fx_midi_chn = 0;
    tcc->fx_midi_port = DEFAULT;
    tcc->fx_midi_num_chn = 0; // off!



    // set combined mode for first 3 tracks of a sequence
    // set all other tracks to CC
    if( (track >= 0 && track <= 2) || (track >= 8 && track <= 10) ) {
      tcc->event_mode = SEQ_EVENT_MODE_Combined;
    } else {
      tcc->event_mode = SEQ_EVENT_MODE_CC;
    }
#endif
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Reset every generative CC on a track to its neutral default (matching
// SEQ_CC_Init). Called by the capture verbs (SEQ_CORE_CaptureToSlot /
// SEQ_CORE_CaptureToTrack, seq_core.c) on the destination CC after the
// computed output is written — so the captured pattern plays the frozen tape
// without re-applying robotize / echo / direction / bus / par-layer modulation
// on top.
//
// Two axes, only one is reset. A capture commits the GENERATION axis (generators,
// randomness, mutation) into notes — that is correctly neutralized here. The
// SHAPING axis (groove, …) is DETERMINISTIC: re-applying its CC on playback
// reproduces the heard sound exactly (groove's per-step swing/velocity/length —
// including negative delays that cannot be baked into step params), so it must be
// PRESERVED, not reset. Groove is the first such carve-out; transpose/scale/echo/
// LFO/direction are candidates for the same treatment (preserve), pending by-ear
// review — left reset for now.
//
// Preserved (identity + structural + step-data carriers + deterministic shaping):
//   - MIDI port/channel, event_mode, MIDI bank/PC, name
//   - length, loop, clock divider (incl. TRIPLETS + MANUAL bits)
//   - lay_const slots holding Note/Chord/Velocity/Length/CC/PB/PC/AT/Ctrl
//   - par_assignment_drum slots holding Velocity/Length
//   - groove (style + value) — deterministic shaping, re-applied identically
//
// When you add a new SEQ_CC_*, classify it: GENERATION → reset here; deterministic
// SHAPING → preserve (a frozen copy must still sound like what was heard).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_ResetGenerativeForBounce(u8 track)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1;

  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  portENTER_CRITICAL();

  // Track mode -> Normal; clears Off / Transpose / Arpeggiator. Bounced tape
  // is just notes, no live transposer/arp input wanted.
  tcc->playmode = SEQ_CORE_TRKMODE_Normal;
  // Mode flags: clears FORCE_SCALE + SUSTAIN + UNSORTED/HOLD/RESTART/FIRST_NOTE/STEP_TRG.
  tcc->trkmode_flags.ALL = 0;
  // Bus assignment -> bus 0.
  tcc->busasg.ALL = 0;
  // Clock-divider flags: clear synch-to-measure only. TRIPLETS + MANUAL are
  // clock-shape (structural), not generative — leave them alone.
  tcc->clkdiv.SYNCH_TO_MEASURE = 0;

  // Note-value limits off.
  tcc->limit_lower = 0;
  tcc->limit_upper = 0;

  // Direction & step traversal -> linear forward.
  tcc->dir_mode = SEQ_CORE_TRKDIR_Forward;
  tcc->steps_replay = 0;
  tcc->steps_forward = 0;
  tcc->steps_jump_back = 0;
  tcc->steps_repeat = 0;
  tcc->steps_skip = 0;
  tcc->steps_rs_interval = 0;

  // Transpose / morph / humanize.
  // NOTE: groove (groove_style + groove_value) is deliberately NOT reset here —
  // it is deterministic shaping, preserved so the frozen copy re-grooves
  // identically to the source (see the axis split in the header comment). The
  // earlier reset stripped the swing/velocity/length groove off every capture,
  // making a grooved drum track sound dead-straight once frozen.
  tcc->transpose_semi = 0;
  tcc->transpose_oct = 0;
  tcc->morph_mode = 0;
  tcc->morph_dst = 0;
  tcc->humanize_value = 0;
  tcc->humanize_mode = 0;

  // Trigger assignments: neutralize ONLY the generative trigger functions
  // (random gate / random value) so the frozen capture plays deterministically.
  // PRESERVE the structural assignments (gate/accent/glide/roll/skip/no_fx/
  // roll_gate) — these map functions to trigger LAYERS and are structural, like
  // the Note/Velocity/Length par-layer assignments preserved below. Clearing the
  // GATE assignment leaves the track with no gate layer, which MBSEQ plays as
  // "every step on" — the bug that filled bounced patterns with a note on every
  // step (gates appeared unset yet every step fired, CLEAR had no effect).
  tcc->trg_assignments.random_gate = 0;
  tcc->trg_assignments.random_value = 0;

  // Echo off.
  tcc->echo_repeats = 0;
  tcc->echo_delay = 0;
  tcc->echo_velocity = 0;
  tcc->echo_fb_velocity = 0;
  tcc->echo_fb_note = 0;
  tcc->echo_fb_gatelength = 0;
  tcc->echo_fb_ticks = 0;

  // LFO off.
  tcc->lfo_waveform = 0;
  tcc->lfo_amplitude = 0;
  tcc->lfo_phase = 0;
  tcc->lfo_steps = 0;
  tcc->lfo_steps_rst = 0;
  tcc->lfo_enable_flags.ALL = 0;
  tcc->lfo_cc = 0;
  tcc->lfo_cc_offset = 0;
  tcc->lfo_cc_ppqn = 0;

  // Robotize neutral — mirrors SEQ_CC_Init. *_probability defaults are 31
  // (full range) so a later re-enable behaves like a fresh track; ACTIVE=0
  // keeps the engine idle on the destination.
  tcc->robotize_active = 0;
  tcc->robotize_probability = 0;
  tcc->robotize_note = 0;
  tcc->robotize_oct = 0;
  tcc->robotize_vel = 0;
  tcc->robotize_len = 0;
  tcc->robotize_skip_probability = 0;
  tcc->robotize_note_probability = 31;
  tcc->robotize_oct_probability = 31;
  tcc->robotize_vel_probability = 31;
  tcc->robotize_len_probability = 31;
  tcc->robotize_sustain_probability = 0;
  tcc->robotize_nofx_probability = 0;
  tcc->robotize_echo_probability = 0;
  tcc->robotize_duplicate_probability = 0;
  tcc->robotize_mask1 = 0xFF;
  tcc->robotize_mask2 = 0xFF;
  tcc->robotize_loop_cycles = 0;
  tcc->robotize_sync_to_master = 0;
  tcc->robotize_palette_length = 16;
  tcc->robotize_loop_start = 0;
  tcc->robotize_loop_rotate = 0;
  tcc->chordmask_strength = 0;
  tcc->chordmask_bus      = 0;
  tcc->chordmask_drum_l   = 0xFF;
  tcc->chordmask_drum_h   = 0xFF;
  tcc->tension_grip       = 0;
  {
    u8 i;
    for(i=0; i<16; ++i)
      tcc->robotize_bar_anchors[i] = 0;
  }

  // FX MIDI duplicate off.
  tcc->fx_midi_mode.ALL = 0;
  tcc->fx_midi_port = DEFAULT;
  tcc->fx_midi_chn = 0;
  tcc->fx_midi_num_chn = 0;

  // Parameter-layer assignments: change generative types (Probability, Delay,
  // Roll, Roll2, Nth1, Nth2, Root, Scale) to None so they don't transform the
  // captured tape. Note/Chord/Velocity/Length/CC/PB/PC/AT/Ctrl are preserved
  // since they carry step data. Non-drum stores types in lay_const[0..15];
  // drum stores them in par_assignment_drum[0..3].
  {
    u8 *par_asg;
    u8 num_layers;
    if( tcc->event_mode == SEQ_EVENT_MODE_Drum ) {
      par_asg = &tcc->par_assignment_drum[0];
      num_layers = 4;
    } else {
      par_asg = &tcc->lay_const[0];
      num_layers = 16;
    }
    u8 i;
    for(i=0; i<num_layers; ++i) {
      switch( (seq_par_layer_type_t)par_asg[i] ) {
        case SEQ_PAR_Type_Probability:
        case SEQ_PAR_Type_Delay:
        case SEQ_PAR_Type_Roll:
        case SEQ_PAR_Type_Roll2:
        case SEQ_PAR_Type_Nth1:
        case SEQ_PAR_Type_Nth2:
        case SEQ_PAR_Type_Root:
        case SEQ_PAR_Type_Scale:
          par_asg[i] = SEQ_PAR_Type_None;
          break;
        default:
          break;
      }
    }
  }

  portEXIT_CRITICAL();

  // Re-derive link_par_layer_* now that some assignments may have changed.
  SEQ_CC_LinkUpdate(track);

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Set CCs
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_Set(u8 track, u8 cc, u8 value)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1; // invalid track

  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  // since CCs can be modified from other tasks at different priority we should do this operation atomic
  portENTER_CRITICAL();

  if( cc < 0x30 ) {
    tcc->lay_const[cc] = value;
    if( tcc->event_mode != SEQ_EVENT_MODE_Drum )
      SEQ_CC_LinkUpdate(track);
  } else {
    switch( cc ) {
      case SEQ_CC_MODE:
	tcc->playmode = value;
	SEQ_CORE_ChordMaskSlotSync(track); // ChordMask ↔ slot 0 (phase C bridge)
	SEQ_CORE_TensionSlotSync(track);   // keep the tension slot consistent too
	break;
      case SEQ_CC_MODE_FLAGS: tcc->trkmode_flags.ALL = value; break;
  
      case SEQ_CC_MIDI_EVENT_MODE: 
	tcc->event_mode = value; 
	SEQ_CC_LinkUpdate(track);
	break;

      case SEQ_CC_MIDI_CHANNEL: tcc->midi_chn = value; break;
      case SEQ_CC_MIDI_PORT: tcc->midi_port = value; break;
      case SEQ_CC_MIDI_PC: tcc->midi_pc = value; break;
      case SEQ_CC_MIDI_BANK_L: tcc->midi_bank_l = value; break;
      case SEQ_CC_MIDI_BANK_H: tcc->midi_bank_h = value; break;

      case SEQ_CC_FX_MIDI_MODE: tcc->fx_midi_mode.ALL = value; break;
      case SEQ_CC_FX_MIDI_PORT: tcc->fx_midi_port = value; break;
      case SEQ_CC_FX_MIDI_CHANNEL: tcc->fx_midi_chn = value; break;
      case SEQ_CC_FX_MIDI_NUM_CHANNELS: tcc->fx_midi_num_chn = value; break;

      case SEQ_CC_BUSASG:
	tcc->busasg.bus = value;
	// Phase G/step-7 polish: BUSASG no longer drives the chord_mask slot's
	// bus — the processor reads tcc->chordmask_bus instead (independent of
	// the track's own bus assignment).
	break;

      case SEQ_CC_LIMIT_LOWER: tcc->limit_lower = value; break;
      case SEQ_CC_LIMIT_UPPER: tcc->limit_upper = value; break;
    
      case SEQ_CC_DIRECTION: tcc->dir_mode = value; break;
      case SEQ_CC_STEPS_REPLAY: tcc->steps_replay = value; break;
      case SEQ_CC_STEPS_FORWARD: tcc->steps_forward = value; break;
      case SEQ_CC_STEPS_JMPBCK: tcc->steps_jump_back = value; break;
      case SEQ_CC_CLK_DIVIDER: tcc->clkdiv.value = value; break;

      case SEQ_CC_LENGTH:
	// ensure that step position pointer matches with the new track length
	tcc->length = value;
	seq_core_trk[track].step %= ((u16)value+1);
	break;

      case SEQ_CC_LOOP: tcc->loop = value; break;
      case SEQ_CC_CLKDIV_FLAGS: tcc->clkdiv.flags = value; break;
    
      case SEQ_CC_TRANSPOSE_SEMI: tcc->transpose_semi = value; break;
      case SEQ_CC_TRANSPOSE_OCT: tcc->transpose_oct = value; break;
      case SEQ_CC_GROOVE_VALUE: tcc->groove_value = value; break;
      case SEQ_CC_GROOVE_STYLE: tcc->groove_style.ALL = value; break;
      case SEQ_CC_MORPH_MODE: tcc->morph_mode = value; break;
      case SEQ_CC_MORPH_DST: tcc->morph_dst = value; break;
      case SEQ_CC_HUMANIZE_VALUE: tcc->humanize_value = value; break;
      case SEQ_CC_HUMANIZE_MODE: tcc->humanize_mode = value; break;
    
      case SEQ_CC_ASG_GATE: tcc->trg_assignments.gate = value; break;
      case SEQ_CC_ASG_ACCENT: tcc->trg_assignments.accent = value; break;
      case SEQ_CC_ASG_ROLL: tcc->trg_assignments.roll = value; break;
      case SEQ_CC_ASG_GLIDE: tcc->trg_assignments.glide = value; break;
      case SEQ_CC_ASG_SKIP: tcc->trg_assignments.skip = value; break;
      case SEQ_CC_ASG_RANDOM_GATE: tcc->trg_assignments.random_gate = value; break;
      case SEQ_CC_ASG_RANDOM_VALUE: tcc->trg_assignments.random_value = value; break;
      case SEQ_CC_ASG_NO_FX: tcc->trg_assignments.no_fx = value; break;
      case SEQ_CC_ASG_ROLL_GATE: tcc->trg_assignments.roll_gate = value; break;

      case SEQ_CC_PAR_ASG_DRUM_LAYER_A:
	tcc->par_assignment_drum[0] = value;
	SEQ_CC_LinkUpdate(track);
	break;
      case SEQ_CC_PAR_ASG_DRUM_LAYER_B:
	tcc->par_assignment_drum[1] = value;
	SEQ_CC_LinkUpdate(track);
	break;
      case SEQ_CC_PAR_ASG_DRUM_LAYER_C:
	tcc->par_assignment_drum[2] = value;
	SEQ_CC_LinkUpdate(track);
	break;
      case SEQ_CC_PAR_ASG_DRUM_LAYER_D:
	tcc->par_assignment_drum[3] = value;
	SEQ_CC_LinkUpdate(track);
	break;

      case SEQ_CC_STEPS_REPEAT: tcc->steps_repeat = value; break;
      case SEQ_CC_STEPS_SKIP: tcc->steps_skip = value; break;
      case SEQ_CC_STEPS_RS_INTERVAL: tcc->steps_rs_interval = value; break;

#if 0  
      case SEQ_CC_CHANGE_STEP: break; // TODO
#endif
    
      case SEQ_CC_ECHO_REPEATS: tcc->echo_repeats = value; break;
      case SEQ_CC_ECHO_DELAY: tcc->echo_delay = value; break;
      case SEQ_CC_ECHO_VELOCITY: tcc->echo_velocity = value; break;
      case SEQ_CC_ECHO_FB_VELOCITY: tcc->echo_fb_velocity = value; break;
      case SEQ_CC_ECHO_FB_NOTE: tcc->echo_fb_note = value; break;
      case SEQ_CC_ECHO_FB_GATELENGTH: tcc->echo_fb_gatelength = value; break;
      case SEQ_CC_ECHO_FB_TICKS: tcc->echo_fb_ticks = value; break;

      case SEQ_CC_LFO_WAVEFORM: tcc->lfo_waveform = value; break;
      case SEQ_CC_LFO_AMPLITUDE: tcc->lfo_amplitude = value; break;
      case SEQ_CC_LFO_PHASE: tcc->lfo_phase = value; break;
      case SEQ_CC_LFO_STEPS: tcc->lfo_steps = value; break;
      case SEQ_CC_LFO_STEPS_RST: tcc->lfo_steps_rst = value; break;
      case SEQ_CC_LFO_ENABLE_FLAGS: tcc->lfo_enable_flags.ALL = value; break;
      case SEQ_CC_LFO_CC: tcc->lfo_cc = value; break;
      case SEQ_CC_LFO_CC_OFFSET: tcc->lfo_cc_offset = value; break;
      case SEQ_CC_LFO_CC_PPQN: tcc->lfo_cc_ppqn = value; break;

      case SEQ_CC_ROBOTIZE_PROBABILITY: tcc->robotize_probability = value; break;
      case SEQ_CC_ROBOTIZE_NOTE: tcc->robotize_note = value; break;
      case SEQ_CC_ROBOTIZE_OCT: tcc->robotize_oct = value; break;
      case SEQ_CC_ROBOTIZE_LEN: tcc->robotize_len = value; break;
      case SEQ_CC_ROBOTIZE_VEL: tcc->robotize_vel = value; break;

      case SEQ_CC_ROBOTIZE_SKIP_PROBABILITY: tcc->robotize_skip_probability = value; break;
      case SEQ_CC_ROBOTIZE_NOTE_PROBABILITY: tcc->robotize_note_probability = value; break;
      case SEQ_CC_ROBOTIZE_OCT_PROBABILITY: tcc->robotize_oct_probability = value; break;
      case SEQ_CC_ROBOTIZE_LEN_PROBABILITY: tcc->robotize_len_probability = value; break;
      case SEQ_CC_ROBOTIZE_VEL_PROBABILITY: tcc->robotize_vel_probability = value; break;
      case SEQ_CC_ROBOTIZE_SUSTAIN_PROBABILITY: tcc->robotize_sustain_probability = value; break;
      case SEQ_CC_ROBOTIZE_NOFX_PROBABILITY: tcc->robotize_nofx_probability = value; break;
      case SEQ_CC_ROBOTIZE_ECHO_PROBABILITY: tcc->robotize_echo_probability = value; break;
      case SEQ_CC_ROBOTIZE_DUPLICATE_PROBABILITY: tcc->robotize_duplicate_probability = value; break;
  

      case SEQ_CC_ROBOTIZE_ACTIVE: tcc->robotize_active = value; break;
      case SEQ_CC_ROBOTIZE_MASK1: tcc->robotize_mask1 = value; break;
      case SEQ_CC_ROBOTIZE_MASK2: tcc->robotize_mask2 = value; break;
      case SEQ_CC_ROBOTIZE_LOOP_CYCLES: tcc->robotize_loop_cycles = value; break;
      case SEQ_CC_ROBOTIZE_SYNC_TO_MASTER: tcc->robotize_sync_to_master = value; break;
      case SEQ_CC_ROBOTIZE_PALETTE_LENGTH:
	tcc->robotize_palette_length = (value < 1) ? 1 : (value > 16 ? 16 : value);
	break;
      case SEQ_CC_ROBOTIZE_LOOP_START:  tcc->robotize_loop_start  = value & 0x0f; break;
      case SEQ_CC_ROBOTIZE_LOOP_ROTATE: tcc->robotize_loop_rotate = value & 0x0f; break;
      case SEQ_CC_CHORDMASK_STRENGTH:
	tcc->chordmask_strength = value & 0x7f;
	SEQ_CORE_ChordMaskSlotSync(track); // strength follows tcc when ChordMask active
	break;
      case SEQ_CC_CHORDMASK_BUS:
	tcc->chordmask_bus = value & 0x03; // 4 buses (0..3)
	SEQ_CORE_ChordMaskSlotSync(track);
	SEQ_CORE_TensionSlotSync(track); // tension shares the chord-context bus
	break;
      case SEQ_CC_CHORDMASK_DRUM_L:
	tcc->chordmask_drum_l = value & 0xff;
	SEQ_CORE_ChordMaskSlotSync(track);
	SEQ_CORE_TensionSlotSync(track);
	break;
      case SEQ_CC_CHORDMASK_DRUM_H:
	tcc->chordmask_drum_h = value & 0xff;
	SEQ_CORE_ChordMaskSlotSync(track);
	SEQ_CORE_TensionSlotSync(track);
	break;
      case SEQ_CC_TENSION_GRIP:
	tcc->tension_grip = value & 0x7f;
	SEQ_CORE_TensionSlotSync(track); // GRIP enables/scales the field slot
	break;

      default:
	portEXIT_CRITICAL();
        return -2; // invalid CC
    }
  }

  portEXIT_CRITICAL();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Set CCs via MIDI (different mapping, especially used by Loopback Feature)
// see also doc/mbseqv4_cc_implementation.txt
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_MIDI_Set(u8 track, u8 cc, u8 value)
{
  if( cc == 0x01 ) { // ModWheel -> Morph Value
#ifndef MBSEQV4L
    // update screen immediately if in morph page
    if( ui_page == SEQ_UI_PAGE_TRKMORPH )
      seq_ui_display_update_req = 1;
#endif
    // forward morph value
    return SEQ_MORPH_ValueSet(value);
  } else if( cc == 0x03 ) {
    seq_core_global_scale = value;
    return 1;
  } else if( cc >= 0x10 && cc <= 0x5f ) {
    u8 mapped_cc = cc+0x20;

    switch( mapped_cc ) {
      case SEQ_CC_LFO_AMPLITUDE:
	value *= 2; // 7bit -> 8bit
	break;
      case SEQ_CC_MIDI_PORT:
      case SEQ_CC_FX_MIDI_PORT:
	if( value >= 0x70 )
	  value = 0xf0 | (value & 0x0f); // map to Bus
	else if( value >= 0x60 )
	  value = 0x80 | (value & 0x0f); // map to AOUT
	break;
    }

    return SEQ_CC_Set(track, mapped_cc, value); // 0x10..0x5f -> 0x30..0x7f
  }

  return -1; // CC not mapped
}


/////////////////////////////////////////////////////////////////////////////
// Returns the CC value for MIDI (different mapping, especially used by Loopback Feature)
// see also doc/mbseqv4_cc_implementation.txt
// Returns < 0 if CC value not mapped
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_MIDI_Get(u8 track, u8 cc, u8 *mapped_cc)
{
  if( cc >= 0x30 && cc <= 0x7f ) {
    *mapped_cc = cc - 0x20;

    u8 value = SEQ_CC_Get(track, cc);
    switch( cc ) {
      case SEQ_CC_LFO_AMPLITUDE:
	value /= 2; // 8bit -> 7bit
	break;
      case SEQ_CC_MIDI_PORT:
      case SEQ_CC_FX_MIDI_PORT:
	if( value >= 0xf0 )
	  value = 0x70 | (value & 0x0f); // map to Bus
	else if( value >= 0x80 )
	  value = 0x60 | (value & 0x0f); // map to AOUT
	break;
    }

    return value;
  }

  return -1; // CC not mapped
}


/////////////////////////////////////////////////////////////////////////////
// Get CCs
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_Get(u8 track, u8 cc)
{
  if( track >= SEQ_CORE_NUM_TRACKS )
    return -1; // invalid track

  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  if( cc < 0x30 ) {
    return tcc->lay_const[cc];
  }

  switch( cc ) {
    case SEQ_CC_MODE: return tcc->playmode;
    case SEQ_CC_MODE_FLAGS: return tcc->trkmode_flags.ALL;
    case SEQ_CC_MIDI_EVENT_MODE: return tcc->event_mode;
    case SEQ_CC_MIDI_CHANNEL: return tcc->midi_chn;
    case SEQ_CC_MIDI_PORT: return tcc->midi_port;
    case SEQ_CC_MIDI_PC: return tcc->midi_pc;
    case SEQ_CC_MIDI_BANK_L: return tcc->midi_bank_l;
    case SEQ_CC_MIDI_BANK_H: return tcc->midi_bank_h;
    case SEQ_CC_FX_MIDI_MODE: return tcc->fx_midi_mode.ALL;
    case SEQ_CC_FX_MIDI_PORT: return tcc->fx_midi_port;
    case SEQ_CC_FX_MIDI_CHANNEL: return tcc->fx_midi_chn;
    case SEQ_CC_FX_MIDI_NUM_CHANNELS: return tcc->fx_midi_num_chn;
    case SEQ_CC_BUSASG: return tcc->busasg.bus;

    case SEQ_CC_LIMIT_LOWER: return tcc->limit_lower;
    case SEQ_CC_LIMIT_UPPER: return tcc->limit_upper;
  
    case SEQ_CC_DIRECTION: return tcc->dir_mode;
    case SEQ_CC_STEPS_REPLAY: return tcc->steps_replay;
    case SEQ_CC_STEPS_FORWARD: return tcc->steps_forward;
    case SEQ_CC_STEPS_JMPBCK: return tcc->steps_jump_back;
    case SEQ_CC_CLK_DIVIDER: return tcc->clkdiv.value;
    case SEQ_CC_LENGTH: return tcc->length;
    case SEQ_CC_LOOP: return tcc->loop;
    case SEQ_CC_CLKDIV_FLAGS: return tcc->clkdiv.flags;
  
    case SEQ_CC_TRANSPOSE_SEMI: return tcc->transpose_semi;
    case SEQ_CC_TRANSPOSE_OCT: return tcc->transpose_oct;
    case SEQ_CC_GROOVE_VALUE: return tcc->groove_value;
    case SEQ_CC_GROOVE_STYLE: return tcc->groove_style.ALL;
    case SEQ_CC_MORPH_MODE: return tcc->morph_mode;
    case SEQ_CC_MORPH_DST: return tcc->morph_dst;
    case SEQ_CC_HUMANIZE_VALUE: return tcc->humanize_value;
    case SEQ_CC_HUMANIZE_MODE: return tcc->humanize_mode;
  
    case SEQ_CC_ASG_GATE: return tcc->trg_assignments.gate;
    case SEQ_CC_ASG_ACCENT: return tcc->trg_assignments.accent;
    case SEQ_CC_ASG_ROLL: return tcc->trg_assignments.roll;
    case SEQ_CC_ASG_GLIDE: return tcc->trg_assignments.glide;
    case SEQ_CC_ASG_SKIP: return tcc->trg_assignments.skip;
    case SEQ_CC_ASG_RANDOM_GATE: return tcc->trg_assignments.random_gate;
    case SEQ_CC_ASG_RANDOM_VALUE: return tcc->trg_assignments.random_value;
    case SEQ_CC_ASG_NO_FX: return tcc->trg_assignments.no_fx;
    case SEQ_CC_ASG_ROLL_GATE: return tcc->trg_assignments.roll_gate;

    case SEQ_CC_PAR_ASG_DRUM_LAYER_A: return tcc->par_assignment_drum[0];
    case SEQ_CC_PAR_ASG_DRUM_LAYER_B: return tcc->par_assignment_drum[1];
    case SEQ_CC_PAR_ASG_DRUM_LAYER_C: return tcc->par_assignment_drum[2];
    case SEQ_CC_PAR_ASG_DRUM_LAYER_D: return tcc->par_assignment_drum[3];

    case SEQ_CC_STEPS_REPEAT: return tcc->steps_repeat;
    case SEQ_CC_STEPS_SKIP: return tcc->steps_skip;
    case SEQ_CC_STEPS_RS_INTERVAL: return tcc->steps_rs_interval;

#if 0  
    case SEQ_CC_CHANGE_STEP: return 0; // TODO
#endif

    case SEQ_CC_ECHO_REPEATS: return tcc->echo_repeats; break;
    case SEQ_CC_ECHO_DELAY: return tcc->echo_delay; break;
    case SEQ_CC_ECHO_VELOCITY: return tcc->echo_velocity; break;
    case SEQ_CC_ECHO_FB_VELOCITY: return tcc->echo_fb_velocity; break;
    case SEQ_CC_ECHO_FB_NOTE: return tcc->echo_fb_note; break;
    case SEQ_CC_ECHO_FB_GATELENGTH: return tcc->echo_fb_gatelength; break;
    case SEQ_CC_ECHO_FB_TICKS: return tcc->echo_fb_ticks; break;

    case SEQ_CC_LFO_WAVEFORM: return tcc->lfo_waveform;
    case SEQ_CC_LFO_AMPLITUDE: return tcc->lfo_amplitude;
    case SEQ_CC_LFO_PHASE: return tcc->lfo_phase;
    case SEQ_CC_LFO_STEPS: return tcc->lfo_steps;
    case SEQ_CC_LFO_STEPS_RST: return tcc->lfo_steps_rst;
    case SEQ_CC_LFO_ENABLE_FLAGS: return tcc->lfo_enable_flags.ALL;
    case SEQ_CC_LFO_CC: return tcc->lfo_cc;
    case SEQ_CC_LFO_CC_OFFSET: return tcc->lfo_cc_offset;
    case SEQ_CC_LFO_CC_PPQN: return tcc->lfo_cc_ppqn;

    case SEQ_CC_ROBOTIZE_PROBABILITY: return tcc->robotize_probability;
    case SEQ_CC_ROBOTIZE_NOTE: return tcc->robotize_note;
    case SEQ_CC_ROBOTIZE_OCT: return tcc->robotize_oct;
    case SEQ_CC_ROBOTIZE_VEL: return tcc->robotize_vel;
    case SEQ_CC_ROBOTIZE_LEN: return tcc->robotize_len;

    case SEQ_CC_ROBOTIZE_SKIP_PROBABILITY: return tcc->robotize_skip_probability;
    case SEQ_CC_ROBOTIZE_NOTE_PROBABILITY: return tcc->robotize_note_probability;
    case SEQ_CC_ROBOTIZE_OCT_PROBABILITY: return tcc->robotize_oct_probability;
    case SEQ_CC_ROBOTIZE_VEL_PROBABILITY: return tcc->robotize_vel_probability;
    case SEQ_CC_ROBOTIZE_LEN_PROBABILITY: return tcc->robotize_len_probability;
    case SEQ_CC_ROBOTIZE_SUSTAIN_PROBABILITY: return tcc->robotize_sustain_probability;
    case SEQ_CC_ROBOTIZE_NOFX_PROBABILITY: return tcc->robotize_nofx_probability;
    case SEQ_CC_ROBOTIZE_ECHO_PROBABILITY: return tcc->robotize_echo_probability;
    case SEQ_CC_ROBOTIZE_DUPLICATE_PROBABILITY: return tcc->robotize_duplicate_probability;

    case SEQ_CC_ROBOTIZE_ACTIVE: return tcc->robotize_active;
    case SEQ_CC_ROBOTIZE_MASK1: return tcc->robotize_mask1;
    case SEQ_CC_ROBOTIZE_MASK2: return tcc->robotize_mask2;
    case SEQ_CC_ROBOTIZE_LOOP_CYCLES: return tcc->robotize_loop_cycles;
    case SEQ_CC_ROBOTIZE_SYNC_TO_MASTER: return tcc->robotize_sync_to_master;
    case SEQ_CC_ROBOTIZE_PALETTE_LENGTH: return tcc->robotize_palette_length;
    case SEQ_CC_ROBOTIZE_LOOP_START: return tcc->robotize_loop_start;
    case SEQ_CC_ROBOTIZE_LOOP_ROTATE: return tcc->robotize_loop_rotate;
    case SEQ_CC_CHORDMASK_STRENGTH:   return tcc->chordmask_strength;
    case SEQ_CC_CHORDMASK_BUS:        return tcc->chordmask_bus;
    case SEQ_CC_CHORDMASK_DRUM_L:     return tcc->chordmask_drum_l;
    case SEQ_CC_CHORDMASK_DRUM_H:     return tcc->chordmask_drum_h;
    case SEQ_CC_TENSION_GRIP:         return tcc->tension_grip;
  }

  return -2; // invalid CC
}



/////////////////////////////////////////////////////////////////////////////
// Should be called whenever the event mode or par layer assignments have
// been changed
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_LinkUpdate(u8 track)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  u8 *par_asg = (tcc->event_mode == SEQ_EVENT_MODE_Drum)
    ? (u8 *)&seq_cc_trk[track].par_assignment_drum[0]
    : (u8 *)&seq_cc_trk[track].lay_const[0*16];

  // since CCs can be modified from other tasks at different priority we should do this operation atomic
  portENTER_CRITICAL();

  tcc->link_par_layer_note = -1;
  tcc->link_par_layer_chord = -1;
  tcc->link_par_layer_velocity = -1;
  tcc->link_par_layer_length = -1;
  tcc->link_par_layer_probability = -1;
  tcc->link_par_layer_delay = -1;
  tcc->link_par_layer_roll = -1;
  tcc->link_par_layer_roll2 = -1;
  tcc->link_par_layer_nth1 = -1;
  tcc->link_par_layer_nth2 = -1;
  tcc->link_par_layer_root = -1;
  tcc->link_par_layer_scale = -1;

  u8 num_layers = SEQ_PAR_NumLayersGet(track);
  if( num_layers ) {
    // search backwards to ensure that first assignments will be taken
    int layer;
    for(layer=num_layers-1; layer>=0; --layer) {
      switch( (seq_par_layer_type_t)par_asg[layer] ) {
        case SEQ_PAR_Type_Note: tcc->link_par_layer_note = layer; break;
        case SEQ_PAR_Type_Chord1: tcc->link_par_layer_chord = layer; break;
        case SEQ_PAR_Type_Chord2: tcc->link_par_layer_chord = layer; break;
        case SEQ_PAR_Type_Chord3: tcc->link_par_layer_chord = layer; break;
        case SEQ_PAR_Type_Velocity: tcc->link_par_layer_velocity = layer; break;
        case SEQ_PAR_Type_Length: tcc->link_par_layer_length = layer; break;
        case SEQ_PAR_Type_Probability: tcc->link_par_layer_probability = layer; break;
        case SEQ_PAR_Type_Delay: tcc->link_par_layer_delay = layer; break;
        case SEQ_PAR_Type_Roll: tcc->link_par_layer_roll = layer; break;
        case SEQ_PAR_Type_Roll2: tcc->link_par_layer_roll2 = layer; break;
        case SEQ_PAR_Type_Nth1: tcc->link_par_layer_nth1 = layer; break;
        case SEQ_PAR_Type_Nth2: tcc->link_par_layer_nth2 = layer; break;
        case SEQ_PAR_Type_Root: tcc->link_par_layer_root = layer; break;
        case SEQ_PAR_Type_Scale: tcc->link_par_layer_scale = layer; break;
      }
    }
  }

  portEXIT_CRITICAL();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Relevant for Drum mode: returns 1 if the track has a parameter layer
// assigned to velocity, otherwise 0 (in this case, the drum instruments
// can only switch between normal and accented velocity)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_TrackHasVelocityParLayer(u8 track)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];

  return (tcc->link_par_layer_velocity >= 0) ? 1 : 0;
}

/////////////////////////////////////////////////////////////////////////////
// Relevant for Drum mode: returns 1 if the track has a trigger layer
// assigned to accent
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_CC_TrackHasAccentTrgLayer(u8 track)
{
  return seq_cc_trk[track].trg_assignments.accent ? 1 : 0;
}


