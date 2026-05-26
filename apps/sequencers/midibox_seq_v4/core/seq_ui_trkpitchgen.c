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
  u8 instr = ui_selected_instrument;

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
    // Phase F.2 + F.3 BOUNCE — cursor-aware destination (§3).
    //
    // Resolve in order:
    //   1. Cursor IS on the engaged gen → in-place gen bounce (free slot,
    //      source stays as last-written). §3 "occupied → replace".
    //   2. A gen is engaged elsewhere on the visible track → relocate it
    //      to (cursor track, cursor instr): restore src from undo, write
    //      loop to dst. §3 "empty → additive".
    //   3. Visible track has an enabled processor → in-place processor
    //      bounce-and-commit (destructive: output → source on visible
    //      track). §3 "occupied → replace" for processors.
    //   4. Visible track empty AND exactly one other track has an enabled
    //      processor → cross-track capture: copy that track's output into
    //      visible track's source, leave the src processor untouched. §3
    //      "empty → additive" for processors (phase F.3).
    //   5. Visible track empty AND multiple other tracks have processors
    //       → refuse with "multi proc" message (ambiguous).
    //   6. Nothing applicable → "nothing to bounce" guidance.
    if( SEQ_GENERATOR_IsEngaged(track, instr) ) {
      SEQ_GENERATOR_Bounce(track, instr);
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "BOUNCED in place");
      return 1;
    }

    u8 src_instr;
    if( SEQ_GENERATOR_FindEngagedOnTrack(track, &src_instr) ) {
      s32 r = SEQ_GENERATOR_BounceRelocate(track, src_instr, track, instr);
      switch( r ) {
        case 0:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1200, "Pitch Gen:", "BOUNCED -> drum slot");
          break;
        case -2:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1800, "Pitch Gen:", "dst not drum-mode");
          break;
        case -3:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 2200, "Pitch Gen:", "undo stale - reENGAGE");
          break;
        default:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1800, "Pitch Gen:", "BOUNCE failed");
      }
      return 1;
    }

    if( SEQ_CORE_ProcessorBounce(track) ) {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 1000, "Pitch Gen:", "BOUNCED (proc)");
      return 1;
    }

    // Phase F.3 cross-track capture. Only reaches here when the visible
    // track has nothing of its own to bounce.
    u8 cap_src;
    u8 cap_count = SEQ_CORE_FindEnabledProcessorTrack(track, &cap_src);
    if( cap_count == 1 ) {
      s32 r = SEQ_CORE_ProcessorBounceCapture(cap_src, track);
      switch( r ) {
        case 0: {
          char line2[21];
          sprintf(line2, "CAPTURED from T%2d", cap_src + 1);
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1200, "Pitch Gen:", line2);
          break;
        }
        case -1:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1800, "Pitch Gen:", "dst not empty");
          break;
        case -2:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1500, "Pitch Gen:", "nothing to bounce");
          break;
        default:
          SEQ_UI_Msg(SEQ_UI_MSG_USER, 1800, "Pitch Gen:", "CAPTURE failed");
      }
      return 1;
    } else if( cap_count >= 2 ) {
      SEQ_UI_Msg(SEQ_UI_MSG_USER, 2000, "Pitch Gen:", "multi proc - pick one");
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
  u8 instr = ui_selected_instrument;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  u8 engaged = SEQ_GENERATOR_IsEngaged(track, instr);
  u8 other_engaged_instr = 0;
  u8 has_other_engaged = 0;
  if( !engaged )
    has_other_engaged = SEQ_GENERATOR_FindEngagedOnTrack(track, &other_engaged_instr);

  // Phase F.3 — cross-track capture candidate hint. Only meaningful when
  // the visible track has nothing of its own (no engaged gen anywhere on
  // it, and no enabled processor).
  u8 cap_src_track = 0;
  u8 cap_count = 0;
  u8 has_own_proc = 0;
  if( !engaged && !has_other_engaged ) {
    u8 s;
    for(s=0; s<SEQ_CORE_NUM_PROCESSOR_SLOTS; ++s) {
      const seq_processor_slot_t *p = &seq_processor_stack[track][s];
      if( p->id != SEQ_PROCESSOR_ID_NONE && p->enabled ) { has_own_proc = 1; break; }
    }
    if( !has_own_proc )
      cap_count = SEQ_CORE_FindEnabledProcessorTrack(track, &cap_src_track);
  }

  // Row 0
  SEQ_LCD_CursorSet(0, 0);
  SEQ_LCD_PrintFormattedString("PITCH GEN  Trk%2d.D%2d   ", track+1, instr+1);
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

  // Row 1 RHS: contextual hint.
  SEQ_LCD_CursorSet(40, 1);
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum )
    SEQ_LCD_PrintString("(needs drum-mode track)                 ");
  else if( tcc->link_par_layer_note < 0 )
    SEQ_LCD_PrintString("(assign Note in PAR-ASG first)          ");
  else if( engaged ) {
    // Show cursor + its lock state + per-step MULT + key gestures.
    u8 locked = SEQ_GENERATOR_LockGet(g, ui_selected_step);
    u8 mc     = SEQ_GENERATOR_MultGet(g, ui_selected_step);
    SEQ_LCD_PrintFormattedString("Stp:%02d %s M:%s G6=LCK G7=MULT        ",
                                 ui_selected_step + 1,
                                 locked ? "[L]" : "[ ]",
                                 mult_label(mc));
  } else if( has_other_engaged ) {
    SEQ_LCD_PrintFormattedString("GEN on D%2d  GP8 relocates here       ",
                                 other_engaged_instr + 1);
  } else if( cap_count == 1 ) {
    SEQ_LCD_PrintFormattedString("Proc on T%2d  GP8 captures here         ",
                                 cap_src_track + 1);
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
