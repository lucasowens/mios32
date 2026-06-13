// SEQ V4 test-control SysEx interface.
//
// Wire protocol:
//   request : F0 00 00 7E 4F 54 <cmd> [args...] F7
//   reply   : F0 00 00 7E 4F 54 <cmd> [data...] F7
//
// The header (F0 00 00 7E 4F 54) is intentionally distinct from MBSEQ's own
// SysEx header (F0 00 00 7E 4D ...) so the two parsers never claim each
// other's bytes. The trailing 'OT' (0x4F 0x54) is the harness vendor tag.

#include <mios32.h>
#include <string.h>
#include <seq_bpm.h>
#include "tasks.h"
#include "seq_testctrl.h"
#include "seq_ui.h"
#include "seq_ui_pages.h"
#include "seq_hwcfg.h"
#include "seq_lcd.h"
#include "seq_lcd_logo.h"
#include "seq_cc.h"
#include "seq_core.h"
#include "seq_midi_port.h"
#include "seq_pattern.h"
#include "file.h"
#include "seq_file.h"
#include "seq_file_b.h"
#include "seq_trg.h"
#include "seq_par.h"
#include "seq_layer.h"
#include "seq_generator.h"


/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

static const u8 testctrl_header[6] = { 0xf0, 0x00, 0x00, 0x7e, 0x4f, 0x54 };

#define CMD_PING              0x01
#define CMD_BUTTON            0x10
#define CMD_ENCODER           0x11
#define CMD_LCD_SNAPSHOT      0x20
#define CMD_RESET_STATE       0x30
#define CMD_PAGE_SET          0x31
#define CMD_TRACK_CONFIG      0x32
#define CMD_TICK_QUERY        0x40
#define CMD_CC_GET            0x50
#define CMD_CC_SET            0x51
#define CMD_PLAY_SECTION_GET  0x52
#define CMD_PLAY_SECTION_SET  0x53
#define CMD_BOUNCE            0x54
#define CMD_PATTERN_LOAD      0x55
#define CMD_SESSION_LOAD      0x56
#define CMD_SESSION_NAME_GET  0x57
#define CMD_TRG_BYTE_GET      0x58
#define CMD_MSP_QUERY         0x59
#define CMD_UI_INSTR_SET      0x5a
#define CMD_TRACK_DRUM_INIT   0x5b
#define CMD_GENERATOR_QUERY   0x5c
#define CMD_UI_TRACK_SET      0x5d
#define CMD_TRACK_DRUM_PAR_SET 0x5e
#define CMD_TRACK_DRUM_PAR_GET 0x5f
#define CMD_GENERATOR_TICK_FORCE 0x60
#define CMD_GENERATOR_DIAL_SET   0x61
#define CMD_GENERATOR_MULT_SET   0x62
#define CMD_CAPTURE_TO_TRACK     0x63
#define CMD_CAPTURE_TO_SLOT_TRACK 0x64
#define CMD_UI_TRACK_GET         0x65
#define CMD_TRACK_PAR_GET        0x66
#define CMD_TRACK_PAR_SET        0x67
#define CMD_GLOBAL_SCALE_SET     0x68
#define CMD_TENSION_SET          0x69
#define CMD_TENSION_BAND_GET     0x6a
#define CMD_TENSION_GET          0x6b
#define CMD_TENSION_RESOLVE      0x6c
#define CMD_PATTERN_SAVE         0x6d
#define CMD_BANK_CREATE          0x6e
#define CMD_TRACK_LOAD           0x6f
#define CMD_TRACK_UNDO           0x70
#define CMD_TRACK_UNDO_QUERY     0x71
#define CMD_SESSION_CREATE       0x72
#define CMD_DIRTY_QUERY          0x73
#define CMD_DIRTY_SET            0x74
#define CMD_PATTERN_CHANGE       0x75
#define CMD_GENERATOR_LOCK_SET   0x76
#define CMD_CHECKPOINT           0x77
#define CMD_REVERT               0x78
#define CMD_ANCHOR_PRESENT       0x79
#define CMD_PHRASE_CAPTURE       0x7a
#define CMD_PHRASE_RECALL        0x7b
#define CMD_PHRASE_PRESENT       0x7c
#define CMD_TRG_BYTE_SET         0x7d
#define CMD_FREEZE_SET           0x7e

// Encoder indices match MBSEQ's internal numbering:
//   0  = Datawheel
//   1..16 = GP1..GP16
//   17 = BPM wheel

// Logical button IDs. Keep in sync with tests/harness/sysex.py.
// Numbered explicitly so additions don't shift existing IDs.
typedef enum {
  BTN_MENU      = 0x01,
  BTN_SELECT    = 0x02,
  BTN_EXIT      = 0x03,
  BTN_PLAY      = 0x04,
  BTN_STOP      = 0x05,
  BTN_PAUSE     = 0x06,
  BTN_RECORD    = 0x07,
  BTN_REW       = 0x08,
  BTN_FWD       = 0x09,
  BTN_LEFT      = 0x0a,
  BTN_RIGHT     = 0x0b,
  BTN_UP        = 0x0c,
  BTN_DOWN      = 0x0d,
  BTN_EDIT      = 0x0e,
  BTN_MUTE      = 0x0f,
  BTN_PATTERN   = 0x10,
  BTN_SONG      = 0x11,
  BTN_BOOKMARK  = 0x12,
  BTN_CLEAR     = 0x13,
  BTN_UNDO      = 0x14,
  BTN_COPY      = 0x15,
  BTN_PASTE     = 0x16,
  // DIRECT_TRACK (the midiphy select row) 1..16: BTN_DIRECT_TRACK_BASE + (n - 1).
  BTN_DIRECT_TRACK_BASE = 0x20,
  // GP1..GP16 are contiguous: BTN_GP_BASE + (n - 1).
  BTN_GP_BASE   = 0x40,
} button_id_t;

// Max payload reply size for LCD_SNAPSHOT: 2x80 = 160 bytes, packed 7-bit
// (every 7 source bytes -> 8 wire bytes, +1 byte for line/col header).
// Round up generously.
#define LCD_SNAPSHOT_MAX_REPLY  256


/////////////////////////////////////////////////////////////////////////////
// Parser state
/////////////////////////////////////////////////////////////////////////////

typedef enum {
  STATE_IDLE,        // waiting for first header byte
  STATE_HEADER,      // matching header[1..5]
  STATE_CMD,         // header matched, waiting for command byte
  STATE_PAYLOAD,     // collecting payload bytes
  STATE_DONE         // got F7 and dispatched (or errored)
} parser_state_t;

static parser_state_t parser_state = STATE_IDLE;
static u8 header_ctr = 0;
static u8 cmd = 0;
static mios32_midi_port_t last_port = DEFAULT;

#define PAYLOAD_BUF_MAX 16
static u8 payload_buf[PAYLOAD_BUF_MAX];
static u8 payload_len = 0;


/////////////////////////////////////////////////////////////////////////////
// Reply helpers
/////////////////////////////////////////////////////////////////////////////

// Caller must guarantee payload bytes are already 7-bit (< 0x80). This avoids
// silently corrupting binary payloads when a future caller forgets to encode.
static s32 send_reply(mios32_midi_port_t port, u8 reply_cmd, const u8 *payload, u32 plen)
{
  u8 buf[LCD_SNAPSHOT_MAX_REPLY];
  u8 *p = buf;

  for(u32 i=0; i<sizeof(testctrl_header); ++i)
    *p++ = testctrl_header[i];

  *p++ = reply_cmd;

  for(u32 i=0; i<plen; ++i)
    *p++ = payload[i];

  *p++ = 0xf7;

  MUTEX_MIDIOUT_TAKE;
  s32 status = MIOS32_MIDI_SendSysEx(port, buf, (u32)(p - buf));
  MUTEX_MIDIOUT_GIVE;
  return status;
}


// Pack `n` arbitrary bytes from `src` into a 7-bit-safe wire encoding written
// to `dst`. Encoding: every 7 source bytes become 8 wire bytes where the first
// wire byte holds the high bits of the next 7, and each of those 7 wire bytes
// holds the low 7 bits of the corresponding source byte. Final partial group
// uses the same shape with high-bit padding zeroed. Returns bytes written.
static u32 pack7(const u8 *src, u32 n, u8 *dst)
{
  u32 written = 0;
  u32 i = 0;
  while( i < n ) {
    u32 group = (n - i) >= 7 ? 7 : (n - i);
    u8 msbs = 0;
    for(u32 j=0; j<group; ++j)
      if( src[i+j] & 0x80 ) msbs |= (1u << j);
    dst[written++] = msbs;
    for(u32 j=0; j<group; ++j)
      dst[written++] = src[i+j] & 0x7f;
    i += group;
  }
  return written;
}


/////////////////////////////////////////////////////////////////////////////
// Command dispatch
/////////////////////////////////////////////////////////////////////////////

static void cmd_ping(mios32_midi_port_t port)
{
  // Echo back a small build-id so the harness can confirm which firmware is running.
  static const u8 build_id[] = { 'S', 'E', 'Q', 'v', '4' };
  send_reply(port, CMD_PING, build_id, sizeof(build_id));
}


// Translate a logical button ID into the resolved hwcfg pin number, or 0xFFFF
// if the button isn't configured in the current hwcfg.
static u16 lookup_button_pin(u8 id)
{
  if( id >= BTN_GP_BASE && id < BTN_GP_BASE + SEQ_HWCFG_NUM_GP )
    return seq_hwcfg_button.gp[id - BTN_GP_BASE];

  if( id >= BTN_DIRECT_TRACK_BASE && id < BTN_DIRECT_TRACK_BASE + SEQ_HWCFG_NUM_DIRECT_TRACK )
    return seq_hwcfg_button.direct_track[id - BTN_DIRECT_TRACK_BASE];

  switch( id ) {
    case BTN_MENU:     return seq_hwcfg_button.menu;
    case BTN_SELECT:   return seq_hwcfg_button.select;
    case BTN_EXIT:     return seq_hwcfg_button.exit;
    case BTN_PLAY:     return seq_hwcfg_button.play;
    case BTN_STOP:     return seq_hwcfg_button.stop;
    case BTN_PAUSE:    return seq_hwcfg_button.pause;
    case BTN_RECORD:   return seq_hwcfg_button.record;
    case BTN_REW:      return seq_hwcfg_button.rew;
    case BTN_FWD:      return seq_hwcfg_button.fwd;
    case BTN_LEFT:     return seq_hwcfg_button.left;
    case BTN_RIGHT:    return seq_hwcfg_button.right;
    case BTN_UP:       return seq_hwcfg_button.up;
    case BTN_DOWN:     return seq_hwcfg_button.down;
    case BTN_EDIT:     return seq_hwcfg_button.edit;
    case BTN_MUTE:     return seq_hwcfg_button.mute;
    case BTN_PATTERN:  return seq_hwcfg_button.pattern;
    case BTN_SONG:     return seq_hwcfg_button.song;
    case BTN_BOOKMARK: return seq_hwcfg_button.bookmark;
    case BTN_CLEAR:    return seq_hwcfg_button.clear;
    case BTN_UNDO:     return seq_hwcfg_button.undo;
    case BTN_COPY:     return seq_hwcfg_button.copy;
    case BTN_PASTE:    return seq_hwcfg_button.paste;
    default:           return 0xFFFF;
  }
}


// CMD_BUTTON payload: [btn_id, depressed]
// Reply payload: [btn_id, depressed, pin_lo, pin_hi, status]
//   status 0x01 = dispatched, 0x00 = button not configured in hwcfg, 0x02 = bad payload.
// We always reply so the harness gets actionable feedback rather than timing out.
static void cmd_button(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_BUTTON, reply, sizeof(reply));
    return;
  }

  u8 id = payload[0];
  u8 depressed = payload[1] ? 1 : 0;
  u16 pin = lookup_button_pin(id);

  reply[0] = id;
  reply[1] = depressed;
  reply[2] = pin & 0x7f;
  reply[3] = (pin >> 7) & 0x7f;

  if( pin == 0xFFFF || pin == 0 ) {
    // 0 is the "unconfigured" sentinel used throughout MBSEQ (the HW config
    // file uses "0 0" to mean "no pin"); treat it the same as 0xFFFF.
    reply[4] = 0x00;
  } else {
    SEQ_UI_Button_Handler(pin, depressed);
    reply[4] = 0x01;
  }

  send_reply(port, CMD_BUTTON, reply, sizeof(reply));
}


// CMD_ENCODER payload: [encoder_idx, incrementer_offset]
//   encoder_idx: 0=datawheel, 1..16=GP1..GP16, 17=BPM
//   incrementer_offset: signed delta encoded as `delta + 0x40` (i.e. 0x40 = 0).
//     Valid wire range 0..0x7f -> -64..+63. SEQ_UI_Encoder_Handler clamps to +-3.
// Reply payload: [encoder_idx, incrementer_offset, status]
//   status 0x01 = dispatched, 0x02 = bad payload, 0x03 = encoder out of range.
static void cmd_encoder(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[3] = { 0, 0x40, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_ENCODER, reply, sizeof(reply));
    return;
  }

  u8 enc_idx = payload[0];
  u8 inc_off = payload[1] & 0x7f;
  s32 incrementer = (s32)inc_off - 0x40;

  reply[0] = enc_idx;
  reply[1] = inc_off;

  if( enc_idx >= SEQ_HWCFG_NUM_ENCODERS ) {
    reply[2] = 0x03;
  } else {
    SEQ_UI_Encoder_Handler(enc_idx, incrementer);
    reply[2] = 0x01;
  }

  send_reply(port, CMD_ENCODER, reply, sizeof(reply));
}


// CMD_RESET_STATE payload: [flags]
//   flags bit 0: stop transport  (SEQ_BPM_Stop)
//   flags bit 1: return to default page (SEQ_UI_PAGE_EDIT)
//   flags bit 2: reset UI track selection to G1T1 (group=0, tracks bitmask=1)
//   flags bit 3: unmute all tracks + clear port mutes + clear solo + clear slaveclk mute
//   flags bit 4: clear robotize state (ACTIVE/MASK1/MASK2) on all 16 tracks
//   flags bit 5: mute tracks 1-15 (leave track 0 unmuted). For single-track
//                tests so other tracks' patterns don't pollute the capture.
//                Applied AFTER bit 3 if both set, so the final state is "only
//                track 0 unmuted".
// Reply payload: [flags] echoed back, then status 0x01.
//
// Deliberately does NOT clear pattern data — tests that need an empty pattern
// should press CLEAR after reset.
static void cmd_reset_state(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  // Always reset the screen-saver idle timer on a harness reset (called before
  // every test by the conftest board fixture). Harness button injection bypasses
  // the app.c hardware-input path that normally resets it, so without this the
  // saver counts up unimpeded and eventually overlays the LCD mid-suite —
  // corrupting LCD-scrape assertions. No test body runs near the saver delay, so
  // resetting per-test keeps it from ever tripping. RAM-only; the user's saved
  // screensaver-delay config is untouched.
  SEQ_LCD_LOGO_ScreenSaver_Disable();

  // Default: stop + page + track-select + unmute-all + clear-robotize.
  u8 flags = (plen >= 1) ? (payload[0] & 0x7f) : 0x1f;

  if( flags & 0x01 )
    SEQ_BPM_Stop();
  if( flags & 0x02 )
    SEQ_UI_PageSet(SEQ_UI_PAGE_EDIT);
  if( flags & 0x04 ) {
    ui_selected_group = 0;
    ui_selected_tracks = 1;
  }
  if( flags & 0x08 ) {
    seq_core_trk_muted = 0;
    seq_core_trk_soloed = 0;
    seq_core_slaveclk_mute = SEQ_CORE_SLAVECLK_MUTE_Off;
    SEQ_MIDI_PORT_OutMuteSet(USB0, 0);
    SEQ_MIDI_PORT_OutMuteSet(USB1, 0);
    SEQ_MIDI_PORT_OutMuteSet(USB2, 0);
    SEQ_MIDI_PORT_OutMuteSet(USB3, 0);
  }

  if( flags & 0x10 ) {
    // Robotize state survives both page changes and the regular mute reset.
    // Without this, a previous test that enabled robotize SKIP/VELOCITY/etc.
    // leaks across pytest invocations and silently breaks the generator tests.
    for(u8 t=0; t<SEQ_CORE_NUM_TRACKS; ++t) {
      SEQ_CC_Set(t, SEQ_CC_ROBOTIZE_ACTIVE, 0);
      SEQ_CC_Set(t, SEQ_CC_ROBOTIZE_MASK1, 0);
      SEQ_CC_Set(t, SEQ_CC_ROBOTIZE_MASK2, 0);
    }

    // Generator pool also persists across tests (engaged slots from a prior
    // test would otherwise transcribe loops into the next test's freshly-
    // loaded pattern on the first measure boundary). Re-init wipes pool +
    // index map + undo + last-seen-step sentinels.
    SEQ_GENERATOR_Init(0);
  }

  if( flags & 0x20 ) {
    // Mute everything but track 0. Without this, tracks 1-15 fire their
    // (possibly populated) patterns alongside the track-under-test and
    // pollute the harness's note capture. Applied AFTER 0x08's clear so
    // the final state is "only track 0 unmuted".
    seq_core_trk_muted = 0xFFFE;
  }

  // FEARLESS SWITCHING: a harness reset is a state baseline — clear the
  // divergence bookkeeping unconditionally. Without this, the robotize/CC
  // clears above dirty every group on EVERY reset, and the next
  // pattern-change / session-load would auto-writeback test debris into
  // whatever slots seq_pattern[] names (the AUTOTEST A1-A3 baselines after
  // boot — the one SD content the suite must never rewrite).
  MIOS32_IRQ_Disable();
  seq_pattern_dirty = 0;
  MIOS32_IRQ_Enable();

  // A harness reset is a baseline — clear the transient UI gesture statics
  // (pull/capture held-modifier state + active select-view) and FREEZE. These
  // are RAM-only performance state with no reset path of their own, so a manual
  // hands-on session can leave them perturbed (a stuck pull_held_track breaks
  // select-row track-select; a left-on FREEZE silences generator mutation) and
  // poison the next suite run. Cleared unconditionally so reset() fully
  // normalizes UI state.
  SEQ_UI_GestureStateReset();
  seq_core_state.FREEZE = 0;

  // PHRASES: a harness reset is a baseline — clear session-scoped phrase
  // occupancy so a phrase captured by a prior test can't masquerade as present
  // (the "recall refuses an uncaptured phrase" pin needs a clean slate). The
  // phrase DATA on SD is untouched; only the in-RAM occupancy mask resets. Tests
  // must therefore not reset between a capture and its recall.
  SEQ_PATTERN_PhraseResetState();

  u8 reply[2] = { flags, 0x01 };
  send_reply(port, CMD_RESET_STATE, reply, sizeof(reply));
}


// CMD_PAGE_SET payload: [page_id]
// Reply payload: [page_id, status]   status 0x01 = set, 0x02 = bad payload.
//
// SEQ_UI_PageSet only flips ui_page and clears the encoder/button/LED callbacks;
// the new page's *Init function reinstalls them on the next UI task iteration.
// If we returned right after PageSet, subsequent encoder commands would fire
// while ui_encoder_callback == NULL (silent no-op). We call PAGES_CallInit
// directly here so the new page's callbacks are live before we ACK.
static void cmd_page_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_PAGE_SET, reply, sizeof(reply));
    return;
  }
  u8 page = payload[0] & 0x7f;
  reply[0] = page;
  SEQ_UI_PageSet((seq_ui_page_t)page);
  SEQ_UI_PAGES_CallInit((seq_ui_page_t)page);
  reply[1] = 0x01;
  send_reply(port, CMD_PAGE_SET, reply, sizeof(reply));
}


// CMD_TRACK_DRUM_INIT payload: [track]
// Reply payload: [track, status]   status 0x01 = ok, 0x02 = bad payload.
//
// Self-contained drum-mode track setup for harness tests. Reinitializes the
// par/trg layout to (64 steps × 1 par-layer × 16 instruments) for par and
// (64 steps × 8 trg-layers × 1 trg-instrument) for trg, sets event_mode = Drum
// (CC 0x42), sets par_assignment_drum[0] = Note so the generator can find a
// destination Note layer, then calls SEQ_CC_LinkUpdate to refresh the layer
// link cache (link_par_layer_note in particular). This is the minimum a
// PITCHGEN ENGAGE call needs to pass its drum-mode + Note-layer gating —
// no preset-track-name / per-drum-note assignments (tests don't depend on
// MIDI playback, only on the dispatch behavior).
static void cmd_track_drum_init(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_TRACK_DRUM_INIT, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;

  // Layout for 16-instrument drum mode: 64 par steps × 1 layer × 16 instr =
  // 1024 B (exactly SEQ_PAR_MAX_BYTES); 64 trg steps × 8 layers × 1 instr.
  SEQ_PAR_TrackInit(track, 64, 1, 16);
  SEQ_TRG_TrackInit(track, 64, 8, 1);

  // event_mode = Drum, par-layer 0 = Note, then refresh link cache so
  // tcc->link_par_layer_note ends up at 0.
  SEQ_CC_Set(track, SEQ_CC_MIDI_EVENT_MODE, SEQ_EVENT_MODE_Drum);
  seq_cc_trk[track].par_assignment_drum[0] = SEQ_PAR_Type_Note;
  SEQ_CC_LinkUpdate(track);

  reply[0] = track;
  reply[1] = 0x01;
  send_reply(port, CMD_TRACK_DRUM_INIT, reply, sizeof(reply));
}


// CMD_GENERATOR_QUERY payload: [track, instrument] or [track, instrument, flags]
//   flags bit0 (Stage B): also ship anchor[64], appended after mult in the
//   pack7 bundle — the byte-identical persistence pin needs the full slot.
// Reply payload (status 0x01 = ok):
//   [track, instr, status, range_min, range_max, mutation_rate,
//    mutation_depth, contour_shape, engaged, anchor_valid,
//    pack7(loop[64] | locks[8] | mult[32] [| anchor[64]])]
//      // 104 raw -> 120 wire; with anchor 168 raw -> 192 wire
// Reply payload (status 0x02 bad / 0x03 no slot):
//   [track, instr, status]
//
// Lets harness tests pin behavioral contracts (LOCK survives mutation, ROLL
// only touches unlocked steps, depth=0 freezes, ANCHOR/SNAP round-trip, MULT
// scales the rate-gated mutate decision) without resorting to MIDI capture.
// Returns raw loop bytes + lock bitmap + MULT nibbles; callers reconstruct
// via unpack7. Engaged byte is 0/1 so a single query covers both "slot
// exists" and "currently mutating"; anchor_valid is 0/1.
static void cmd_generator_query(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  // 10 fixed-header bytes + pack7(168) ≈ 202 bytes; round up for safety.
  u8 reply[3 + 7 + 224] = { 0, 0, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_GENERATOR_QUERY, reply, 3);
    return;
  }
  u8 track = payload[0];
  u8 instr = payload[1];
  reply[0] = track;
  reply[1] = instr;

  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  if( g == NULL ) {
    reply[2] = 0x03;
    send_reply(port, CMD_GENERATOR_QUERY, reply, 3);
    return;
  }

  reply[2] = 0x01;
  reply[3] = g->range_min;
  reply[4] = g->range_max;
  reply[5] = g->mutation_rate;
  reply[6] = g->mutation_depth;
  reply[7] = g->contour_shape;
  reply[8] = g->engaged ? 1 : 0;
  reply[9] = g->anchor_valid ? 1 : 0;

  // Bundle loop[64] + locks[8] + mult[32] (+ anchor[64] if flags bit0) into
  // raw bytes, pack7 to wire.
  u8 raw[SEQ_GENERATOR_LOOP_LEN + SEQ_GENERATOR_LOCKS_BYTES
         + SEQ_GENERATOR_MULT_BYTES + SEQ_GENERATOR_LOOP_LEN];
  u32 raw_len = SEQ_GENERATOR_LOOP_LEN + SEQ_GENERATOR_LOCKS_BYTES
                + SEQ_GENERATOR_MULT_BYTES;
  memcpy(raw, g->loop, SEQ_GENERATOR_LOOP_LEN);
  memcpy(raw + SEQ_GENERATOR_LOOP_LEN, g->locks, SEQ_GENERATOR_LOCKS_BYTES);
  memcpy(raw + SEQ_GENERATOR_LOOP_LEN + SEQ_GENERATOR_LOCKS_BYTES,
         g->mult, SEQ_GENERATOR_MULT_BYTES);
  if( plen >= 3 && (payload[2] & 0x01) ) {
    memcpy(raw + raw_len, g->anchor, SEQ_GENERATOR_LOOP_LEN);
    raw_len += SEQ_GENERATOR_LOOP_LEN;
  }

  u32 w = 10 + pack7(raw, raw_len, &reply[10]);
  send_reply(port, CMD_GENERATOR_QUERY, reply, w);
}


// CMD_GENERATOR_LOCK_SET payload: [track, instr, step, on]
// Reply payload: [track, instr, status]
//   status 0x01 = set, 0x02 = bad payload, 0x03 = no slot / step out of range.
//
// Stage B companion to MULT_SET: lets the persistence pins sculpt the lock
// bitmap deterministically (locks are part of the byte-identical round-trip
// contract) without driving the PITCHGEN page cursor.
static void cmd_generator_lock_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[3] = { 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_GENERATOR_LOCK_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0];
  u8 instr = payload[1];
  u8 step  = payload[2];
  u8 on    = payload[3];
  reply[0] = track;
  reply[1] = instr;

  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  if( g == NULL || step >= SEQ_GENERATOR_LOOP_LEN ) {
    reply[2] = 0x03;
    send_reply(port, CMD_GENERATOR_LOCK_SET, reply, sizeof(reply));
    return;
  }

  SEQ_GENERATOR_LockSet(g, step, on ? 1 : 0);
  reply[2] = 0x01;
  send_reply(port, CMD_GENERATOR_LOCK_SET, reply, sizeof(reply));
}


// CMD_UI_INSTR_SET payload: [instr]
// Reply payload: [instr, status]   status 0x01 = set, 0x02 = bad payload.
//
// Writes ui_selected_instrument directly — the global the PITCHGEN page (and
// drum-mode EDIT) reads as the "cursor target drum". Required so the harness
// can park the cursor on an empty drum slot before pressing BOUNCE to exercise
// the relocate path. No display dirty bit — the next LCD frame redraw picks
// the new value up.
static void cmd_ui_instr_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_UI_INSTR_SET, reply, sizeof(reply));
    return;
  }
  u8 instr = payload[0] & 0x0f;   // 0..15
  ui_selected_instrument = instr;
  // Pages render content dependent on the cursor — request a low-prio LCD
  // pass so tests reading the LCD within SETTLE see the change instead of
  // racing the ~250ms cursor-flash periodic. Same fix as cmd_ui_track_set.
  seq_ui_display_update_req = 1;
  reply[0] = instr;
  reply[1] = 0x01;
  send_reply(port, CMD_UI_INSTR_SET, reply, sizeof(reply));
}


// CMD_UI_TRACK_SET payload: [track 0..15]
// Reply payload: [track, status]   status 0x01 = set, 0x02 = bad payload.
//
// Sets ui_selected_group + ui_selected_tracks so SEQ_UI_VisibleTrackGet
// returns `track`. Phase F.3 cross-track tests use this to park the visible
// track on Tx before pressing BOUNCE / opening PITCHGEN.
static void cmd_ui_track_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_UI_TRACK_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;        // 0..15
  ui_selected_group = track >> 2;       // 0..3
  // Stamp a single bit for track%4 into the group's nibble; clear the rest.
  ui_selected_tracks = (u16)(1 << (track & 3)) << (ui_selected_group * 4);
  // The PITCHGEN page (and any other page reading visible track) only
  // redraws on display_update_req — set it so the LCD reflects the new
  // visible track in time for the test's SETTLE-then-read pattern.
  seq_ui_display_update_req = 1;
  reply[0] = track;
  reply[1] = 0x01;
  send_reply(port, CMD_UI_TRACK_SET, reply, sizeof(reply));
}


// CMD_UI_TRACK_GET payload: (none)
// Reply payload: [visible_track 0..15, status 0x01]
//
// Returns SEQ_UI_VisibleTrackGet() — used to verify track selection (e.g. that
// the select row still switches tracks after a capture gesture, i.e. button_state
// wasn't corrupted).
static void cmd_ui_track_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2];
  reply[0] = SEQ_UI_VisibleTrackGet() & 0x0f;
  reply[1] = 0x01;
  send_reply(port, CMD_UI_TRACK_GET, reply, sizeof(reply));
}


// CMD_TRACK_DRUM_PAR_SET payload: [track, instr, step, value]
// Reply payload: [track, instr, step, status]   status 0x01 = set, 0x02 = bad.
//
// Direct write to a drum-slot Note par-layer step. Lets the harness seed
// a drum with content without going through ENGAGE — useful for tests
// that need to set up specific par-buffer state ahead of a UI gesture.
static void cmd_track_drum_par_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_TRACK_DRUM_PAR_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 instr = payload[1] & 0x0f;
  u8 step  = payload[2] & 0x7f;
  u8 value = payload[3] & 0x7f;
  reply[0] = track;
  reply[1] = instr;
  reply[2] = step;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum || tcc->link_par_layer_note < 0 ) {
    send_reply(port, CMD_TRACK_DRUM_PAR_SET, reply, sizeof(reply));
    return;
  }
  SEQ_PAR_Set(track, step, (u8)tcc->link_par_layer_note, instr, value);
  // PITCHGEN row-1 hint depends on whether the cursor drum has any non-zero
  // par bytes; force a redraw so the hint reflects the new content under
  // the test's SETTLE.
  seq_ui_display_update_req = 1;
  reply[3] = 0x01;
  send_reply(port, CMD_TRACK_DRUM_PAR_SET, reply, sizeof(reply));
}


// CMD_GENERATOR_TICK_FORCE payload: [track, instr]
// Reply payload: [track, instr, status]
//   status 0x01 = ok, 0x02 = bad payload, 0x03 = no allocated slot.
//
// Forces one synchronous mutate cycle on the generator slot at
// (track, instr), exactly as SEQ_GENERATOR_Tick would on a real track
// wrap. Used by behavioral tests to pin the §8 step 6 mutate-path
// carveouts (depth=0 freezing, LOCK preservation across actual mutate,
// MULT scaling) without orchestrating playback. ROLL doesn't cover
// these — ROLL bypasses rate/depth/MULT — so this is the only way to
// exercise the production mutate path under the harness.
static void cmd_generator_tick_force(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[3] = { 0, 0, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_GENERATOR_TICK_FORCE, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 instr = payload[1] & 0x0f;
  reply[0] = track;
  reply[1] = instr;
  s32 r = SEQ_GENERATOR_ForceMutate(track, instr);
  reply[2] = (r == 0) ? 0x01 : 0x03;
  send_reply(port, CMD_GENERATOR_TICK_FORCE, reply, sizeof(reply));
}


// CMD_GENERATOR_DIAL_SET payload: [track, instr, dial_id, value]
// Reply payload: [track, instr, dial_id, status]
//   status 0x01 = ok, 0x02 = bad payload (dial_id out of range), 0x03 = no slot.
//
// Dial IDs: 0=range_min, 1=range_max, 2=mutation_rate, 3=mutation_depth,
// 4=contour_shape. Value is the raw 7-bit dial value; the firmware does
// not range-clamp here (tests are responsible for staying inside the
// dial's documented range), but does normalize on read via the existing
// helpers. Lets behavioral tests set exact dial values without spamming
// encoder events through SEQ_UI_Var8_Inc.
#define DIAL_RANGE_MIN  0
#define DIAL_RANGE_MAX  1
#define DIAL_RATE       2
#define DIAL_DEPTH      3
#define DIAL_CONTOUR    4
static void cmd_generator_dial_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_GENERATOR_DIAL_SET, reply, sizeof(reply));
    return;
  }
  u8 track    = payload[0] & 0x0f;
  u8 instr    = payload[1] & 0x0f;
  u8 dial_id  = payload[2];
  u8 value    = payload[3] & 0x7f;
  reply[0] = track;
  reply[1] = instr;
  reply[2] = dial_id;

  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  if( g == NULL ) {
    reply[3] = 0x03;
    send_reply(port, CMD_GENERATOR_DIAL_SET, reply, sizeof(reply));
    return;
  }

  switch( dial_id ) {
    case DIAL_RANGE_MIN: g->range_min      = value; break;
    case DIAL_RANGE_MAX: g->range_max      = value; break;
    case DIAL_RATE:      g->mutation_rate  = value; break;
    case DIAL_DEPTH:     g->mutation_depth = value; break;
    case DIAL_CONTOUR:   g->contour_shape  = value; break;
    default:
      send_reply(port, CMD_GENERATOR_DIAL_SET, reply, sizeof(reply));
      return;
  }
  reply[3] = 0x01;
  send_reply(port, CMD_GENERATOR_DIAL_SET, reply, sizeof(reply));
}


// CMD_GENERATOR_MULT_SET payload: [track, instr, step, code]
// Reply payload: [track, instr, step, status]
//   status 0x01 = ok, 0x02 = bad payload (step ≥ LOOP_LEN), 0x03 = no slot.
//
// Sets the per-step MULT code directly. Code is masked to 4 bits; the
// mutate path interprets 0..3 as {0×, 0.5×, 1×, 2×} and treats codes
// 4..15 as the default (1×). The MultCycle gesture wraps at MULT_NUM,
// but this command lets tests stamp any 4-bit value (including reserved
// codes) for compatibility coverage.
static void cmd_generator_mult_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_GENERATOR_MULT_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 instr = payload[1] & 0x0f;
  u8 step  = payload[2];
  u8 code  = payload[3] & 0x0f;
  reply[0] = track;
  reply[1] = instr;
  reply[2] = step;
  if( step >= SEQ_GENERATOR_LOOP_LEN ) {
    send_reply(port, CMD_GENERATOR_MULT_SET, reply, sizeof(reply));
    return;
  }
  seq_generator_t *g = SEQ_GENERATOR_Get(track, instr);
  if( g == NULL ) {
    reply[3] = 0x03;
    send_reply(port, CMD_GENERATOR_MULT_SET, reply, sizeof(reply));
    return;
  }
  SEQ_GENERATOR_MultSet(g, step, code);
  reply[3] = 0x01;
  send_reply(port, CMD_GENERATOR_MULT_SET, reply, sizeof(reply));
}


// CMD_TRACK_DRUM_PAR_GET payload: [track, instr, step]
// Reply payload: [track, instr, step, value, status]
//   status 0x01 = ok, 0x02 = bad payload, 0x03 = track not drum-mode / no Note layer.
static void cmd_track_drum_par_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_TRACK_DRUM_PAR_GET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 instr = payload[1] & 0x0f;
  u8 step  = payload[2] & 0x7f;
  reply[0] = track;
  reply[1] = instr;
  reply[2] = step;
  seq_cc_trk_t *tcc = &seq_cc_trk[track];
  if( tcc->event_mode != SEQ_EVENT_MODE_Drum || tcc->link_par_layer_note < 0 ) {
    reply[4] = 0x03;
    send_reply(port, CMD_TRACK_DRUM_PAR_GET, reply, sizeof(reply));
    return;
  }
  s32 v = SEQ_PAR_Get(track, step, (u8)tcc->link_par_layer_note, instr);
  reply[3] = (u8)(v & 0x7f);
  reply[4] = 0x01;
  send_reply(port, CMD_TRACK_DRUM_PAR_GET, reply, sizeof(reply));
}


// CMD_TRACK_PAR_GET payload: [track, layer, instr, step]
// Reply payload: [track, layer, instr, step, value, status]   status 0x01 = ok, 0x02 = bad.
//
// General (mode-agnostic, layer-explicit) par-layer read — the note-track
// counterpart of CMD_TRACK_DRUM_PAR_GET. No event_mode gate: a thin wrapper
// over SEQ_PAR_Get so tests can observe ANY track's par output (e.g. a melodic
// track's Note/Velocity layers) and prove note-track bounce is byte-identical
// to drum-track bounce. Reads SEQ_PAR_OutputActive (the post-render mirror).
static void cmd_track_par_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_TRACK_PAR_GET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 layer = payload[1] & 0x7f;
  u8 instr = payload[2] & 0x7f;
  u8 step  = payload[3] & 0x7f;
  reply[0] = track;
  reply[1] = layer;
  reply[2] = instr;
  reply[3] = step;
  s32 v = SEQ_PAR_Get(track, step, layer, instr);
  reply[4] = (u8)(v & 0x7f);
  reply[5] = 0x01;
  send_reply(port, CMD_TRACK_PAR_GET, reply, sizeof(reply));
}


// CMD_TRACK_PAR_SET payload: [track, layer, instr, step, value]
// Reply payload: [track, layer, instr, step, value, status]   status 0x01 = set, 0x02 = bad.
//
// General (mode-agnostic, layer-explicit) par-layer write — the note-track
// counterpart of CMD_TRACK_DRUM_PAR_SET. Thin wrapper over SEQ_PAR_Set so tests
// can seed an arbitrary track/layer (no Drum gate).
static void cmd_track_par_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 5 ) {
    send_reply(port, CMD_TRACK_PAR_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 layer = payload[1] & 0x7f;
  u8 instr = payload[2] & 0x7f;
  u8 step  = payload[3] & 0x7f;
  u8 value = payload[4] & 0x7f;
  reply[0] = track;
  reply[1] = layer;
  reply[2] = instr;
  reply[3] = step;
  reply[4] = value;
  SEQ_PAR_Set(track, step, layer, instr, value);
  seq_ui_display_update_req = 1;
  reply[5] = 0x01;
  send_reply(port, CMD_TRACK_PAR_SET, reply, sizeof(reply));
}


// CMD_GLOBAL_SCALE_SET payload: [scale, root_selection, keyb_root]
// Reply payload: [scale, root_selection, keyb_root, status]   status 0x01 = ok.
//
// Sets the three globals SEQ_CORE_FTS_GetScaleAndRoot reads when a track has
// FORCE_SCALE on and no per-step Scale/Root par-layer override. Lets force-scale
// tests pin a deterministic key — board.reset() does NOT touch global scale, so
// without this the snapped pitches would depend on whatever the session last set.
//   scale          = seq_scale_table index (0 = Major)
//   root_selection = 0 -> root taken from keyb_root; >0 -> root = root_selection-1
//   keyb_root      = 0..11 (used only when root_selection == 0)
static void cmd_global_scale_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_GLOBAL_SCALE_SET, reply, sizeof(reply));
    return;
  }
  u8 scale          = payload[0] & 0x7f;
  u8 root_selection = payload[1] & 0x7f;
  u8 keyb_root      = payload[2] & 0x7f;
  seq_core_global_scale                = scale;
  seq_core_global_scale_root_selection = root_selection;
  seq_core_keyb_scale_root             = keyb_root;
  // Track 2: FTS is stack-resident — re-render so track_par_get pins read the
  // new scale's output without needing a play/stop cycle.
  SEQ_CORE_RenderDirtySetAll();
  reply[0] = scale;
  reply[1] = root_selection;
  reply[2] = keyb_root;
  reply[3] = 0x01;
  send_reply(port, CMD_GLOBAL_SCALE_SET, reply, sizeof(reply));
}


// CMD_TENSION_SET payload: [gravity_biased]  (gravity = gravity_biased - 64,
// range -64..+63; 64 = detent/pass-through). Reply: [gravity_biased, status].
// Sets the global GRAVITY dial and dirties all tracks so the change renders.
// GRIP (per-track) is set via the existing CMD_CC_SET on SEQ_CC_TENSION_GRIP
// (0x9A); SHADE via CMD_GLOBAL_SCALE_SET — so this is the only new verb needed.
static void cmd_tension_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_TENSION_SET, reply, sizeof(reply));
    return;
  }
  s8 gravity = (s8)((s16)(payload[0] & 0x7f) - 64);
  seq_core_tension_gravity = gravity;
  SEQ_CORE_RenderDirtySetAll();
  reply[0] = payload[0] & 0x7f;
  reply[1] = 0x01;
  send_reply(port, CMD_TENSION_SET, reply, sizeof(reply));
}


// CMD_TENSION_BAND_GET payload: [gravity_biased, (opt) track, (opt) bus].
// Reply: [zone, band_lo, band_hi, status] where band = band_lo | (band_hi << 7)
// is the 12-bit target PC mask for the gravity's zone against the chord held on
// `bus` and the current global scale/root. zone: 0=detent,1..6=DRONE..SLIP.
// Pure function — no render/transport needed (the §8 band-mask pinning verb).
static void cmd_tension_band_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_TENSION_BAND_GET, reply, sizeof(reply));
    return;
  }
  s8 gravity = (s8)((s16)(payload[0] & 0x7f) - 64);
  u8 track   = (plen >= 2) ? (payload[1] & 0x0f) : 0;
  u8 bus     = (plen >= 3) ? (payload[2] & 0x03) : seq_cc_trk[track].chordmask_bus;
  u8 zone = 0;
  u16 band = SEQ_CORE_TensionBandMask(gravity, bus, &zone);
  reply[0] = zone;
  reply[1] = band & 0x7f;
  reply[2] = (band >> 7) & 0x1f;
  reply[3] = 0x01;
  send_reply(port, CMD_TENSION_BAND_GET, reply, sizeof(reply));
}


// CMD_TENSION_GET (no payload). Reply: [gravity_biased, status] where
// gravity = gravity_biased - 64. Reads the current global GRAVITY dial — lets
// the harness verify the cockpit GRAVITY encoder / RESOLVE button and (Stage C)
// watch the RESOLVE ramp land on the detent.
static void cmd_tension_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2];
  reply[0] = (u8)(((s16)seq_core_tension_gravity + 64) & 0x7f);
  reply[1] = 0x01;
  send_reply(port, CMD_TENSION_GET, reply, sizeof(reply));
}


// CMD_TENSION_RESOLVE (no payload). Triggers the RESOLVE ramp (or instant snap
// when stopped). Reply: [status]. The harness reads CMD_TENSION_GET afterward to
// watch GRAVITY land on the detent.
static void cmd_tension_resolve(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[1] = { 0x01 };
  SEQ_CORE_TensionResolve();
  send_reply(port, CMD_TENSION_RESOLVE, reply, sizeof(reply));
}


// CMD_TRACK_CONFIG payload: [track, midi_port, midi_chn]
//   track:     0..15
//   midi_port: raw mios32_midi_port_t value (e.g. 0x10 = USB0/USB1...)
//              UI shows USB1 == enum USB0 == 0x10 (port numbering off-by-one).
//   midi_chn:  0..15
// Reply payload: [track, midi_port, midi_chn, status]   status 0x01 = ok, 0x02 = bad.
//
// Writes directly via SEQ_CC_Set so the change goes through the same path the
// UI would. Useful in tests to force a track's output to USB1 (the harness's
// MIDI port) regardless of how the user has the pattern configured.
static void cmd_track_config(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_TRACK_CONFIG, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0] & 0x0f;
  u8 midi_port_byte = payload[1] & 0x7f;
  u8 midi_chn = payload[2] & 0x0f;

  // The wire byte for midi_port is the full enum value (e.g. 0x10 = USB0).
  // The wire is 7-bit-safe; USB0..USB7 = 0x10..0x17 all fit. UART0..3 = 0x20..0x23
  // and IIC0..7 = 0x30..0x37, also fit. OSC ports start at 0x70 — still fits.
  SEQ_CC_Set(track, SEQ_CC_MIDI_PORT, midi_port_byte);
  SEQ_CC_Set(track, SEQ_CC_MIDI_CHANNEL, midi_chn);

  reply[0] = track;
  reply[1] = midi_port_byte;
  reply[2] = midi_chn;
  reply[3] = 0x01;
  send_reply(port, CMD_TRACK_CONFIG, reply, sizeof(reply));
}


// CMD_TICK_QUERY: no payload.
// Reply payload (all bytes 7-bit safe):
//   [is_running, mute_lo, mute_hi, trk0_playmode, trk0_midi_port, trk0_midi_chn,
//    trk0_event_mode, port_mute_usb0..3, slaveclk_mute, soloed_lo, soloed_hi,
//    trk0_lfo_cc_muted, trk0_layer_muted]
static void cmd_tick_query(mios32_midi_port_t port)
{
  seq_cc_trk_t *tcc0 = &seq_cc_trk[0];
  u8 reply[22];
  reply[0]  = SEQ_BPM_IsRunning() ? 1 : 0;
  reply[1]  = seq_core_trk_muted & 0x7f;
  reply[2]  = (seq_core_trk_muted >> 7) & 0x7f;
  reply[3]  = tcc0->playmode & 0x7f;
  reply[4]  = tcc0->midi_port & 0x7f;
  reply[5]  = tcc0->midi_chn & 0x7f;
  reply[6]  = tcc0->event_mode & 0x7f;
  reply[7]  = SEQ_MIDI_PORT_OutMuteGet(USB0) & 0x01;
  reply[8]  = SEQ_MIDI_PORT_OutMuteGet(USB1) & 0x01;
  reply[9]  = SEQ_MIDI_PORT_OutMuteGet(USB2) & 0x01;
  reply[10] = SEQ_MIDI_PORT_OutMuteGet(USB3) & 0x01;
  reply[11] = seq_core_slaveclk_mute & 0x7f;
  reply[12] = seq_core_trk_soloed & 0x7f;
  reply[13] = (seq_core_trk_soloed >> 7) & 0x7f;
  reply[14] = (seq_core_trk[0].lfo_cc_muted_from_midi) & 0x7f;
  reply[15] = seq_core_trk[0].layer_muted & 0x7f;
  // bpm tick: u32, pack into 5 7-bit bytes.
  u32 t = SEQ_BPM_TickGet();
  reply[16] = (t >> 0)  & 0x7f;
  reply[17] = (t >> 7)  & 0x7f;
  reply[18] = (t >> 14) & 0x7f;
  reply[19] = (t >> 21) & 0x7f;
  reply[20] = (t >> 28) & 0x0f;
  // track 0's current step (per-track state)
  reply[21] = seq_core_trk[0].step & 0x7f;
  send_reply(port, CMD_TICK_QUERY, reply, sizeof(reply));
}


// CMD_CC_GET payload: [track, cc_hi, cc_lo]
//   cc value range 0..255 = (cc_hi << 7) | cc_lo. cc_hi must be 0 or 1.
// Reply payload: [track, cc_hi, cc_lo, value_hi, value_lo, status]
//   status 0x01 = ok, 0x02 = bad payload, 0x03 = invalid track, 0x04 = unmapped CC.
static void cmd_cc_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_CC_GET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0];
  u8 cc_hi = payload[1] & 0x01;
  u8 cc_lo = payload[2] & 0x7f;
  u16 cc = ((u16)cc_hi << 7) | cc_lo;

  reply[0] = track;
  reply[1] = cc_hi;
  reply[2] = cc_lo;

  if( track >= SEQ_CORE_NUM_TRACKS ) {
    reply[5] = 0x03;
  } else {
    s32 v = SEQ_CC_Get(track, (u8)cc);
    if( v < 0 ) {
      reply[5] = 0x04;
    } else {
      reply[3] = ((u8)v >> 7) & 0x01;
      reply[4] = (u8)v & 0x7f;
      reply[5] = 0x01;
    }
  }
  send_reply(port, CMD_CC_GET, reply, sizeof(reply));
}


// CMD_CC_SET payload: [track, cc_hi, cc_lo, value_hi, value_lo]
// Reply payload: [track, cc_hi, cc_lo, value_hi, value_lo, status]
//   status 0x01 = ok, 0x02 = bad payload, 0x03 = invalid track, 0x04 = SEQ_CC_Set rejected.
static void cmd_cc_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 5 ) {
    send_reply(port, CMD_CC_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0];
  u8 cc_hi = payload[1] & 0x01;
  u8 cc_lo = payload[2] & 0x7f;
  u8 v_hi  = payload[3] & 0x01;
  u8 v_lo  = payload[4] & 0x7f;
  u16 cc = ((u16)cc_hi << 7) | cc_lo;
  u8 value = ((u8)v_hi << 7) | v_lo;

  reply[0] = track;
  reply[1] = cc_hi;
  reply[2] = cc_lo;
  reply[3] = v_hi;
  reply[4] = v_lo;

  if( track >= SEQ_CORE_NUM_TRACKS ) {
    reply[5] = 0x03;
  } else {
    s32 r = SEQ_CC_Set(track, (u8)cc, value);
    reply[5] = (r < 0) ? 0x04 : 0x01;
  }
  send_reply(port, CMD_CC_SET, reply, sizeof(reply));
}


// CMD_PLAY_SECTION_GET payload: [track]
// Reply payload: [track, value, status]
static void cmd_play_section_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[3] = { 0, 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_PLAY_SECTION_GET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0];
  reply[0] = track;
  if( track >= SEQ_CORE_NUM_TRACKS ) {
    reply[2] = 0x03;
  } else {
    reply[1] = seq_core_trk[track].play_section & 0x7f;
    reply[2] = 0x01;
  }
  send_reply(port, CMD_PLAY_SECTION_GET, reply, sizeof(reply));
}


// CMD_PLAY_SECTION_SET payload: [track, value]
// Reply payload: [track, value, status]
static void cmd_play_section_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[3] = { 0, 0, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_PLAY_SECTION_SET, reply, sizeof(reply));
    return;
  }
  u8 track = payload[0];
  u8 value = payload[1] & 0x7f;
  reply[0] = track;
  reply[1] = value;
  if( track >= SEQ_CORE_NUM_TRACKS ) {
    reply[2] = 0x03;
  } else {
    seq_core_trk[track].play_section = value;
    reply[2] = 0x01;
  }
  send_reply(port, CMD_PLAY_SECTION_SET, reply, sizeof(reply));
}


// CMD_BOUNCE payload: [src_track, dst_group, dst_bank, dst_pattern]
// Reply payload: [src_track, dst_bank, dst_pattern, commit_ok, dispatch_status]
//   commit_ok 0x01 = SEQ_CORE_CaptureToSlot returned >=0, 0x00 = returned <0.
//   dispatch_status 0x01 = ok, 0x02 = bad payload.
//
// Captures src_track's computed output (lossless) into the (bank, pattern)
// slot. Synchronous: the parser blocks until SEQ_CORE_CaptureToSlot returns.
// SD I/O can take 100ms+, so the harness must wait_for_sysex with a generous
// timeout (~3s). PatternWrite + the in-RAM snapshot/restore run inside the call.
static void cmd_bounce(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_BOUNCE, reply, sizeof(reply));
    return;
  }
  u8 src_track    = payload[0];
  u8 dst_group    = payload[1];
  u8 dst_bank     = payload[2] & 0x07;
  u8 dst_pattern  = payload[3] & 0x7f;

  s32 r = SEQ_CORE_CaptureToSlot(src_track, dst_group, dst_bank, dst_pattern);

  reply[0] = src_track;
  reply[1] = dst_bank;
  reply[2] = dst_pattern;
  reply[3] = (r >= 0) ? 0x01 : 0x00;
  reply[4] = 0x01;
  send_reply(port, CMD_BOUNCE, reply, sizeof(reply));
}


// CMD_CAPTURE_TO_TRACK payload: [src_track, dst_track]
// Reply payload: [src_track, dst_track, ok, dispatch_status]
//   ok 0x01 = SEQ_CORE_CaptureToTrack returned >=0, 0x00 = returned <0.
//   dispatch_status 0x01 = ok, 0x02 = bad payload.
//
// Captures src_track's computed output (lossless) onto dst_track in the current
// pattern (RAM only — no SD). dst inherits src's event-mode/geometry/lower-48
// CCs, then its generative CC is reset.
static void cmd_capture_to_track(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_CAPTURE_TO_TRACK, reply, sizeof(reply));
    return;
  }
  u8 src_track = payload[0];
  u8 dst_track = payload[1];

  s32 r = SEQ_CORE_CaptureToTrack(src_track, dst_track);

  reply[0] = src_track;
  reply[1] = dst_track;
  reply[2] = (r >= 0) ? 0x01 : 0x00;
  reply[3] = 0x01;
  send_reply(port, CMD_CAPTURE_TO_TRACK, reply, sizeof(reply));
}


// CMD_CAPTURE_TO_SLOT_TRACK payload: [src_track, dst_track, dst_bank, dst_pattern]
// Reply payload: [src_track, dst_track, ok, dispatch_status]
//   ok 0x01 = SEQ_CORE_CaptureToSlotTrack returned >=0, 0x00 = returned <0.
//   dispatch_status 0x01 = ok, 0x02 = bad payload.
//
// Renders src_track's computed output into dst_track of slot (bank, pattern),
// persisted to SD, preserving the slot's other tracks. Synchronous (two SD ops).
static void cmd_capture_to_slot_track(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_CAPTURE_TO_SLOT_TRACK, reply, sizeof(reply));
    return;
  }
  u8 src_track   = payload[0];
  u8 dst_track   = payload[1];
  u8 dst_bank    = payload[2] & 0x07;
  u8 dst_pattern = payload[3] & 0x7f;

  s32 r = SEQ_CORE_CaptureToSlotTrack(src_track, dst_track, dst_bank, dst_pattern);

  reply[0] = src_track;
  reply[1] = dst_track;
  reply[2] = (r >= 0) ? 0x01 : 0x00;
  reply[3] = 0x01;
  send_reply(port, CMD_CAPTURE_TO_SLOT_TRACK, reply, sizeof(reply));
}


// CMD_TRACK_LOAD payload: [dst_track, src_bank, src_pattern, src_slot_track]
// Reply: [dst_track, bank, pattern, slot_track, load_ok, dispatch_status]
//   dispatch_status 0x01 = ok, 0x02 = bad payload, 0x03 = invalid track.
//
// The RECOMBINE pull verb: reads ONE stored track section into dst_track (any
// bank x pattern x section -> any live track), arms the track undo, runs the
// per-track census fan, bar-aligns. slot_track is deliberately NOT masked so
// out-of-range refusal (load_ok=0) is pinnable. Synchronous SD read (100ms+).
static void cmd_track_load(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 4 ) {
    send_reply(port, CMD_TRACK_LOAD, reply, sizeof(reply));
    return;
  }
  u8 dst_track   = payload[0];
  u8 bank        = payload[1];
  u8 pattern     = payload[2] & 0x7f;
  u8 slot_track  = payload[3];

  reply[0] = dst_track;
  reply[1] = bank;
  reply[2] = pattern;
  reply[3] = slot_track;

  if( dst_track >= SEQ_CORE_NUM_TRACKS ) {
    reply[5] = 0x03;
  } else {
    s32 r = SEQ_CORE_LoadTrackFromSlot(dst_track, bank, pattern, slot_track);
    reply[4] = (r >= 0) ? 0x01 : 0x00;
    reply[5] = 0x01;
  }
  send_reply(port, CMD_TRACK_LOAD, reply, sizeof(reply));
}


// CMD_TRACK_UNDO payload: none.
// Reply: [restored_track (0x7f = none), restored_ok, dispatch_status]
//
// One-shot restore of the track undo victim (most recent destructive
// track-grain verb). RAM only, synchronous.
static void cmd_track_undo(mios32_midi_port_t port)
{
  u8 reply[3] = { 0x7f, 0x00, 0x01 };
  s32 r = SEQ_CORE_TrackUndoRestore();
  if( r >= 0 ) {
    reply[0] = (u8)r & 0x7f;
    reply[1] = 0x01;
  }
  send_reply(port, CMD_TRACK_UNDO, reply, sizeof(reply));
}


// CMD_TRACK_UNDO_QUERY payload: none.
// Reply: [valid, kind, track, dispatch_status] — non-consuming state peek.
static void cmd_track_undo_query(mios32_midi_port_t port)
{
  u8 valid = 0, kind = 0, track = 0;
  SEQ_CORE_TrackUndoInfoGet(&valid, &kind, &track);
  u8 reply[4] = { valid, kind, (u8)(track & 0x7f), 0x01 };
  send_reply(port, CMD_TRACK_UNDO_QUERY, reply, sizeof(reply));
}


// CMD_SESSION_CREATE payload: [name_bytes...]  (1..8 ASCII chars, 7-bit)
// Reply: [armed_ok, dispatch_status]
//   dispatch_status 0x01 ok, 0x02 bad payload, 0x03 dir create failed,
//   0x04 session already exists (refused — never clobbers).
// Creates /SESSIONS/<name> and arms the ASYNC format (the low-prio task in
// app.c writes the bank/config files — takes seconds — then loads the new
// session and stores its name). Poll CMD_SESSION_NAME_GET until it returns
// the new name. Mirrors the SAVE menu's New Session flow (DoSessionSaveOrNew
// in seq_ui_menu.c). NOTE: arming stops the sequencer and clears live state.
static void cmd_session_create(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 || plen > 8 ) {
    send_reply(port, CMD_SESSION_CREATE, reply, sizeof(reply));
    return;
  }

  char name[9];
  u8 i;
  for(i=0; i<plen; ++i)
    name[i] = (char)payload[i];
  name[plen] = 0;

  char path[30];
  sprintf(path, "%s/%s", SEQ_FILE_SESSION_PATH, name);

  s32 status;
  MUTEX_SDCARD_TAKE;
  if( FILE_DirExists(path) == 1 ) {
    status = -2; // exists — refuse
  } else {
    FILE_MakeDir(path);
    status = (FILE_DirExists(path) == 1) ? 0 : -1;
  }
  MUTEX_SDCARD_GIVE;

  if( status == -2 ) {
    reply[1] = 0x04;
  } else if( status < 0 ) {
    reply[1] = 0x03;
  } else {
    SEQ_FILE_CreateSession(name, 1); // arms the async format
    reply[0] = 0x01;
    reply[1] = 0x01;
  }
  send_reply(port, CMD_SESSION_CREATE, reply, sizeof(reply));
}


// CMD_DIRTY_QUERY — FEARLESS SWITCHING diagnostics. No payload.
// Reply payload: [dirty_mask, wb_cnt_lo7, wb_cnt_mid7, wb_cnt_hi7, status]
//   dirty_mask = seq_pattern_dirty (bit per group, 4 bits used)
//   wb_cnt     = seq_pattern_writeback_count, 21 LSBs, 7 bits per byte
static void cmd_dirty_query(mios32_midi_port_t port)
{
  u8 reply[5];
  u32 cnt = seq_pattern_writeback_count;
  reply[0] = seq_pattern_dirty & 0x7f;
  reply[1] = (u8)(cnt & 0x7f);
  reply[2] = (u8)((cnt >> 7) & 0x7f);
  reply[3] = (u8)((cnt >> 14) & 0x7f);
  reply[4] = 0x01;
  send_reply(port, CMD_DIRTY_QUERY, reply, sizeof(reply));
}


// CMD_DIRTY_SET payload: [group, value]   value 0 = clear, 1 = set
// Reply payload: [group, value, dirty_mask_after, status]
// Test-only knob: lets the harness pin "clean switch skips writeback" without
// rebooting, and force a writeback without making a real edit.
static void cmd_dirty_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[4] = { 0, 0, 0, 0x02 };
  if( plen < 2 ) {
    send_reply(port, CMD_DIRTY_SET, reply, sizeof(reply));
    return;
  }
  u8 group = payload[0];
  u8 value = payload[1];
  reply[0] = group;
  reply[1] = value;

  if( group >= SEQ_CORE_NUM_GROUPS ) {
    reply[3] = 0x03;
  } else {
    MIOS32_IRQ_Disable();
    if( value )
      seq_pattern_dirty |= (1 << group);
    else
      seq_pattern_dirty &= ~(1 << group);
    MIOS32_IRQ_Enable();
    reply[3] = 0x01;
  }
  reply[2] = seq_pattern_dirty & 0x7f;
  send_reply(port, CMD_DIRTY_SET, reply, sizeof(reply));
}


// CMD_PATTERN_CHANGE payload: [group, bank, pattern]
// Reply payload: [group, bank, pattern, change_ok, dispatch_status]
// Unlike CMD_PATTERN_LOAD (a RAW load that deliberately bypasses the FEARLESS
// writeback) this goes through SEQ_PATTERN_Change — the real switch path, so
// a dirty outgoing group is written back first. With the sequencer stopped it
// takes the immediate branch (synchronous: SD write+read; generous timeout).
static void cmd_pattern_change(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_PATTERN_CHANGE, reply, sizeof(reply));
    return;
  }
  u8 group   = payload[0];
  u8 bank    = payload[1] & 0x07;
  u8 pattern = payload[2] & 0x7f;

  reply[0] = group;
  reply[1] = bank;
  reply[2] = pattern;

  if( group >= SEQ_CORE_NUM_GROUPS ) {
    reply[4] = 0x03;
  } else {
    seq_pattern_t pat;
    pat.ALL = 0;
    pat.bank = bank;
    pat.pattern = pattern;
    s32 r = SEQ_PATTERN_Change(group, pat, 0);
    reply[3] = (r >= 0) ? 0x01 : 0x00;
    reply[4] = 0x01;
  }
  send_reply(port, CMD_PATTERN_CHANGE, reply, sizeof(reply));
}


// CMD_CHECKPOINT — FEARLESS SWITCHING Stage C. No payload. Blesses all four
// groups' live state (incl. generator state) into the session's anchor file
// (MBSEQ_AN.V4), lazy-creating it on first use. Reply payload: [ok, status].
// Synchronous: 4 group-record SD writes (~a session-save's worth) — generous
// timeout on the host wrapper.
static void cmd_checkpoint(mios32_midi_port_t port)
{
  s32 r = SEQ_PATTERN_Checkpoint();
  u8 reply[2] = { (r >= 0) ? 0x01 : 0x00, 0x01 };
  send_reply(port, CMD_CHECKPOINT, reply, sizeof(reply));
}


// CMD_REVERT — FEARLESS SWITCHING Stage C. No payload. Restores all four
// groups from the blessed anchor (the organism comes back to the checkpoint),
// then sets every group dirty. Refuses cleanly when the session has no anchor
// yet (REVERT before the first CHECKPOINT, or after a session hop): ok=0,
// status=0x03 — distinct from an I/O failure (ok=0, status=0x01) so the pin
// can tell a no-anchor refusal from a real error. Reply payload: [ok, status].
static void cmd_revert(mios32_midi_port_t port)
{
  u8 reply[2];
  if( SEQ_PATTERN_AnchorPresent() <= 0 ) {
    reply[0] = 0x00; reply[1] = 0x03; // clean refuse: no anchor for this session
  } else {
    s32 r = SEQ_PATTERN_Revert();
    reply[0] = (r >= 0) ? 0x01 : 0x00;
    reply[1] = 0x01;
  }
  send_reply(port, CMD_REVERT, reply, sizeof(reply));
}


// CMD_ANCHOR_PRESENT — FEARLESS SWITCHING Stage C. No payload. 1 if the
// current session has a blessed anchor. Reply payload: [present, status].
static void cmd_anchor_present(mios32_midi_port_t port)
{
  u8 reply[2] = { (SEQ_PATTERN_AnchorPresent() > 0) ? 0x01 : 0x00, 0x01 };
  send_reply(port, CMD_ANCHOR_PRESENT, reply, sizeof(reply));
}


// CMD_PHRASE_CAPTURE — PHRASES bundle. Payload: [n]. Snapshots the live organism
// into phrase n (4 group-record SD writes into MBSEQ_PH.V4, lazy-created).
// CHECKPOINT generalized to N slots. Reply payload: [ok, status].
static void cmd_phrase_capture(mios32_midi_port_t port, u8 *payload, u32 len)
{
  u8 reply[2];
  if( len < 1 ) {
    reply[0] = 0x00; reply[1] = 0x02; // malformed
  } else {
    s32 r = SEQ_PATTERN_PhraseCapture(payload[0]);
    reply[0] = (r >= 0) ? 0x01 : 0x00;
    reply[1] = 0x01;
  }
  send_reply(port, CMD_PHRASE_CAPTURE, reply, sizeof(reply));
}


// CMD_PHRASE_RECALL — PHRASES bundle. Payload: [n]. Restores the live organism
// from phrase n (REVERT generalized), then sets every group dirty. Refuses an
// un-captured phrase cleanly: ok=0, status=0x03 — distinct from an I/O failure
// (ok=0, status=0x01), mirroring CMD_REVERT. Reply payload: [ok, status].
static void cmd_phrase_recall(mios32_midi_port_t port, u8 *payload, u32 len)
{
  u8 reply[2];
  if( len < 1 ) {
    reply[0] = 0x00; reply[1] = 0x02; // malformed
  } else if( SEQ_PATTERN_PhrasePresent(payload[0]) <= 0 ) {
    reply[0] = 0x00; reply[1] = 0x03; // clean refuse: phrase not captured
  } else {
    s32 r = SEQ_PATTERN_PhraseRecall(payload[0]);
    reply[0] = (r >= 0) ? 0x01 : 0x00;
    reply[1] = 0x01;
  }
  send_reply(port, CMD_PHRASE_RECALL, reply, sizeof(reply));
}


// CMD_PHRASE_PRESENT — PHRASES bundle. Payload: [n]. 1 if phrase n has been
// captured this session. Reply payload: [present, status].
static void cmd_phrase_present(mios32_midi_port_t port, u8 *payload, u32 len)
{
  u8 reply[2];
  if( len < 1 ) {
    reply[0] = 0x00; reply[1] = 0x02; // malformed
  } else {
    reply[0] = (SEQ_PATTERN_PhrasePresent(payload[0]) > 0) ? 0x01 : 0x00;
    reply[1] = 0x01;
  }
  send_reply(port, CMD_PHRASE_PRESENT, reply, sizeof(reply));
}


// CMD_PATTERN_LOAD payload: [group, bank, pattern]
// Reply payload: [group, bank, pattern, load_ok, dispatch_status]
//   load_ok 0x01 = SEQ_PATTERN_Load returned >=0, 0x00 = returned <0.
//
// Synchronous: SD read can take 100ms+. Same timeout caveat as CMD_BOUNCE.
// NOTE (FEARLESS): raw load — deliberately bypasses the dirty writeback so
// tests can replace live state without committing it. Use CMD_PATTERN_CHANGE
// to exercise the real switch path.
static void cmd_pattern_load(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_PATTERN_LOAD, reply, sizeof(reply));
    return;
  }
  u8 group   = payload[0];
  u8 bank    = payload[1] & 0x07;
  u8 pattern = payload[2] & 0x7f;

  reply[0] = group;
  reply[1] = bank;
  reply[2] = pattern;

  if( group >= SEQ_CORE_NUM_GROUPS ) {
    reply[4] = 0x03;
  } else {
    seq_pattern_t pat;
    pat.ALL = 0;
    pat.bank = bank;
    pat.pattern = pattern;
    s32 r = SEQ_PATTERN_Load(group, pat);
    reply[3] = (r >= 0) ? 0x01 : 0x00;
    reply[4] = 0x01;
  }
  send_reply(port, CMD_PATTERN_LOAD, reply, sizeof(reply));
}


// CMD_BANK_CREATE payload: [bank]. (Re)creates the bank file in the current
// session with the CURRENT firmware's pattern-slot size — i.e. V3-sized ext
// blocks, so chord-mask + tension GRIP CCs fit and persist. DESTROYS that bank's
// existing patterns. Reply: [bank, ok, status]. (All SD sessions on this dev
// device are disposable test work, so reformatting a scratch bank is free — but
// never recreate the AUTOTEST baseline bank the suite depends on.)
static void cmd_bank_create(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[3] = { 0, 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_BANK_CREATE, reply, sizeof(reply));
    return;
  }
  u8 bank = payload[0] & 0x07;
  reply[0] = bank;
  s32 r = SEQ_FILE_B_Create(seq_file_session_name, bank);
  if( r >= 0 )
    r = SEQ_FILE_B_Open(seq_file_session_name, bank);  // make it usable now
  reply[1] = (r >= 0) ? 0x01 : 0x00;
  reply[2] = 0x01;
  send_reply(port, CMD_BANK_CREATE, reply, sizeof(reply));
}


// CMD_PATTERN_SAVE payload: [group, bank, pattern]. Persists the working group's
// current in-RAM pattern (all 4 of its tracks: par/trg layers + the persisted
// CCs) to the bank slot on SD, so a host-configured rig survives reboot. Mirrors
// cmd_pattern_load but calls SEQ_PATTERN_Save. Reply: [group, bank, pattern, ok,
// status]. NOTE: a track's GRIP (CC 0x9A) is outside the persisted ext-CC range
// until the Tension-Workbench Stage-D widening, so a reloaded rig keeps its
// notes/gates/config but GRIP must be re-applied (the rig builder sets it live).
static void cmd_pattern_save(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 3 ) {
    send_reply(port, CMD_PATTERN_SAVE, reply, sizeof(reply));
    return;
  }
  u8 group   = payload[0];
  u8 bank    = payload[1] & 0x07;
  u8 pattern = payload[2] & 0x7f;

  reply[0] = group;
  reply[1] = bank;
  reply[2] = pattern;

  if( group >= SEQ_CORE_NUM_GROUPS ) {
    reply[4] = 0x03;
  } else {
    seq_pattern_t pat;
    pat.ALL = 0;
    pat.bank = bank;
    pat.pattern = pattern;
    s32 r = SEQ_PATTERN_Save(group, pat);
    reply[3] = (r >= 0) ? 0x01 : 0x00;
    reply[4] = 0x01;
  }
  send_reply(port, CMD_PATTERN_SAVE, reply, sizeof(reply));
}


// CMD_SESSION_LOAD payload: [name_bytes...]   (1..12 ASCII chars, all 7-bit)
// Reply payload: [load_ok, dispatch_status, name_len, name_bytes...]
//   dispatch_status 0x01 = ok, 0x02 = empty payload, 0x03 = name too long.
//   load_ok 0x01 = SEQ_FILE_LoadAllFiles succeeded; 0x00 = load failed and the
//   previous session name was restored (matching OpenSession() in seq_ui_menu.c).
// The active session name AFTER the call is always echoed so the harness can
// verify state regardless of success or failure.
//
// Synchronous: the load chain reads B/M/S/G/BM/C across all banks for the new
// session. SD I/O can take several seconds; harness must use a generous timeout.
static void cmd_session_load(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 load_ok = 0x00;
  u8 dispatch_status = 0x02;  // empty payload by default

  if( plen >= 1 && plen <= 12 ) {
    // FEARLESS SWITCHING: mirror the seq_ui_menu.c session-switch path —
    // persist dirty groups into the OUTGOING session before the name changes.
    SEQ_PATTERN_WritebackAllDirty();

    char prev_name[13];
    strcpy(prev_name, seq_file_session_name);

    for(u32 i=0; i<plen; ++i)
      seq_file_session_name[i] = payload[i] & 0x7f;
    seq_file_session_name[plen] = 0;

    MUTEX_SDCARD_TAKE;
    s32 status = SEQ_FILE_LoadAllFiles(0); // excluding HW config
    MUTEX_SDCARD_GIVE;

    if( status < 0 ) {
      strcpy(seq_file_session_name, prev_name);
      load_ok = 0x00;
    } else {
      MUTEX_SDCARD_TAKE;
      SEQ_FILE_StoreSessionName();
      MUTEX_SDCARD_GIVE;
      load_ok = 0x01;
    }
    dispatch_status = 0x01;
  } else if( plen > 12 ) {
    dispatch_status = 0x03;
  }

  u32 nlen = strlen(seq_file_session_name);
  if( nlen > 12 ) nlen = 12;

  u8 reply[16];
  u32 rlen = 0;
  reply[rlen++] = load_ok;
  reply[rlen++] = dispatch_status;
  reply[rlen++] = (u8)nlen;
  for(u32 i=0; i<nlen; ++i)
    reply[rlen++] = seq_file_session_name[i] & 0x7f;

  send_reply(port, CMD_SESSION_LOAD, reply, rlen);
}


// CMD_SESSION_NAME_GET: no payload.
// Reply payload: [name_len, name_bytes...]
static void cmd_session_name_get(mios32_midi_port_t port)
{
  u32 nlen = strlen(seq_file_session_name);
  if( nlen > 12 ) nlen = 12;

  u8 reply[14];
  reply[0] = (u8)nlen;
  for(u32 i=0; i<nlen; ++i)
    reply[1+i] = seq_file_session_name[i] & 0x7f;
  send_reply(port, CMD_SESSION_NAME_GET, reply, 1 + nlen);
}


// CMD_TRG_BYTE_GET payload: [track, trg_layer, trg_instrument, step8_start, step8_count]
//   Reads a span of trigger bytes for diagnostic comparison. For each byte we
//   return BOTH the source (seq_trg_layer_value) and the phase-A output mirror
//   (seq_trg_output_value), so divergence between the two is detectable.
// Reply payload: [track, trg_layer, trg_instrument, step8_start, step8_count, status, packed_pairs...]
//   status 0x01 = ok, 0x02 = bad payload, 0x03 = invalid args (track/layer/instr/range).
//   packed_pairs is pack7-encoded sequence of 2*step8_count bytes:
//     layer_0, output_0, layer_1, output_1, ...
//   step8_count is capped at 32 (64 raw bytes -> ~80 wire bytes after pack7).
static void cmd_trg_byte_get(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6 + 96] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 5 ) {
    send_reply(port, CMD_TRG_BYTE_GET, reply, 6);
    return;
  }
  u8 track       = payload[0];
  u8 trg_layer   = payload[1];
  u8 trg_instr   = payload[2];
  u8 step8_start = payload[3];
  u8 step8_count = payload[4];

  reply[0] = track;
  reply[1] = trg_layer;
  reply[2] = trg_instr;
  reply[3] = step8_start;
  reply[4] = step8_count;

  if( track >= SEQ_CORE_NUM_TRACKS ||
      step8_count == 0 || step8_count > 32 ) {
    reply[5] = 0x03;
    send_reply(port, CMD_TRG_BYTE_GET, reply, 6);
    return;
  }

  u8 num_t_layers = (u8)SEQ_TRG_NumLayersGet(track);
  u8 num_t_instr  = (u8)SEQ_TRG_NumInstrumentsGet(track);
  u8 num_t_step8  = (u8)(SEQ_TRG_NumStepsGet(track) / 8);
  if( trg_layer >= num_t_layers || trg_instr >= num_t_instr ||
      (u16)step8_start + step8_count > num_t_step8 ) {
    reply[5] = 0x03;
    send_reply(port, CMD_TRG_BYTE_GET, reply, 6);
    return;
  }

  u16 base_ix = (u16)trg_instr * num_t_layers * num_t_step8
              + (u16)trg_layer * num_t_step8
              + step8_start;

  // Build raw [layer,output] pairs, then pack7. "output" reports the active
  // half of the double-buffered mirror (what the tick currently reads).
  u8 raw[64];
  u8 *trg_out_active = SEQ_TRG_OutputActive(track);
  for(u8 i=0; i<step8_count; ++i) {
    raw[2*i]     = seq_trg_layer_value[track][base_ix + i];
    raw[2*i + 1] = trg_out_active[base_ix + i];
  }

  reply[5] = 0x01;
  u32 w = 6 + pack7(raw, (u32)step8_count * 2, &reply[6]);
  send_reply(port, CMD_TRG_BYTE_GET, reply, w);
}


// CMD_TRG_BYTE_SET payload: [track, trg_layer, trg_instr, step8, val_lo7, val_hi1]
//   Writes one byte (8 steps) of a trigger layer's SOURCE (seq_trg_layer_value)
//   via SEQ_TRG_Set8 — the write half of CMD_TRG_BYTE_GET. Lets the harness
//   rebuild trg fixtures (e.g. restore AUTOTEST A3's "gate every step" after a
//   corrupting event_mode flip). The 8-bit gate byte can't fit one 7-bit MIDI
//   byte, so the value arrives split: value = val_lo7 | (val_hi1 << 7) (0xff =
//   0x7f,0x01). A subsequent pattern_save persists the source, so no render here.
// Reply payload: [track, trg_layer, trg_instr, step8, value&0x7f, status]
//   status 0x01 = ok, 0x02 = bad payload, 0x03 = invalid args.
static void cmd_trg_byte_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[6] = { 0, 0, 0, 0, 0, 0x02 };
  if( plen < 6 ) {
    send_reply(port, CMD_TRG_BYTE_SET, reply, sizeof(reply));
    return;
  }
  u8 track     = payload[0] & 0x0f;
  u8 trg_layer = payload[1] & 0x7f;
  u8 trg_instr = payload[2] & 0x7f;
  u8 step8     = payload[3] & 0x7f;
  u8 value     = (payload[4] & 0x7f) | ((payload[5] & 0x01) << 7);
  reply[0] = track;
  reply[1] = trg_layer;
  reply[2] = trg_instr;
  reply[3] = step8;
  reply[4] = value & 0x7f;

  u8 num_t_layers = (u8)SEQ_TRG_NumLayersGet(track);
  u8 num_t_instr  = (u8)SEQ_TRG_NumInstrumentsGet(track);
  u8 num_t_step8  = (u8)(SEQ_TRG_NumStepsGet(track) / 8);
  if( track >= SEQ_CORE_NUM_TRACKS ||
      trg_layer >= num_t_layers || trg_instr >= num_t_instr ||
      step8 >= num_t_step8 ) {
    reply[5] = 0x03;
    send_reply(port, CMD_TRG_BYTE_SET, reply, sizeof(reply));
    return;
  }

  SEQ_TRG_Set8(track, step8, trg_layer, trg_instr, value);
  seq_ui_display_update_req = 1;
  reply[5] = 0x01;
  send_reply(port, CMD_TRG_BYTE_SET, reply, sizeof(reply));
}


// CMD_FREEZE_SET payload: [value]  (0 = live, nonzero = frozen).
//   Sets the global generator-mutation master switch (seq_core_state.FREEZE) —
//   the design's FREEZE. While frozen, SEQ_GENERATOR_Tick skips the per-measure
//   auto-mutate so engaged loops hold. Lets HIL pin "frozen loops don't drift".
// Reply payload: [state, status]   state echoes the new FREEZE bit; status 0x01.
static void cmd_freeze_set(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[2] = { 0, 0x02 };
  if( plen < 1 ) {
    send_reply(port, CMD_FREEZE_SET, reply, sizeof(reply));
    return;
  }
  portENTER_CRITICAL();
  seq_core_state.FREEZE = payload[0] & 0x01;
  portEXIT_CRITICAL();
  reply[0] = seq_core_state.FREEZE;
  reply[1] = 0x01;
  send_reply(port, CMD_FREEZE_SET, reply, sizeof(reply));
}


// CMD_MSP_QUERY: no payload. Reply payload is pack7-encoded:
//   raw[0..3]   = high_water_bytes   (peak MSP usage since paint, LE u32)
//   raw[4..7]   = paint_extent_bytes (painted region size, LE u32)
//   raw[8..11]  = paint_initial_depth (_estack - paint_hi, LE u32)
//   raw[12..15] = paint_lo (absolute address, LE u32)
//   raw[16..19] = paint_hi (absolute address, LE u32)
// MSP peak usage from _estack = paint_initial_depth + high_water_bytes.
static void cmd_msp_query(mios32_midi_port_t port)
{
  u32 high_water = SEQ_CORE_MSPHighWaterBytes();
  u32 extent     = SEQ_CORE_MSPPaintExtent();
  u32 initial    = SEQ_CORE_MSPPaintInitialDepth();
  u32 lo         = SEQ_CORE_MSPPaintLo();
  u32 hi         = SEQ_CORE_MSPPaintHi();

  u8 raw[20];
  u32 vals[5] = { high_water, extent, initial, lo, hi };
  for(u32 i=0; i<5; ++i) {
    raw[4*i + 0] = (u8)(vals[i] & 0xff);
    raw[4*i + 1] = (u8)((vals[i] >> 8)  & 0xff);
    raw[4*i + 2] = (u8)((vals[i] >> 16) & 0xff);
    raw[4*i + 3] = (u8)((vals[i] >> 24) & 0xff);
  }

  u8 reply[32];
  u32 w = pack7(raw, sizeof(raw), reply);
  send_reply(port, CMD_MSP_QUERY, reply, w);
}


// CMD_LCD_SNAPSHOT: no payload.
// Reply payload: [lines, columns, packed_bytes...]
// Bit 7 of each lcd_buffer byte is MBSEQ's internal "already-rendered to the
// physical LCD" dirty flag (set by SEQ_LCD_Update after each PrintChar). We
// mask it off so the harness sees only what's printed, then pack7-encode the
// result so the payload stays 7-bit on the wire. Harness unpacks symmetrically.
static void cmd_lcd_snapshot(mios32_midi_port_t port)
{
  const u8 *src = SEQ_LCD_BufferGet();
  u16 lines = SEQ_LCD_BufferLinesGet();
  u16 cols = SEQ_LCD_BufferColumnsGet();
  u32 n = (u32)lines * (u32)cols;

  // Strip the dirty flag into a local copy before packing. LCD_MAX is 2*80=160.
  u8 stripped[2 * 80];
  if( n > sizeof(stripped) ) n = sizeof(stripped);
  for(u32 i=0; i<n; ++i)
    stripped[i] = src[i] & 0x7f;

  u8 reply[LCD_SNAPSHOT_MAX_REPLY - sizeof(testctrl_header) - 2];
  u32 w = 0;
  reply[w++] = lines & 0x7f;
  reply[w++] = cols & 0x7f;
  w += pack7(stripped, n, &reply[w]);

  send_reply(port, CMD_LCD_SNAPSHOT, reply, w);
}


/////////////////////////////////////////////////////////////////////////////
// Public API
/////////////////////////////////////////////////////////////////////////////

s32 SEQ_TESTCTRL_Init(u32 mode)
{
  parser_state = STATE_IDLE;
  header_ctr = 0;
  cmd = 0;
  payload_len = 0;
  last_port = DEFAULT;
  return 0;
}


// Called from APP_SYSEX_Parser for every SysEx byte (incl. F0 / F7).
s32 SEQ_TESTCTRL_Parser(mios32_midi_port_t port, u8 midi_in)
{
  // Real-time bytes can be interleaved into SysEx; ignore them here.
  if( midi_in >= 0xf8 )
    return 0;

  // If we're already mid-message and bytes arrive on a different port, abandon
  // the in-flight message — we can't reassemble across ports.
  if( parser_state != STATE_IDLE && port != last_port ) {
    parser_state = STATE_IDLE;
    header_ctr = 0;
  }
  last_port = port;

  // Any non-status byte in the middle of a SysEx is fine; a status byte other
  // than F7 means the stream was aborted (running status, new SysEx, etc.).
  if( midi_in >= 0x80 && midi_in != 0xf0 && midi_in != 0xf7 ) {
    parser_state = STATE_IDLE;
    header_ctr = 0;
    return 0;
  }

  switch( parser_state ) {
    case STATE_IDLE:
      if( midi_in == testctrl_header[0] ) {
        parser_state = STATE_HEADER;
        header_ctr = 1;
      }
      break;

    case STATE_HEADER:
      if( midi_in != testctrl_header[header_ctr] ) {
        // Header mismatch — not for us. Reset and let other parsers handle it.
        parser_state = STATE_IDLE;
        header_ctr = 0;
      } else {
        ++header_ctr;
        if( header_ctr >= sizeof(testctrl_header) )
          parser_state = STATE_CMD;
      }
      break;

    case STATE_CMD:
      cmd = midi_in;
      payload_len = 0;
      parser_state = STATE_PAYLOAD;
      break;

    case STATE_PAYLOAD:
      if( midi_in == 0xf7 ) {
        switch( cmd ) {
          case CMD_PING:
            cmd_ping(port);
            break;
          case CMD_BUTTON:
            cmd_button(port, payload_buf, payload_len);
            break;
          case CMD_ENCODER:
            cmd_encoder(port, payload_buf, payload_len);
            break;
          case CMD_LCD_SNAPSHOT:
            cmd_lcd_snapshot(port);
            break;
          case CMD_RESET_STATE:
            cmd_reset_state(port, payload_buf, payload_len);
            break;
          case CMD_PAGE_SET:
            cmd_page_set(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_CONFIG:
            cmd_track_config(port, payload_buf, payload_len);
            break;
          case CMD_TICK_QUERY:
            cmd_tick_query(port);
            break;
          case CMD_CC_GET:
            cmd_cc_get(port, payload_buf, payload_len);
            break;
          case CMD_CC_SET:
            cmd_cc_set(port, payload_buf, payload_len);
            break;
          case CMD_PLAY_SECTION_GET:
            cmd_play_section_get(port, payload_buf, payload_len);
            break;
          case CMD_PLAY_SECTION_SET:
            cmd_play_section_set(port, payload_buf, payload_len);
            break;
          case CMD_BOUNCE:
            cmd_bounce(port, payload_buf, payload_len);
            break;
          case CMD_PATTERN_LOAD:
            cmd_pattern_load(port, payload_buf, payload_len);
            break;
          case CMD_PATTERN_SAVE:
            cmd_pattern_save(port, payload_buf, payload_len);
            break;
          case CMD_BANK_CREATE:
            cmd_bank_create(port, payload_buf, payload_len);
            break;
          case CMD_SESSION_LOAD:
            cmd_session_load(port, payload_buf, payload_len);
            break;
          case CMD_SESSION_NAME_GET:
            cmd_session_name_get(port);
            break;
          case CMD_TRG_BYTE_GET:
            cmd_trg_byte_get(port, payload_buf, payload_len);
            break;
          case CMD_MSP_QUERY:
            cmd_msp_query(port);
            break;
          case CMD_UI_INSTR_SET:
            cmd_ui_instr_set(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_DRUM_INIT:
            cmd_track_drum_init(port, payload_buf, payload_len);
            break;
          case CMD_GENERATOR_QUERY:
            cmd_generator_query(port, payload_buf, payload_len);
            break;
          case CMD_UI_TRACK_SET:
            cmd_ui_track_set(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_DRUM_PAR_SET:
            cmd_track_drum_par_set(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_DRUM_PAR_GET:
            cmd_track_drum_par_get(port, payload_buf, payload_len);
            break;
          case CMD_GENERATOR_TICK_FORCE:
            cmd_generator_tick_force(port, payload_buf, payload_len);
            break;
          case CMD_GENERATOR_DIAL_SET:
            cmd_generator_dial_set(port, payload_buf, payload_len);
            break;
          case CMD_GENERATOR_MULT_SET:
            cmd_generator_mult_set(port, payload_buf, payload_len);
            break;
          case CMD_CAPTURE_TO_TRACK:
            cmd_capture_to_track(port, payload_buf, payload_len);
            break;
          case CMD_CAPTURE_TO_SLOT_TRACK:
            cmd_capture_to_slot_track(port, payload_buf, payload_len);
            break;
          case CMD_UI_TRACK_GET:
            cmd_ui_track_get(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_PAR_GET:
            cmd_track_par_get(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_PAR_SET:
            cmd_track_par_set(port, payload_buf, payload_len);
            break;
          case CMD_GLOBAL_SCALE_SET:
            cmd_global_scale_set(port, payload_buf, payload_len);
            break;
          case CMD_TENSION_SET:
            cmd_tension_set(port, payload_buf, payload_len);
            break;
          case CMD_TENSION_BAND_GET:
            cmd_tension_band_get(port, payload_buf, payload_len);
            break;
          case CMD_TENSION_GET:
            cmd_tension_get(port, payload_buf, payload_len);
            break;
          case CMD_TENSION_RESOLVE:
            cmd_tension_resolve(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_LOAD:
            cmd_track_load(port, payload_buf, payload_len);
            break;
          case CMD_TRACK_UNDO:
            cmd_track_undo(port);
            break;
          case CMD_TRACK_UNDO_QUERY:
            cmd_track_undo_query(port);
            break;
          case CMD_SESSION_CREATE:
            cmd_session_create(port, payload_buf, payload_len);
            break;
          case CMD_DIRTY_QUERY:
            cmd_dirty_query(port);
            break;
          case CMD_DIRTY_SET:
            cmd_dirty_set(port, payload_buf, payload_len);
            break;
          case CMD_PATTERN_CHANGE:
            cmd_pattern_change(port, payload_buf, payload_len);
            break;
          case CMD_GENERATOR_LOCK_SET:
            cmd_generator_lock_set(port, payload_buf, payload_len);
            break;
          case CMD_CHECKPOINT:
            cmd_checkpoint(port);
            break;
          case CMD_REVERT:
            cmd_revert(port);
            break;
          case CMD_ANCHOR_PRESENT:
            cmd_anchor_present(port);
            break;
          case CMD_PHRASE_CAPTURE:
            cmd_phrase_capture(port, payload_buf, payload_len);
            break;
          case CMD_PHRASE_RECALL:
            cmd_phrase_recall(port, payload_buf, payload_len);
            break;
          case CMD_PHRASE_PRESENT:
            cmd_phrase_present(port, payload_buf, payload_len);
            break;
          case CMD_TRG_BYTE_SET:
            cmd_trg_byte_set(port, payload_buf, payload_len);
            break;
          case CMD_FREEZE_SET:
            cmd_freeze_set(port, payload_buf, payload_len);
            break;
          default:
            // Unknown command — silently ignore. Harness will time out and surface
            // the failure with a clear message, which is more useful than an
            // ambiguous error reply on the wire.
            break;
        }
        parser_state = STATE_IDLE;
        header_ctr = 0;
      } else if( payload_len < PAYLOAD_BUF_MAX ) {
        payload_buf[payload_len++] = midi_in;
      }
      // Over-long payloads silently drop trailing bytes; the F7 still dispatches
      // and individual handlers validate their own payload length.
      break;

    case STATE_DONE:
      parser_state = STATE_IDLE;
      header_ctr = 0;
      break;
  }

  return 0;
}


s32 SEQ_TESTCTRL_TimeOut(mios32_midi_port_t port)
{
  if( port == last_port ) {
    parser_state = STATE_IDLE;
    header_ctr = 0;
  }
  return 0;
}
