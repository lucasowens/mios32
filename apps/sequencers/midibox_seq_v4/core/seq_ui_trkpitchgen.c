// $Id$
/*
 * Pitch Generator page — phase E + F + G + H (§8 step 6 polish + follow-on).
 *
 * GP1 ENGAGE / ROLL / DISENGAGE (LED reflects engaged state).
 *       - disengaged          → ENGAGE (alloc slot, snapshot par, seed loop)
 *       - engaged             → ROLL (one-shot reroll of unlocked steps)
 *       - SELECT held + GP1   → DISENGAGE (stop mutating, keep slot+loop)
 *     This collapses the phase-F ENGAGE-toggle into the design-doc shape
 *     (§9 line 1199): "ROLL collapses into ENGAGE-while-engaged ROLL once
 *     step 6 lands." DISENGAGE moves behind the SELECT modifier because
 *     live use favors BOUNCE-in-place or UNDO over plain DISENGAGE.
 * GP2 UNDO (restores pre-engage par snapshot, disengages every gen on that
 *     track).
 * GP3 Range min   (encoder; clamped to <= range_max).
 *     Button press: ANCHOR — re-snapshot current loop as new identity (H).
 * GP4 Range max   (encoder; clamped to >= range_min).
 *     Button press: SNAP — restore loop from anchor (hard return, H).
 * GP5 Mutation rate 0..127 (encoder; the §5 journey dial, sweepable live).
 * GP6 Mutation depth 0..127 (encoder; 0=frozen, 127=full reroll, between =
 *     ±depth semitone perturb around existing value). Phase G.
 *     Button press: toggle per-step LOCK at cursor step (phase G.4).
 * GP7 Contour shape (encoder cycles UNIFORM/LOW_BIAS/HIGH_BIAS/TRIANGLE).
 *     Biases the full-reroll distribution. Phase G.
 *     Button press: cycle per-step MULT at cursor (0×/0.5×/1×/2×). Phase H.
 * GP8 BOUNCE — phase F. Dual semantics on one button:
 *       - generator engaged on (track, instr)  → freeze + free the slot
 *         (SEQ_GENERATOR_Bounce). Loop is discarded; source stays as last
 *         written; next ENGAGE seeds fresh. UNDO still reverts to pre-engage.
 *       - else any enabled processor slot on track → commit output → source
 *         and clear the stack (SEQ_CORE_ProcessorBounce).
 *       - else: nothing-to-bounce message.
 *
 * Target = a Note line on the visible track (track = SEQ_UI_VisibleTrackGet).
 *           Drum track: instrument = ui_selected_instrument (the drum cursor),
 *           one line per drum, writing the shared linked Note layer. Normal
 *           track: instrument = 0 (single melodic line) and the gen writes the
 *           cursor's Note layer (ui_selected_par_layer) if it's a Note layer,
 *           else the linked Note layer — cursor-aware, deliberate placement
 *           wins. The only requirement is an assigned Note par-layer (else
 *           ENGAGE refuses with a guidance message). See gen_instr() /
 *           gen_par_layer().
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
// Generator target line for the visible track. Drum tracks select a line via
// the instrument cursor (which drum); normal tracks collapse to the single
// melodic line (instrument 0) — ui_selected_instrument is the drum cursor and
// would be stale from a previously-visited drum track. One generator slot per
// normal track in this mono build; per-Note-layer polyphony is a later step.
/////////////////////////////////////////////////////////////////////////////
static u8 gen_instr(u8 track)
{
  return (seq_cc_trk[track].event_mode == SEQ_EVENT_MODE_Drum)
           ? ui_selected_instrument : 0;
}


/////////////////////////////////////////////////////////////////////////////
// Cursor-aware target Note par-layer for the visible track. Drum tracks write
// the shared linked Note layer (drum lines are separated by instrument, not
// layer). Normal tracks honor the cursor: if ui_selected_par_layer is itself a
// Note layer, the gen targets *that* layer (deliberate cursor placement wins);
// otherwise it falls back to the track's linked Note layer. Returns the linked
// layer cast to u8 even when -1 (no Note layer) — Engage's -3 guard catches it.
/////////////////////////////////////////////////////////////////////////////
static u8 gen_par_layer(u8 track)
{
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum &&
      SEQ_PAR_AssignmentGet(track, ui_selected_par_layer) == SEQ_PAR_Type_Note )
    return ui_selected_par_layer;
  return (u8)tcc->link_par_layer_note;
}


/////////////////////////////////////////////////////////////////////////////
// LED handler — GP1 lit while engaged.
/////////////////////////////////////////////////////////////////////////////
static s32 LED_Handler(u16 *gp_leds)
{
  u8 track = SEQ_UI_VisibleTrackGet();
  if( SEQ_GENERATOR_IsEngaged(track, gen_instr(track)) )
    *gp_leds = (1 << 0);
  else
    *gp_leds = 0;
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Range / rate / depth / contour encoder dispatch + datawheel cursor.
/////////////////////////////////////////////////////////////////////////////
static s32 Encoder_Handler(seq_ui_encoder_t encoder, s32 incrementer)
{
  u8 track = SEQ_UI_VisibleTrackGet();

  // Datawheel scrolls the lock cursor across the loop. Works regardless of
  // engagement state — moving the cursor pre-ENGAGE lets the user pre-aim
  // where the first LOCK toggle will land. Clamped to loop length, not the
  // track's par-step count (the gen always works in LOOP_LEN units).
  if( encoder == SEQ_UI_ENCODER_Datawheel ) {
    if( SEQ_UI_Var8_Inc(&ui_selected_step, 0,
                        SEQ_GENERATOR_LOOP_LEN - 1, incrementer) >= 1 ) {
      ui_selected_step_view = ui_selected_step / 16;
      return 1;
    }
    return 0;
  }

  seq_generator_t *g = SEQ_GENERATOR_Get(track, gen_instr(track));

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
    case SEQ_UI_ENCODER_GP6: {
      u8 v = g->mutation_depth;
      s32 r = SEQ_UI_Var8_Inc(&v, 0, 127, incrementer);
      g->mutation_depth = v;
      return r;
    }
    case SEQ_UI_ENCODER_GP7: {
      u8 v = g->contour_shape;
      s32 r = SEQ_UI_Var8_Inc(&v, 0, SEQ_GENERATOR_CONTOUR_NUM - 1, incrementer);
      g->contour_shape = v;
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
  u8 instr = gen_instr(track);

  if( button == SEQ_UI_BUTTON_GP1 ) {
    u8 engaged = SEQ_GENERATOR_IsEngaged(track, instr);

    // SELECT held = explicit DISENGAGE escape (rarely needed in live use,
    // but covers DISENGAGE→re-ENGAGE iteration without losing the loop).
    if( seq_ui_button_state.SELECT_PRESSED ) {
      if( engaged ) {
        SEQ_GENERATOR_Disengage(track, instr);
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "DISENGAGED");
      } else {
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "not engaged");
      }
      return 1;
    }

    if( engaged ) {
      // ROLL: one-shot reroll of unlocked steps. Independent of rate dial.
      SEQ_GENERATOR_Roll(track);
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 500, "Pitch Gen:", "ROLL");
      return 1;
    }

    // disengaged → ENGAGE on the cursor drum, unconditionally.
    //
    // Earlier (phase F.3) this branch implemented a "first empty legal
    // layer" auto-jump: if the cursor drum had Note content and no
    // existing gen slot, the cursor was moved to the first empty drum
    // before engaging. Intent: protect user-authored material from the
    // gen's first source-write. In live use that fought the deliberate
    // gesture "I want the gen on *this* drum" — selecting a drum then
    // pressing ENGAGE silently landed elsewhere. The UNDO snapshot
    // (taken inside SEQ_GENERATOR_Engage *before* the first source
    // write) already protects against accidental overwrites, so the
    // auto-jump is removed: user pick always wins. §3 "default
    // destination = first empty legal layer" still applies in spirit —
    // "default" means "absent a deliberate cursor placement," but we
    // can't reliably detect that, so deferring to the cursor is the
    // right call.

    s32 r = SEQ_GENERATOR_Engage(track, instr, gen_par_layer(track));
    switch( r ) {
      case 0:
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "ENGAGED");
        break;
      case -1:
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "pool full (64/64)");
        break;
      case -2:
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "bad track/line");
        break;
      case -3:
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "assign Note par-layer");
        break;
      default:
        SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "ENGAGE failed");
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

  if( button == SEQ_UI_BUTTON_GP8 ) {
    // BOUNCE — in-place freeze on the visible track only. Cross-track and
    // to-slot captures are deliberate, explicit gestures elsewhere (the
    // PATTERN-hold capture gesture drives capture → pattern slot); GP8 no
    // longer auto-guesses a destination.
    //
    //   1. Cursor IS on the engaged gen → freeze gen in place (free slot,
    //      source stays as last-written).
    //   2. Visible track has an enabled processor → freeze processor output
    //      → source on the visible track, clear the stack.
    //   3. Nothing applicable → guidance.
    if( SEQ_GENERATOR_IsEngaged(track, instr) ) {
      SEQ_GENERATOR_Bounce(track, instr);
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "BOUNCED in place");
      return 1;
    }

    if( SEQ_CORE_ProcessorBounce(track) ) {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "BOUNCED (proc)");
      return 1;
    }

    SEQ_UI_Msg(SEQ_UI_MSG_USER, 1500, "Pitch Gen:", "nothing to bounce");
    return 1;
  }

  if( button == SEQ_UI_BUTTON_GP6 ) {
    // Phase G LOCK toggle at the cursor step. No-op if no slot allocated.
    s32 r = SEQ_GENERATOR_LockToggle(track, instr, ui_selected_step);
    if( r < 0 ) {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "ENGAGE first to LOCK");
    } else {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 500, "Pitch Gen:",
                 r ? "step LOCKED" : "step unlocked");
    }
    return 1;
  }

  // Phase H gestures. GP3/GP4/GP7 button-presses (previously no-ops that
  // fell through into Encoder_Handler with incrementer 0) now dispatch
  // ANCHOR/SNAP/MULT. Encoder rotation on those same GPs still tunes
  // range_min / range_max / contour as before — button vs. encoder are
  // distinct events at this layer.
  if( button == SEQ_UI_BUTTON_GP3 ) {
    s32 r = SEQ_GENERATOR_Anchor(track, instr);
    if( r < 0 )
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "ENGAGE first to ANCHOR");
    else
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "ANCHORED");
    return 1;
  }

  if( button == SEQ_UI_BUTTON_GP4 ) {
    s32 r = SEQ_GENERATOR_Snap(track, instr);
    switch( r ) {
      case 0:  SEQ_UI_Msg(SEQ_UI_MSG_USER, 750, "Pitch Gen:", "SNAPPED");      break;
      case -2: SEQ_UI_Msg(SEQ_UI_MSG_USER, 1500, "Pitch Gen:", "no anchor yet"); break;
      default: SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "ENGAGE first to SNAP");
    }
    return 1;
  }

  if( button == SEQ_UI_BUTTON_GP7 ) {
    s32 r = SEQ_GENERATOR_MultCycle(track, instr, ui_selected_step);
    if( r < 0 ) {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "ENGAGE first to MULT");
    } else {
      // SEQ_UI_Msg takes `char *` (no const) on its line args; keep the
      // table mutable so we don't need a cast at the call site.
      static char *mult_name[SEQ_GENERATOR_MULT_NUM] = {
        "MULT 0x  (mute)",  "MULT 0.5x",  "MULT 1x",  "MULT 2x"
      };
      static char fallback[] = "MULT ?";
      char *line2 = (r < SEQ_GENERATOR_MULT_NUM) ? mult_name[r] : fallback;
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 600, "Pitch Gen:", line2);
    }
    return 1;
  }

  if( button == SEQ_UI_BUTTON_GP5 ) {
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
// PITCH GEN  Trk 1.D 3   state:ENGAGED      G1=EN/RL G2=UN G3=ANC G4=SNP G8=BNC
// Lo:C 2 Hi:C 6 R:008 D:127 Ct:Uni          Stp:03 [L] M:1x   G6=LCK G7=MULT
/////////////////////////////////////////////////////////////////////////////
static const char *contour_name(u8 shape)
{
  switch( shape ) {
    case SEQ_GENERATOR_CONTOUR_LOW_BIAS:  return "Lo ";
    case SEQ_GENERATOR_CONTOUR_HIGH_BIAS: return "Hi ";
    case SEQ_GENERATOR_CONTOUR_TRIANGLE:  return "Tri";
    default:                              return "Uni";
  }
}

// MULT label for the engaged-hint row. Padded to a fixed 4-char width so the
// surrounding LCD slots line up regardless of code (the cursor scroll shouldn't
// reflow the rest of the row).
static const char *mult_label(u8 code)
{
  switch( code ) {
    case SEQ_GENERATOR_MULT_MUTE:   return "0x  ";
    case SEQ_GENERATOR_MULT_HALF:   return "0.5x";
    case SEQ_GENERATOR_MULT_DEFAULT:return "1x  ";
    case SEQ_GENERATOR_MULT_DOUBLE: return "2x  ";
    default:                       return "??  ";
  }
}

static s32 LCD_Handler(u8 high_prio)
{
  if( high_prio )
    return 0;

  u8 track = SEQ_UI_VisibleTrackGet();
  u8 instr = gen_instr(track);
  u8 is_drum = (seq_cc_trk[track].event_mode == SEQ_EVENT_MODE_Drum);
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  u8 engaged = SEQ_GENERATOR_IsEngaged(track, instr);

  // Row 0. Drum tracks name the line by drum number (.D nn); normal tracks
  // name the target Note par-layer (Nt:X) — the layer the gen writes into,
  // which the cursor selects (cursor-aware). Show the engaged gen's actual
  // target if a slot exists, else the layer ENGAGE would pick right now. Both
  // prefixes are 23 chars wide so "state:" lands at the same column.
  SEQ_LCD_CursorSet(0, 0);
  if( is_drum )
    SEQ_LCD_PrintFormattedString("PITCH GEN  Trk%2d.D%2d   ", track+1, instr+1);
  else {
    u8 pl = (g != NULL) ? g->par_layer : gen_par_layer(track);
    char letter = (pl < SEQ_PAR_NumLayersGet(track)) ? (char)('A' + pl) : '?';
    SEQ_LCD_PrintFormattedString("PITCH GEN  Trk%2d Nt:%c  ", track+1, letter);
  }
  if( engaged )       SEQ_LCD_PrintString("state:ENGAGED     ");
  else if( g != NULL ) SEQ_LCD_PrintString("state:disengaged  ");
  else                 SEQ_LCD_PrintString("state:--          ");

  SEQ_LCD_CursorSet(40, 0);
  SEQ_LCD_PrintString("G1=EN/RL G2=UN G3=ANC G4=SNP G8=BNC     ");

  // Row 1 LHS: dials. Phase G adds depth (D:) and contour (Ct:).
  SEQ_LCD_CursorSet(0, 1);
  if( g != NULL ) {
    SEQ_LCD_PrintString("Lo:");
    SEQ_LCD_PrintNote(g->range_min);
    SEQ_LCD_PrintString(" Hi:");
    SEQ_LCD_PrintNote(g->range_max);
    SEQ_LCD_PrintFormattedString(" R:%03d D:%03d Ct:%s         ",
                                 g->mutation_rate, g->mutation_depth,
                                 contour_name(g->contour_shape));
  } else {
    SEQ_LCD_PrintString("Lo:--  Hi:--  R:--- D:--- Ct:---        ");
  }

  // Row 1 RHS: contextual hint. The only hard requirement now (drum OR
  // normal) is a Note par-layer to write into.
  SEQ_LCD_CursorSet(40, 1);
  if( tcc->link_par_layer_note < 0 )
    SEQ_LCD_PrintString("(assign Note in PAR-ASG first)          ");
  else if( engaged ) {
    // Show cursor + its lock state + per-step MULT + key gestures.
    u8 locked = SEQ_GENERATOR_LockGet(g, ui_selected_step);
    u8 mc     = SEQ_GENERATOR_MultGet(g, ui_selected_step);
    SEQ_LCD_PrintFormattedString("Stp:%02d %s M:%s G6=LCK G7=MULT        ",
                                 ui_selected_step + 1,
                                 locked ? "[L]" : "[ ]",
                                 mult_label(mc));
  } else
    SEQ_LCD_PrintString("press GP1 to ENGAGE   GP8=bounce proc   ");

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
