"""SysEx protocol constants for the SEQ V4 test-control interface.

Wire format:
    request : F0 00 00 7E 4F 54 <cmd> [args...] F7
    reply   : F0 00 00 7E 4F 54 <cmd> [data...] F7

Keep this file in sync with seq_testctrl.c.
"""

TESTCTRL_HEADER = bytes([0xF0, 0x00, 0x00, 0x7E, 0x4F, 0x54])

CMD_PING = 0x01
CMD_BUTTON = 0x10
CMD_ENCODER = 0x11
CMD_LCD_SNAPSHOT = 0x20
CMD_RESET_STATE = 0x30
CMD_PAGE_SET = 0x31
CMD_TRACK_CONFIG = 0x32
CMD_TICK_QUERY = 0x40
CMD_CC_GET = 0x50
CMD_CC_SET = 0x51
CMD_PLAY_SECTION_GET = 0x52
CMD_PLAY_SECTION_SET = 0x53
CMD_BOUNCE = 0x54
CMD_PATTERN_LOAD = 0x55
CMD_SESSION_LOAD = 0x56
CMD_SESSION_NAME_GET = 0x57
CMD_TRG_BYTE_GET = 0x58
CMD_MSP_QUERY = 0x59
CMD_UI_INSTR_SET = 0x5a
CMD_TRACK_DRUM_INIT = 0x5b
CMD_GENERATOR_QUERY = 0x5c
CMD_UI_TRACK_SET = 0x5d
CMD_TRACK_DRUM_PAR_SET = 0x5e
CMD_TRACK_DRUM_PAR_GET = 0x5f


# Mirror of seq_ui_page_t (from seq_ui_pages.h). Add as needed.
class Page:
    MENU = 1
    EDIT = 16
    MUTE = 17
    PATTERN = 19
    SONG = 20
    MIXER = 21
    TRKEVNT = 22
    TRKEUCLID = 33
    TRKJAM = 34
    FX_ECHO = 36
    FX_HUMANIZE = 37
    FX_ROBOTIZE = 38
    ROBOLOOP = 39
    BPM = 47
    PITCHGEN = 59


# mios32_midi_port_t values that fit in 7 bits.
class MidiPort:
    DEFAULT = 0x00
    USB0 = 0x10  # shows as "USB1" in the SEQ UI (off-by-one display)
    USB1 = 0x11
    USB2 = 0x12
    USB3 = 0x13
    UART0 = 0x20
    UART1 = 0x21
    UART2 = 0x22
    UART3 = 0x23


# Encoder indices match MBSEQ's internal numbering.
class Encoder:
    DATAWHEEL = 0
    BPM = 17

    @staticmethod
    def GP(n: int) -> int:
        """GP1..GP16 -> encoder index. n is 1-based."""
        if not 1 <= n <= 16:
            raise ValueError(f"GP index out of range: {n}")
        return n


# RESET_STATE flags.
RESET_STOP_TRANSPORT = 0x01
RESET_PAGE_TO_EDIT = 0x02
RESET_TRACK_SELECTION = 0x04
RESET_UNMUTE_ALL = 0x08
RESET_CLEAR_ROBOTIZE = 0x10
RESET_MUTE_NON_T0 = 0x20  # mute tracks 1-15; applied after UNMUTE_ALL
RESET_DEFAULT = (
    RESET_STOP_TRANSPORT
    | RESET_PAGE_TO_EDIT
    | RESET_TRACK_SELECTION
    | RESET_UNMUTE_ALL
    | RESET_CLEAR_ROBOTIZE
    | RESET_MUTE_NON_T0
)


# Logical button IDs (mirror of button_id_t in seq_testctrl.c).
class Button:
    MENU = 0x01
    SELECT = 0x02
    EXIT = 0x03
    PLAY = 0x04
    STOP = 0x05
    PAUSE = 0x06
    RECORD = 0x07
    REW = 0x08
    FWD = 0x09
    LEFT = 0x0A
    RIGHT = 0x0B
    UP = 0x0C
    DOWN = 0x0D
    EDIT = 0x0E
    MUTE = 0x0F
    PATTERN = 0x10
    SONG = 0x11
    BOOKMARK = 0x12
    CLEAR = 0x13
    UNDO = 0x14
    COPY = 0x15
    PASTE = 0x16

    @staticmethod
    def GP(n: int) -> int:
        """GP1..GP16 -> button id. n is 1-based."""
        if not 1 <= n <= 16:
            raise ValueError(f"GP index out of range: {n}")
        return 0x40 + (n - 1)


# Status byte returned by CMD_BUTTON.
BUTTON_STATUS_DISPATCHED = 0x01
BUTTON_STATUS_UNCONFIGURED = 0x00
BUTTON_STATUS_BAD_PAYLOAD = 0x02

# Status byte returned by CMD_ENCODER.
ENCODER_STATUS_DISPATCHED = 0x01
ENCODER_STATUS_BAD_PAYLOAD = 0x02
ENCODER_STATUS_OUT_OF_RANGE = 0x03


# Status byte returned by all the synchronous SD-touching commands.
CMD_STATUS_OK = 0x01
CMD_STATUS_BAD_PAYLOAD = 0x02
CMD_STATUS_INVALID_TRACK = 0x03
CMD_STATUS_UNMAPPED_CC = 0x04

# CMD_SESSION_LOAD dispatch_status values (in addition to OK/BAD_PAYLOAD).
SESSION_STATUS_NAME_TOO_LONG = 0x03

# Session name length cap (mirror of seq_file_session_name[13] — 12 chars + null).
SESSION_NAME_MAX_LEN = 12


# CC indices (mirror of seq_cc.h). Only the ones the tests actually touch are
# listed — add more as needed. Values >= 0x80 use the 14-bit wire encoding.
class CC:
    LFO_AMPLITUDE = 0x31
    MODE = 0x40
    MODE_FLAGS = 0x41
    BUSASG = 0x45
    MIDI_CHANNEL = 0x46
    MIDI_PORT = 0x47
    DIRECTION = 0x48
    STEPS_REPLAY = 0x49
    STEPS_FORWARD = 0x4A
    STEPS_JMPBCK = 0x4B
    LENGTH = 0x4D
    TRANSPOSE_SEMI = 0x50
    TRANSPOSE_OCT = 0x51
    GROOVE_VALUE = 0x52
    GROOVE_STYLE = 0x53
    MORPH_MODE = 0x54
    HUMANIZE_VALUE = 0x56
    STEPS_REPEAT = 0x5C
    STEPS_SKIP = 0x5D
    ASG_GATE = 0x60
    ECHO_REPEATS = 0x70
    FX_MIDI_NUM_CHANNELS = 0x7B
    ROBOTIZE_MASK1 = 0x80
    ROBOTIZE_MASK2 = 0x81
    ROBOTIZE_ACTIVE = 0x82
    ROBOTIZE_LOOP_CYCLES = 0x91
    CHORDMASK_STRENGTH = 0x96  # 0..127 ChordMask probabilistic snap strength
    CHORDMASK_BUS = 0x97       # 0..3   per-processor bus the ChordMask reads PC-set from
    CHORDMASK_DRUM_L = 0x98    # bit i = process drum i  (drums 0..7)
    CHORDMASK_DRUM_H = 0x99    # bit i = process drum 8+i (drums 8..15)


def frame(cmd: int, payload: bytes = b"") -> bytes:
    """Build a complete SysEx message for a testctrl command."""
    return TESTCTRL_HEADER + bytes([cmd]) + payload + bytes([0xF7])


def parse_reply(msg: bytes) -> tuple[int, bytes] | None:
    """Parse an incoming SysEx message. Returns (cmd, payload) or None if not ours."""
    if len(msg) < len(TESTCTRL_HEADER) + 2:
        return None
    if not msg.startswith(TESTCTRL_HEADER):
        return None
    if msg[-1] != 0xF7:
        return None
    cmd = msg[len(TESTCTRL_HEADER)]
    payload = bytes(msg[len(TESTCTRL_HEADER) + 1 : -1])
    return cmd, payload


def unpack7(packed: bytes) -> bytes:
    """Inverse of the firmware's pack7(): unpacks 7-bit-safe groups back to raw bytes.

    Encoding: every group is `(msbs, b0..b6)` where bit j of `msbs` is the
    high bit of byte j. Trailing partial groups are supported.
    """
    out = bytearray()
    i = 0
    while i < len(packed):
        msbs = packed[i]
        i += 1
        # A group has up to 7 data bytes. The last group may be shorter; in
        # that case the encoder still wrote (group_len) data bytes after the
        # msb byte, so we just consume whatever's left.
        group = min(7, len(packed) - i)
        for j in range(group):
            b = packed[i + j] & 0x7F
            if msbs & (1 << j):
                b |= 0x80
            out.append(b)
        i += group
    return bytes(out)
