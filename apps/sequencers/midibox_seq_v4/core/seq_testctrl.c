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
#include <seq_bpm.h>
#include "tasks.h"
#include "seq_testctrl.h"
#include "seq_ui.h"
#include "seq_ui_pages.h"
#include "seq_hwcfg.h"
#include "seq_lcd.h"
#include "seq_cc.h"
#include "seq_core.h"
#include "seq_midi_port.h"
#include "seq_capture.h"
#include "seq_pattern.h"


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
// Reply payload: [flags] echoed back, then status 0x01.
//
// Deliberately does NOT clear pattern data — tests that need an empty pattern
// should press CLEAR after reset.
static void cmd_reset_state(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
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
  }

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


// CMD_BOUNCE payload: [src_track, dst_group, dst_bank, dst_pattern, num_measures]
// Reply payload: [src_track, dst_bank, dst_pattern, commit_ok, dispatch_status]
//   commit_ok 0x01 = SEQ_CAPTURE_CommitToSlot returned >=0, 0x00 = returned <0.
//   dispatch_status 0x01 = ok, 0x02 = bad payload.
//
// Synchronous: the parser blocks until SEQ_CAPTURE_CommitToSlot returns. SD I/O
// can take 100ms+, so the harness must wait_for_sysex with a generous timeout
// (~3s). PatternWrite + the in-RAM snapshot/restore both run inside the call.
static void cmd_bounce(mios32_midi_port_t port, const u8 *payload, u8 plen)
{
  u8 reply[5] = { 0, 0, 0, 0, 0x02 };
  if( plen < 5 ) {
    send_reply(port, CMD_BOUNCE, reply, sizeof(reply));
    return;
  }
  u8 src_track    = payload[0];
  u8 dst_group    = payload[1];
  u8 dst_bank     = payload[2] & 0x07;
  u8 dst_pattern  = payload[3] & 0x7f;
  u8 num_measures = payload[4] & 0x7f;

  seq_pattern_t dst_pat;
  dst_pat.ALL = 0;
  dst_pat.bank = dst_bank;
  dst_pat.pattern = dst_pattern;

  s32 r = SEQ_CAPTURE_CommitToSlot(src_track, dst_group, dst_pat, num_measures);

  reply[0] = src_track;
  reply[1] = dst_bank;
  reply[2] = dst_pattern;
  reply[3] = (r >= 0) ? 0x01 : 0x00;
  reply[4] = 0x01;
  send_reply(port, CMD_BOUNCE, reply, sizeof(reply));
}


// CMD_PATTERN_LOAD payload: [group, bank, pattern]
// Reply payload: [group, bank, pattern, load_ok, dispatch_status]
//   load_ok 0x01 = SEQ_PATTERN_Load returned >=0, 0x00 = returned <0.
//
// Synchronous: SD read can take 100ms+. Same timeout caveat as CMD_BOUNCE.
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
