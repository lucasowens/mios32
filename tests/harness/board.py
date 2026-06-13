"""Board connection abstraction for the SEQ V4.

Opens USB MIDI in/out, reassembles SysEx messages, and exposes high-level
helpers (currently just `ping()`). More commands will be added as the
firmware-side `seq_testctrl.c` grows.
"""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass

import rtmidi

from .sysex import (
    BUTTON_STATUS_DISPATCHED,
    CMD_BOUNCE,
    CMD_BUTTON,
    CMD_CC_GET,
    CMD_CC_SET,
    CMD_ENCODER,
    CMD_LCD_SNAPSHOT,
    CMD_PAGE_SET,
    CMD_PATTERN_LOAD,
    CMD_PING,
    CMD_PLAY_SECTION_GET,
    CMD_PLAY_SECTION_SET,
    CMD_RESET_STATE,
    CMD_SESSION_LOAD,
    CMD_SESSION_NAME_GET,
    CMD_MSP_QUERY,
    CMD_TRG_BYTE_GET,
    CMD_UI_INSTR_SET,
    CMD_TRACK_DRUM_INIT,
    CMD_GENERATOR_QUERY,
    CMD_GENERATOR_TICK_FORCE,
    CMD_GENERATOR_DIAL_SET,
    CMD_GENERATOR_MULT_SET,
    CMD_CAPTURE_TO_TRACK,
    CMD_CAPTURE_TO_SLOT_TRACK,
    DIAL_RANGE_MIN,
    DIAL_RANGE_MAX,
    DIAL_RATE,
    DIAL_DEPTH,
    DIAL_CONTOUR,
    CMD_UI_TRACK_SET,
    CMD_UI_TRACK_GET,
    CMD_TRACK_DRUM_PAR_SET,
    CMD_TRACK_DRUM_PAR_GET,
    CMD_TRACK_PAR_SET,
    CMD_TRACK_PAR_GET,
    CMD_GLOBAL_SCALE_SET,
    CMD_TENSION_SET,
    CMD_TENSION_BAND_GET,
    CMD_TENSION_GET,
    CMD_TENSION_RESOLVE,
    CMD_PATTERN_SAVE,
    CMD_STATUS_OK,
    CMD_TICK_QUERY,
    CMD_TRACK_CONFIG,
    CMD_SESSION_CREATE,
    CMD_TRACK_LOAD,
    CMD_TRACK_UNDO,
    CMD_TRACK_UNDO_QUERY,
    CMD_DIRTY_QUERY,
    CMD_DIRTY_SET,
    CMD_PATTERN_CHANGE,
    CMD_GENERATOR_LOCK_SET,
    ENCODER_STATUS_DISPATCHED,
    ENCODER_STATUS_OUT_OF_RANGE,
    MidiPort,
    RESET_DEFAULT,
    SESSION_NAME_MAX_LEN,
    SESSION_STATUS_NAME_TOO_LONG,
    frame,
    parse_reply,
    unpack7,
)


class BoardNotFound(Exception):
    pass


class ButtonNotConfigured(Exception):
    """Raised when CMD_BUTTON reports the button isn't mapped in the current hwcfg."""


@dataclass
class NoteEvent:
    """A captured Note On/Off event from the SEQ V4."""

    timestamp: float    # seconds since capture start
    port_idx: int       # which USB port the byte arrived on (0..3)
    is_on: bool         # True for Note On with vel > 0
    channel: int        # 0..15
    note: int           # 0..127
    velocity: int       # 0..127


@dataclass
class LCDSnapshot:
    """A point-in-time read of the SEQ V4's 2x80 LCD buffer.

    `text` returns the buffer as two 80-char lines joined by '\\n', with any
    non-printable custom-glyph bytes (0x80+) substituted for '?' so it's safe
    to print. Use `line(0)` / `line(1)` for raw bytes including custom glyphs.
    """

    lines: int
    columns: int
    raw: bytes  # lines*columns

    def line_bytes(self, idx: int) -> bytes:
        if not 0 <= idx < self.lines:
            raise IndexError(idx)
        return self.raw[idx * self.columns : (idx + 1) * self.columns]

    def line(self, idx: int) -> str:
        return "".join(chr(b) if 0x20 <= b < 0x7F else "?" for b in self.line_bytes(idx))

    @property
    def text(self) -> str:
        return "\n".join(self.line(i) for i in range(self.lines))

    def contains(self, needle: str) -> bool:
        return needle in self.text

    def __str__(self) -> str:
        return self.text


@dataclass
class GeneratorState:
    """Live snapshot of a SEQ_GENERATOR_* pool slot.

    Returned by Board.generator_query(). `loop` is the 64-byte Turing array
    (pitch per step, the source the gen writes into the Note par-layer).
    `locks` is a 64-tuple of bools (True = step locked, survives mutation).
    `mult` is a 64-tuple of ints (each 0..15, 4-bit MULT code per step;
    0=mute, 1=0.5×, 2=1×/default, 3=2×).
    `anchor_valid` is True once anchor[] holds a captured snapshot (set at
    auto-anchor on ENGAGE seed and on the ANCHOR gesture). The anchor bytes
    are only transferred when generator_query(..., with_anchor=True) — the
    Stage B persistence pins need the full slot byte-identical; everything
    else saves the wire bytes and gets `anchor=None`.
    """

    track: int
    instrument: int
    range_min: int
    range_max: int
    mutation_rate: int
    mutation_depth: int
    contour_shape: int
    engaged: bool
    anchor_valid: bool
    loop: bytes              # 64 bytes
    locks: tuple[bool, ...]  # 64 entries
    mult: tuple[int, ...]    # 64 entries, each 0..15
    anchor: bytes | None = None  # 64 bytes when requested, else None


@dataclass
class CapturedMessage:
    timestamp: float           # seconds since Board open
    data: bytes                # full MIDI bytes (incl. F0/F7 for SysEx)
    port_idx: int = 0          # which input port (0..3) the bytes arrived on


# Substring to search for in MIDI port names. The SEQ V4 USB device enumerates
# as "MIDIbox SEQ V4+ Port N" on macOS.
_PORT_NAME_HINT = "midibox seq v4"


def _find_all_ports(port_iface) -> list[tuple[int, str]]:
    """Return [(port_index, port_name), ...] for every matching port."""
    matches = []
    for i, name in enumerate(port_iface.get_ports()):
        if _PORT_NAME_HINT in name.lower():
            matches.append((i, name))
    return matches


class Board:
    """High-level connection to a SEQ V4 board over USB MIDI.

    Opens all available USB MIDI ports for input so capture sees notes regardless
    of which port the user's track is configured to send to. Output / SysEx
    commands are sent on the first port; the firmware's testctrl parser doesn't
    care which port a command arrives on.
    """

    def __init__(self):
        in_iface = rtmidi.MidiIn()
        out_iface = rtmidi.MidiOut()

        in_ports = _find_all_ports(in_iface)
        out_ports = _find_all_ports(out_iface)
        in_iface.delete()
        out_iface.delete()

        if not in_ports or not out_ports:
            raise BoardNotFound(
                f"no MIDI port matched {_PORT_NAME_HINT!r}; "
                f"saw inputs={rtmidi.MidiIn().get_ports()!r}"
            )

        # One MidiIn per port so callbacks identify which port a byte came from.
        self._inputs: list[tuple[int, str, rtmidi.MidiIn]] = []
        for port_idx, (rt_idx, name) in enumerate(in_ports):
            mi = rtmidi.MidiIn()
            mi.ignore_types(sysex=False, timing=True, active_sense=True)
            mi.open_port(rt_idx)
            mi.set_callback(self._make_callback(port_idx))
            self._inputs.append((port_idx, name, mi))

        # One MidiOut on the first matching port — that's where we send testctrl
        # SysEx commands. The firmware parses SysEx from any port equally.
        self._midi_out = rtmidi.MidiOut()
        self._midi_out.open_port(out_ports[0][0])
        self._out_name = out_ports[0][1]

        self._t0 = time.monotonic()
        self._lock = threading.Lock()
        self._messages: deque[CapturedMessage] = deque(maxlen=8192)

    @property
    def input_port_names(self) -> list[str]:
        return [name for _, name, _ in self._inputs]

    @property
    def output_port_name(self) -> str:
        return self._out_name

    def close(self) -> None:
        for _, _, mi in self._inputs:
            try:
                mi.close_port()
            except Exception:
                pass
        try:
            self._midi_out.close_port()
        except Exception:
            pass

    def __enter__(self) -> "Board":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _make_callback(self, port_idx: int):
        def _cb(event, _data=None):
            msg, _delta = event
            with self._lock:
                self._messages.append(
                    CapturedMessage(
                        timestamp=time.monotonic() - self._t0,
                        data=bytes(msg),
                        port_idx=port_idx,
                    )
                )
        return _cb

    def drain(self) -> None:
        """Discard all captured messages. Tests rarely need this — `wait_for_sysex`
        uses an internal watermark instead. Calling drain mid-capture will wipe
        Note On events you wanted to assert on.
        """
        with self._lock:
            self._messages.clear()

    def send_raw(self, data: bytes) -> None:
        self._midi_out.send_message(list(data))

    def wait_for_sysex(self, cmd: int, timeout: float = 2.0, since: float | None = None) -> bytes:
        """Block until a testctrl SysEx reply with the given cmd byte arrives.

        `since` filters out replies older than the given capture timestamp. When
        omitted, defaults to "now" — i.e. only replies that arrive *after* this
        call returns are considered. This avoids matching against a leftover reply
        from a previous command without wiping the message buffer (which would
        also wipe captured Note events).
        """
        if since is None:
            since = time.monotonic() - self._t0
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                snapshot = list(self._messages)
            for msg in snapshot:
                if msg.timestamp < since:
                    continue
                parsed = parse_reply(msg.data)
                if parsed is None:
                    continue
                got_cmd, payload = parsed
                if got_cmd == cmd:
                    return payload
            time.sleep(0.005)
        raise TimeoutError(f"no testctrl reply for cmd=0x{cmd:02x} within {timeout}s")

    def ping(self, timeout: float = 2.0) -> bytes:
        """Send PING, return the firmware's build-id payload (e.g. b'SEQv4')."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PING))
        return self.wait_for_sysex(CMD_PING, timeout=timeout, since=since)

    def button(self, button_id: int, depressed: bool, timeout: float = 1.0) -> int:
        """Send a button event. depressed=False means "pressed", True means "released",
        matching MBSEQ's internal convention (`s32 depressed` arg).

        Returns the resolved hwcfg pin (useful for diagnostics). Raises
        ButtonNotConfigured if the firmware reports the button isn't mapped.
        """
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_BUTTON, bytes([button_id & 0x7F, 1 if depressed else 0])))
        payload = self.wait_for_sysex(CMD_BUTTON, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short BUTTON reply: {payload!r}")
        _id, _depressed, pin_lo, pin_hi, status = payload[:5]
        pin = pin_lo | (pin_hi << 7)
        if status != BUTTON_STATUS_DISPATCHED:
            raise ButtonNotConfigured(
                f"button id 0x{button_id:02x} not configured in current hwcfg "
                f"(firmware status={status:#04x}, pin={pin})"
            )
        return pin

    def press(self, button_id: int, hold_seconds: float = 0.02) -> None:
        """Convenience: send press, sleep briefly, send release."""
        self.button(button_id, depressed=False)
        if hold_seconds > 0:
            time.sleep(hold_seconds)
        self.button(button_id, depressed=True)

    def encoder(self, encoder_idx: int, delta: int, timeout: float = 1.0) -> None:
        """Send an encoder turn. Positive delta = clockwise.

        Wire encoding clamps the delta to [-64, 63]; the firmware further clamps
        to [-3, +3] per call inside SEQ_UI_Encoder_Handler. For larger turns,
        call repeatedly with delta=+/-3.
        """
        if not -64 <= delta <= 63:
            raise ValueError(f"encoder delta out of wire range: {delta}")
        inc_offset = (delta + 0x40) & 0x7F
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_ENCODER, bytes([encoder_idx & 0x7F, inc_offset])))
        payload = self.wait_for_sysex(CMD_ENCODER, timeout=timeout, since=since)
        if len(payload) < 3:
            raise RuntimeError(f"short ENCODER reply: {payload!r}")
        status = payload[2]
        if status == ENCODER_STATUS_OUT_OF_RANGE:
            raise ValueError(f"encoder index {encoder_idx} out of range on the firmware side")
        if status != ENCODER_STATUS_DISPATCHED:
            raise RuntimeError(f"ENCODER returned status {status:#04x}")

    def turn(self, encoder_idx: int, total: int) -> None:
        """Convenience: turn an encoder by `total` (any magnitude) using +/-3 chunks
        so each chunk fits inside MBSEQ's internal clamp.
        """
        if total == 0:
            return
        step = 3 if total > 0 else -3
        remaining = total
        while remaining != 0:
            chunk = step if abs(remaining) >= 3 else remaining
            self.encoder(encoder_idx, chunk)
            remaining -= chunk

    def reset(self, flags: int = RESET_DEFAULT, timeout: float = 1.0) -> None:
        """Return the device to a known state: stop transport + go to EDIT page (by default)."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_RESET_STATE, bytes([flags & 0x7F])))
        self.wait_for_sysex(CMD_RESET_STATE, timeout=timeout, since=since)

    def page_set(self, page_id: int, timeout: float = 1.0) -> None:
        """Jump directly to a UI page (bypasses the page-jump grid)."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PAGE_SET, bytes([page_id & 0x7F])))
        self.wait_for_sysex(CMD_PAGE_SET, timeout=timeout, since=since)

    def track_config(
        self,
        track: int,
        midi_port: int = MidiPort.USB0,
        channel: int = 0,
        timeout: float = 1.0,
    ) -> None:
        """Set a track's MIDI output port + channel directly.

        Useful in tests to force a track to emit on USB1 (the harness's port)
        regardless of how the user's pattern is configured. `midi_port` is the
        raw mios32_midi_port_t enum value, NOT the UI's 1-based display.
        Default USB0 (== "USB1" in the UI) routes to Port 1.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= channel <= 15:
            raise ValueError(f"channel out of range: {channel}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_TRACK_CONFIG, bytes([track, midi_port & 0x7F, channel & 0x0F]))
        )
        self.wait_for_sysex(CMD_TRACK_CONFIG, timeout=timeout, since=since)

    def capture_start(self) -> float:
        """Mark the start of a capture window. Returns the start timestamp.

        Does NOT clear the buffer — clearing would wipe in-flight Note events
        that arrived between the previous SysEx round-trip and this call. Use
        the returned timestamp with `capture_notes(since=...)` to filter.
        """
        return time.monotonic() - self._t0

    def capture_notes(self, since: float | None = None) -> list[NoteEvent]:
        """Return all Note On/Off events seen so far, in arrival order.

        `since` (capture timestamp from capture_start()) filters to events after
        that point. Note Off and Note On with velocity 0 both produce
        `NoteEvent(is_on=False)`.
        """
        events: list[NoteEvent] = []
        with self._lock:
            snapshot = list(self._messages)
        for msg in snapshot:
            if since is not None and msg.timestamp < since:
                continue
            if len(msg.data) != 3:
                continue
            status = msg.data[0]
            kind = status & 0xF0
            chan = status & 0x0F
            if kind == 0x90:  # Note On
                note, vel = msg.data[1], msg.data[2]
                events.append(
                    NoteEvent(
                        timestamp=msg.timestamp,
                        port_idx=msg.port_idx,
                        is_on=vel > 0,
                        channel=chan,
                        note=note,
                        velocity=vel,
                    )
                )
            elif kind == 0x80:  # Note Off
                note, vel = msg.data[1], msg.data[2]
                events.append(
                    NoteEvent(
                        timestamp=msg.timestamp,
                        port_idx=msg.port_idx,
                        is_on=False,
                        channel=chan,
                        note=note,
                        velocity=vel,
                    )
                )
        return events

    def tick_query(self, timeout: float = 1.0) -> dict:
        """Return engine + track-0 state for diagnostics."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TICK_QUERY))
        payload = self.wait_for_sysex(CMD_TICK_QUERY, timeout=timeout, since=since)
        if len(payload) < 22:
            raise RuntimeError(f"short TICK_QUERY reply ({len(payload)}b): {payload!r}")
        bpm_tick = (
            payload[16]
            | (payload[17] << 7)
            | (payload[18] << 14)
            | (payload[19] << 21)
            | (payload[20] << 28)
        )
        return {
            "running": bool(payload[0]),
            "muted_mask": payload[1] | (payload[2] << 7),
            "trk0_playmode": payload[3],
            "trk0_midi_port": payload[4],
            "trk0_midi_chn": payload[5],
            "trk0_event_mode": payload[6],
            "port_mute_usb0": bool(payload[7]),
            "port_mute_usb1": bool(payload[8]),
            "port_mute_usb2": bool(payload[9]),
            "port_mute_usb3": bool(payload[10]),
            "slaveclk_mute": payload[11],
            "soloed_mask": payload[12] | (payload[13] << 7),
            "trk0_lfo_cc_muted": payload[14],
            "trk0_layer_muted": payload[15],
            "bpm_tick": bpm_tick,
            "trk0_step": payload[21],
        }

    def cc_get(self, track: int, cc: int, timeout: float = 1.0) -> int:
        """Read a track CC value. cc supports 0..255 (covers both the main
        128-byte block and the 0x80..0x95 extension CCs).

        Raises ValueError if the firmware reports invalid track / unmapped CC.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= cc <= 255:
            raise ValueError(f"cc out of range: {cc}")
        cc_hi = (cc >> 7) & 0x01
        cc_lo = cc & 0x7F
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_CC_GET, bytes([track, cc_hi, cc_lo])))
        payload = self.wait_for_sysex(CMD_CC_GET, timeout=timeout, since=since)
        if len(payload) < 6:
            raise RuntimeError(f"short CC_GET reply: {payload!r}")
        status = payload[5]
        if status != CMD_STATUS_OK:
            raise ValueError(
                f"CC_GET track={track} cc=0x{cc:02x} returned status {status:#04x}"
            )
        return (payload[3] << 7) | payload[4]

    def cc_set(self, track: int, cc: int, value: int, timeout: float = 1.0) -> None:
        """Write a track CC value via SEQ_CC_Set. cc range 0..255, value 0..255."""
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= cc <= 255:
            raise ValueError(f"cc out of range: {cc}")
        if not 0 <= value <= 255:
            raise ValueError(f"value out of range: {value}")
        cc_hi = (cc >> 7) & 0x01
        cc_lo = cc & 0x7F
        v_hi = (value >> 7) & 0x01
        v_lo = value & 0x7F
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_CC_SET, bytes([track, cc_hi, cc_lo, v_hi, v_lo])))
        payload = self.wait_for_sysex(CMD_CC_SET, timeout=timeout, since=since)
        if len(payload) < 6:
            raise RuntimeError(f"short CC_SET reply: {payload!r}")
        status = payload[5]
        if status != CMD_STATUS_OK:
            raise ValueError(
                f"CC_SET track={track} cc=0x{cc:02x} value={value} returned status {status:#04x}"
            )

    def play_section_get(self, track: int, timeout: float = 1.0) -> int:
        """Read runtime play_section (subsection A-H, 0..7) for a track."""
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PLAY_SECTION_GET, bytes([track])))
        payload = self.wait_for_sysex(CMD_PLAY_SECTION_GET, timeout=timeout, since=since)
        if len(payload) < 3:
            raise RuntimeError(f"short PLAY_SECTION_GET reply: {payload!r}")
        if payload[2] != CMD_STATUS_OK:
            raise ValueError(f"PLAY_SECTION_GET status {payload[2]:#04x}")
        return payload[1]

    def play_section_set(self, track: int, value: int, timeout: float = 1.0) -> None:
        """Write runtime play_section for a track. value 0..7 selects sections A-H."""
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= value <= 127:
            raise ValueError(f"play_section out of range: {value}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PLAY_SECTION_SET, bytes([track, value])))
        payload = self.wait_for_sysex(CMD_PLAY_SECTION_SET, timeout=timeout, since=since)
        if len(payload) < 3:
            raise RuntimeError(f"short PLAY_SECTION_SET reply: {payload!r}")
        if payload[2] != CMD_STATUS_OK:
            raise ValueError(f"PLAY_SECTION_SET status {payload[2]:#04x}")

    def track_drum_init(self, track: int, timeout: float = 1.0) -> None:
        """Reinitialize a track for 16-instrument drum mode (64 par steps × 1
        Note layer × 16 drums, 64 trg steps × 8 trg-layers × 1 trg-instr).

        Use this in tests that need a deterministic drum-mode track without
        relying on a pre-saved AUTOTEST drum pattern. Sets event_mode = Drum,
        par_assignment_drum[0] = Note, then refreshes link cache. After this
        call, `SEQ_GENERATOR_Engage(track, instr)` will pass its drum-mode
        and Note-layer gating for instruments 0..15.

        Destructive: par + trg layers are cleared.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TRACK_DRUM_INIT, bytes([track])))
        payload = self.wait_for_sysex(CMD_TRACK_DRUM_INIT, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short TRACK_DRUM_INIT reply: {payload!r}")
        if payload[1] != CMD_STATUS_OK:
            raise ValueError(f"TRACK_DRUM_INIT status {payload[1]:#04x}")

    def ui_instrument_set(self, instr: int, timeout: float = 1.0) -> None:
        """Park the UI's drum-slot cursor (ui_selected_instrument).

        The PITCHGEN page reads this as the BOUNCE/ENGAGE destination. Drum-
        mode tracks have 16 instrument slots (0..15). Outside drum mode the
        value has limited meaning (the firmware masks to 4 bits).
        """
        if not 0 <= instr <= 15:
            raise ValueError(f"instrument out of range: {instr}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_UI_INSTR_SET, bytes([instr])))
        payload = self.wait_for_sysex(CMD_UI_INSTR_SET, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short UI_INSTR_SET reply: {payload!r}")
        if payload[1] != CMD_STATUS_OK:
            raise ValueError(f"UI_INSTR_SET status {payload[1]:#04x}")

    def ui_track_set(self, track: int, timeout: float = 1.0) -> None:
        """Park the UI's visible-track cursor (ui_selected_group +
        ui_selected_tracks) so SEQ_UI_VisibleTrackGet() returns `track`.
        Required by phase F.3 cross-track capture tests.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_UI_TRACK_SET, bytes([track])))
        payload = self.wait_for_sysex(CMD_UI_TRACK_SET, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short UI_TRACK_SET reply: {payload!r}")
        if payload[1] != CMD_STATUS_OK:
            raise ValueError(f"UI_TRACK_SET status {payload[1]:#04x}")

    def ui_track_get(self, timeout: float = 1.0) -> int:
        """Return SEQ_UI_VisibleTrackGet() (0..15) — the UI's current visible
        track. Used to verify track selection (e.g. that the select row still
        switches tracks after a capture gesture)."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_UI_TRACK_GET, b""))
        payload = self.wait_for_sysex(CMD_UI_TRACK_GET, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short UI_TRACK_GET reply: {payload!r}")
        if payload[1] != CMD_STATUS_OK:
            raise ValueError(f"UI_TRACK_GET status {payload[1]:#04x}")
        return payload[0]

    def track_drum_par_set(
        self, track: int, instr: int, step: int, value: int, timeout: float = 1.0
    ) -> None:
        """Direct write to a drum slot's Note par-layer step. Use to seed
        drum content without engaging a generator. Track must be drum-mode
        with a Note par-layer assigned (e.g. via track_drum_init()).
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instr <= 15:
            raise ValueError(f"instr out of range: {instr}")
        if not 0 <= step <= 127:
            raise ValueError(f"step out of range: {step}")
        if not 0 <= value <= 127:
            raise ValueError(f"value out of range: {value}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_TRACK_DRUM_PAR_SET, bytes([track, instr, step, value]))
        )
        payload = self.wait_for_sysex(
            CMD_TRACK_DRUM_PAR_SET, timeout=timeout, since=since
        )
        if len(payload) < 4:
            raise RuntimeError(f"short TRACK_DRUM_PAR_SET reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise ValueError(f"TRACK_DRUM_PAR_SET status {payload[3]:#04x}")

    def track_drum_par_get(
        self, track: int, instr: int, step: int, timeout: float = 1.0
    ) -> int:
        """Read a single drum slot's Note par-layer step value. Returns
        the 7-bit value. Raises if the track is not drum-mode / no Note
        par-layer assigned."""
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instr <= 15:
            raise ValueError(f"instr out of range: {instr}")
        if not 0 <= step <= 127:
            raise ValueError(f"step out of range: {step}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_TRACK_DRUM_PAR_GET, bytes([track, instr, step]))
        )
        payload = self.wait_for_sysex(
            CMD_TRACK_DRUM_PAR_GET, timeout=timeout, since=since
        )
        if len(payload) < 5:
            raise RuntimeError(f"short TRACK_DRUM_PAR_GET reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise ValueError(f"TRACK_DRUM_PAR_GET status {payload[4]:#04x}")
        return payload[3]

    def track_par_set(
        self, track: int, layer: int, instr: int, step: int, value: int,
        timeout: float = 1.0,
    ) -> None:
        """General (mode-agnostic, layer-explicit) par-layer write — the
        note-track counterpart of track_drum_par_set. Writes any track/layer/
        instr/step directly via SEQ_PAR_Set (no Drum gate). Use to seed a
        melodic track's Note/Velocity/etc. layers."""
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= layer <= 127:
            raise ValueError(f"layer out of range: {layer}")
        if not 0 <= instr <= 127:
            raise ValueError(f"instr out of range: {instr}")
        if not 0 <= step <= 127:
            raise ValueError(f"step out of range: {step}")
        if not 0 <= value <= 127:
            raise ValueError(f"value out of range: {value}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_TRACK_PAR_SET, bytes([track, layer, instr, step, value]))
        )
        payload = self.wait_for_sysex(CMD_TRACK_PAR_SET, timeout=timeout, since=since)
        if len(payload) < 6:
            raise RuntimeError(f"short TRACK_PAR_SET reply: {payload!r}")
        if payload[5] != CMD_STATUS_OK:
            raise ValueError(f"TRACK_PAR_SET status {payload[5]:#04x}")

    def track_par_get(
        self, track: int, layer: int, instr: int, step: int, timeout: float = 1.0
    ) -> int:
        """General (mode-agnostic, layer-explicit) par-layer read — the
        note-track counterpart of track_drum_par_get. Returns the 7-bit
        SEQ_PAR_Get(track, step, layer, instr) output-mirror value for any
        track/layer, so note-track captures can be verified byte-for-byte."""
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= layer <= 127:
            raise ValueError(f"layer out of range: {layer}")
        if not 0 <= instr <= 127:
            raise ValueError(f"instr out of range: {instr}")
        if not 0 <= step <= 127:
            raise ValueError(f"step out of range: {step}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TRACK_PAR_GET, bytes([track, layer, instr, step])))
        payload = self.wait_for_sysex(CMD_TRACK_PAR_GET, timeout=timeout, since=since)
        if len(payload) < 6:
            raise RuntimeError(f"short TRACK_PAR_GET reply: {payload!r}")
        if payload[5] != CMD_STATUS_OK:
            raise ValueError(f"TRACK_PAR_GET status {payload[5]:#04x}")
        return payload[4]

    def global_scale_set(
        self, scale: int, root_selection: int = 0, keyb_root: int = 0,
        timeout: float = 1.0,
    ) -> None:
        """Pin the global scale/root (seq_core_global_scale +
        global_scale_root_selection + keyb_scale_root) that force-to-scale reads
        when a track has no per-step Scale/Root par-layer. board.reset() does NOT
        touch these, so force-scale tests must set them explicitly for a
        deterministic key. scale = seq_scale_table index (0 = Major);
        root_selection 0 -> root from keyb_root (0..11), >0 -> root_selection-1."""
        if not 0 <= scale <= 127:
            raise ValueError(f"scale out of range: {scale}")
        if not 0 <= root_selection <= 127:
            raise ValueError(f"root_selection out of range: {root_selection}")
        if not 0 <= keyb_root <= 127:
            raise ValueError(f"keyb_root out of range: {keyb_root}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_GLOBAL_SCALE_SET, bytes([scale, root_selection, keyb_root]))
        )
        payload = self.wait_for_sysex(CMD_GLOBAL_SCALE_SET, timeout=timeout, since=since)
        if len(payload) < 4:
            raise RuntimeError(f"short GLOBAL_SCALE_SET reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise ValueError(f"GLOBAL_SCALE_SET status {payload[3]:#04x}")

    def tension_set(self, gravity: int, timeout: float = 1.0) -> None:
        """Set the global GRAVITY dial (Tension Workbench). gravity is the
        signed -64..+63 dial value (0 = detent / pass-through); it's sent biased
        by +64 to stay 7-bit-safe on the wire. Dirties all tracks so the change
        renders. GRIP is per-track via cc_set(track, CC.TENSION_GRIP, ...)."""
        if not -64 <= gravity <= 63:
            raise ValueError(f"gravity out of range: {gravity}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TENSION_SET, bytes([(gravity + 64) & 0x7F])))
        payload = self.wait_for_sysex(CMD_TENSION_SET, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short TENSION_SET reply: {payload!r}")
        if payload[1] != CMD_STATUS_OK:
            raise ValueError(f"TENSION_SET status {payload[1]:#04x}")

    def tension_band_get(
        self, gravity: int, track: int = 0, bus: int | None = None,
        timeout: float = 1.0,
    ) -> tuple[int, int]:
        """Pure-function pin of the GRAVITY band-mask builder. Returns
        (zone, band) where zone is 0=detent/1..6=DRONE..SLIP and band is the
        12-bit target pitch-class mask for `gravity`'s zone against the chord
        held on `bus` (defaults to the track's CHORDMASK_BUS) and the current
        global scale/root. No render/transport needed."""
        if not -64 <= gravity <= 63:
            raise ValueError(f"gravity out of range: {gravity}")
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        payload_out = bytearray([(gravity + 64) & 0x7F, track & 0x0F])
        if bus is not None:
            payload_out.append(bus & 0x03)
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TENSION_BAND_GET, bytes(payload_out)))
        payload = self.wait_for_sysex(CMD_TENSION_BAND_GET, timeout=timeout, since=since)
        if len(payload) < 4:
            raise RuntimeError(f"short TENSION_BAND_GET reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise ValueError(f"TENSION_BAND_GET status {payload[3]:#04x}")
        zone = payload[0]
        band = payload[1] | (payload[2] << 7)
        return zone, band

    def tension_get(self, timeout: float = 1.0) -> int:
        """Read the current global GRAVITY dial (signed −64..+63)."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TENSION_GET))
        payload = self.wait_for_sysex(CMD_TENSION_GET, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short TENSION_GET reply: {payload!r}")
        if payload[1] != CMD_STATUS_OK:
            raise ValueError(f"TENSION_GET status {payload[1]:#04x}")
        return payload[0] - 64

    def tension_resolve(self, timeout: float = 1.0) -> None:
        """Trigger RESOLVE — ramp GRAVITY to the detent (instant when stopped)."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TENSION_RESOLVE))
        payload = self.wait_for_sysex(CMD_TENSION_RESOLVE, timeout=timeout, since=since)
        if not payload or payload[0] != CMD_STATUS_OK:
            raise RuntimeError(f"TENSION_RESOLVE failed: {payload!r}")

    def bounce(
        self,
        src_track: int,
        dst_bank: int,
        dst_pattern: int,
        dst_group: int = 0,
        timeout: float = 4.0,
    ) -> bool:
        """Trigger SEQ_CORE_CaptureToSlot on the firmware (capture → slot).

        Captures src_track's computed output (lossless — exact par/trg, CC
        layers included) into the slot at (dst_bank, dst_pattern): the source
        layers are overwritten with the forced-render output, generative CC is
        reset, the name gets a "BNC" prefix, and the slot is written. The source
        track's live RAM is restored byte-identical afterward. dst_group is
        vestigial (kept for payload shape).

        Returns True if the firmware committed successfully (>= 0). The 4s
        default timeout accommodates SD write latency.
        """
        if not 0 <= src_track <= 15:
            raise ValueError(f"src_track out of range: {src_track}")
        if not 0 <= dst_bank <= 7:
            raise ValueError(f"dst_bank out of range: {dst_bank}")
        if not 0 <= dst_pattern <= 127:
            raise ValueError(f"dst_pattern out of range: {dst_pattern}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(
                CMD_BOUNCE,
                bytes([src_track, dst_group, dst_bank, dst_pattern]),
            )
        )
        payload = self.wait_for_sysex(CMD_BOUNCE, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short BOUNCE reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise RuntimeError(f"BOUNCE dispatch status {payload[4]:#04x}")
        return payload[3] == CMD_STATUS_OK

    def capture_to_track(
        self,
        src_track: int,
        dst_track: int,
        timeout: float = 4.0,
    ) -> bool:
        """Trigger SEQ_CORE_CaptureToTrack on the firmware (capture → track).

        Captures src_track's computed output (lossless) onto dst_track in the
        current pattern (RAM only — no SD). dst inherits src's
        event-mode/geometry/lower-48 CCs so the captured bytes read correctly,
        then its generative CC is reset. Returns True on success (>= 0).
        """
        if not 0 <= src_track <= 15:
            raise ValueError(f"src_track out of range: {src_track}")
        if not 0 <= dst_track <= 15:
            raise ValueError(f"dst_track out of range: {dst_track}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_CAPTURE_TO_TRACK, bytes([src_track, dst_track])))
        payload = self.wait_for_sysex(CMD_CAPTURE_TO_TRACK, timeout=timeout, since=since)
        if len(payload) < 4:
            raise RuntimeError(f"short CAPTURE_TO_TRACK reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise RuntimeError(f"CAPTURE_TO_TRACK dispatch status {payload[3]:#04x}")
        return payload[2] == CMD_STATUS_OK

    def capture_to_slot_track(
        self,
        src_track: int,
        dst_track: int,
        dst_bank: int,
        dst_pattern: int,
        timeout: float = 4.0,
    ) -> bool:
        """Trigger SEQ_CORE_CaptureToSlotTrack (capture → track-in-slot, saved).

        Renders src_track's computed output into dst_track of slot
        (dst_bank, dst_pattern), persisted to SD, preserving the slot's other
        tracks. The dst group's live RAM is restored afterward. Returns True on
        success. The 4s default timeout accommodates two SD ops.
        """
        if not 0 <= src_track <= 15:
            raise ValueError(f"src_track out of range: {src_track}")
        if not 0 <= dst_track <= 15:
            raise ValueError(f"dst_track out of range: {dst_track}")
        if not 0 <= dst_bank <= 7:
            raise ValueError(f"dst_bank out of range: {dst_bank}")
        if not 0 <= dst_pattern <= 127:
            raise ValueError(f"dst_pattern out of range: {dst_pattern}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_CAPTURE_TO_SLOT_TRACK, bytes([src_track, dst_track, dst_bank, dst_pattern]))
        )
        payload = self.wait_for_sysex(CMD_CAPTURE_TO_SLOT_TRACK, timeout=timeout, since=since)
        if len(payload) < 4:
            raise RuntimeError(f"short CAPTURE_TO_SLOT_TRACK reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise RuntimeError(f"CAPTURE_TO_SLOT_TRACK dispatch status {payload[3]:#04x}")
        return payload[2] == CMD_STATUS_OK

    def pattern_load(
        self,
        group: int,
        bank: int,
        pattern: int,
        timeout: float = 4.0,
    ) -> bool:
        """Load (bank, pattern) into group via SEQ_PATTERN_Load.

        Synchronous SD read. Returns True if the read succeeded.
        """
        if not 0 <= group <= 3:
            raise ValueError(f"group out of range: {group}")
        if not 0 <= bank <= 7:
            raise ValueError(f"bank out of range: {bank}")
        if not 0 <= pattern <= 127:
            raise ValueError(f"pattern out of range: {pattern}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PATTERN_LOAD, bytes([group, bank, pattern])))
        payload = self.wait_for_sysex(CMD_PATTERN_LOAD, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short PATTERN_LOAD reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise RuntimeError(f"PATTERN_LOAD dispatch status {payload[4]:#04x}")
        return payload[3] == CMD_STATUS_OK

    def pattern_save(
        self, group: int, bank: int, pattern: int, timeout: float = 4.0
    ) -> bool:
        """Persist the working group's current in-RAM pattern (all 4 tracks) to
        the (bank, pattern) slot on SD. Use to snapshot a host-built rig so it
        survives reboot. Returns True if the write committed. The V3 ext block
        persists CCs 0x80..0x9f, so GRIP (0x9a) and the chord-mask CCs travel
        with the save (pinned by test_tension_persist)."""
        if not 0 <= group <= 3:
            raise ValueError(f"group out of range: {group}")
        if not 0 <= bank <= 7:
            raise ValueError(f"bank out of range: {bank}")
        if not 0 <= pattern <= 127:
            raise ValueError(f"pattern out of range: {pattern}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PATTERN_SAVE, bytes([group, bank, pattern])))
        payload = self.wait_for_sysex(CMD_PATTERN_SAVE, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short PATTERN_SAVE reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise RuntimeError(f"PATTERN_SAVE dispatch status {payload[4]:#04x}")
        return payload[3] == CMD_STATUS_OK

    def session_create(self, name: str, timeout: float = 60.0) -> None:
        """Create a NEW session (/SESSIONS/<name>) with default content and
        wait until it is active. The firmware arms an async format (a low-prio
        task writes the four bank files + config — several seconds), so this
        polls session_name_get until it reports the new name. Refuses if the
        session already exists (use session_load for that). Arming stops the
        sequencer and clears live state."""
        name = name.upper()  # FAT directory names are uppercase
        encoded = name.encode("ascii")
        if not 1 <= len(encoded) <= 8:
            raise ValueError(f"session name must be 1..8 ASCII chars: {name!r}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_SESSION_CREATE, encoded))
        payload = self.wait_for_sysex(CMD_SESSION_CREATE, timeout=8.0, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short SESSION_CREATE reply: {payload!r}")
        if payload[1] == 0x04:
            raise RuntimeError(f"session {name!r} already exists")
        if payload[1] != CMD_STATUS_OK or payload[0] != 0x01:
            raise RuntimeError(f"SESSION_CREATE failed, status {payload[1]:#04x}")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(1.0)
            try:
                if self.session_name_get() == name:
                    # The name is stored BEFORE load_sd_content re-reads all
                    # files of the new session (4 banks, seconds of SD I/O) —
                    # an immediate SD verb would time out on the mutex.
                    time.sleep(6.0)
                    return
            except (TimeoutError, RuntimeError):
                continue  # device busy formatting; keep polling
        raise TimeoutError(f"session {name!r} did not become active in {timeout}s")

    def track_load(
        self,
        dst_track: int,
        bank: int,
        pattern: int,
        slot_track: int,
        timeout: float = 4.0,
    ) -> bool:
        """Pull ONE stored track section into a live track (the RECOMBINE verb,
        SEQ_CORE_LoadTrackFromSlot). Any bank x pattern x section -> any of the
        16 live tracks; the dst group's other tracks and seq_pattern[] are
        untouched. Arms the track undo with dst's prior state. Returns True if
        the load committed; False = refused (e.g. unwritten slot, slot_track out
        of range) — slot_track is deliberately not range-checked here so refusal
        behavior is pinnable. Synchronous SD read."""
        if not 0 <= dst_track <= 15:
            raise ValueError(f"dst_track out of range: {dst_track}")
        if not 0 <= bank <= 7:
            raise ValueError(f"bank out of range: {bank}")
        if not 0 <= pattern <= 127:
            raise ValueError(f"pattern out of range: {pattern}")
        if not 0 <= slot_track <= 127:
            raise ValueError(f"slot_track not 7-bit safe: {slot_track}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_TRACK_LOAD, bytes([dst_track, bank, pattern, slot_track]))
        )
        payload = self.wait_for_sysex(CMD_TRACK_LOAD, timeout=timeout, since=since)
        if len(payload) < 6:
            raise RuntimeError(f"short TRACK_LOAD reply: {payload!r}")
        if payload[5] != CMD_STATUS_OK:
            raise RuntimeError(f"TRACK_LOAD dispatch status {payload[5]:#04x}")
        return payload[4] == CMD_STATUS_OK

    def track_undo(self, timeout: float = 2.0) -> int | None:
        """One-shot restore of the track undo victim (most recent destructive
        track-grain verb — e.g. a track_load). Returns the restored track
        number, or None if no snapshot was armed."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TRACK_UNDO, b""))
        payload = self.wait_for_sysex(CMD_TRACK_UNDO, timeout=timeout, since=since)
        if len(payload) < 3:
            raise RuntimeError(f"short TRACK_UNDO reply: {payload!r}")
        if payload[2] != CMD_STATUS_OK:
            raise RuntimeError(f"TRACK_UNDO dispatch status {payload[2]:#04x}")
        if payload[1] != CMD_STATUS_OK:
            return None
        return payload[0]

    def track_undo_query(self, timeout: float = 2.0) -> tuple[bool, int, int]:
        """Non-consuming peek at the track undo slot.

        Returns (valid, kind, track); kind 0 = live-RAM victim."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_TRACK_UNDO_QUERY, b""))
        payload = self.wait_for_sysex(CMD_TRACK_UNDO_QUERY, timeout=timeout, since=since)
        if len(payload) < 4:
            raise RuntimeError(f"short TRACK_UNDO_QUERY reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise RuntimeError(f"TRACK_UNDO_QUERY dispatch status {payload[3]:#04x}")
        return (payload[0] == 1, payload[1], payload[2])

    def dirty_query(self, timeout: float = 2.0) -> tuple[int, int]:
        """FEARLESS SWITCHING diagnostics: returns (dirty_mask, writeback_count).

        dirty_mask is a bit per group (live state diverged from the group's
        working slot). writeback_count counts auto-writebacks since boot
        (21 LSBs)."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_DIRTY_QUERY, b""))
        payload = self.wait_for_sysex(CMD_DIRTY_QUERY, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short DIRTY_QUERY reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise RuntimeError(f"DIRTY_QUERY dispatch status {payload[4]:#04x}")
        count = payload[1] | (payload[2] << 7) | (payload[3] << 14)
        return (payload[0], count)

    def dirty_set(self, group: int, value: bool, timeout: float = 2.0) -> int:
        """Force a group's FEARLESS dirty bit (test-only knob). Returns the
        dirty mask after the write."""
        if not 0 <= group <= 3:
            raise ValueError(f"group out of range: {group}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_DIRTY_SET, bytes([group, 1 if value else 0])))
        payload = self.wait_for_sysex(CMD_DIRTY_SET, timeout=timeout, since=since)
        if len(payload) < 4:
            raise RuntimeError(f"short DIRTY_SET reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise RuntimeError(f"DIRTY_SET dispatch status {payload[3]:#04x}")
        return payload[2]

    def pattern_change(
        self, group: int, bank: int, pattern: int, timeout: float = 6.0
    ) -> bool:
        """Switch group to (bank, pattern) via SEQ_PATTERN_Change — the REAL
        switch path, including the FEARLESS auto-writeback of a dirty outgoing
        group (unlike pattern_load, which is a raw read). With the sequencer
        stopped this is synchronous (SD write+read — generous timeout)."""
        if not 0 <= group <= 3:
            raise ValueError(f"group out of range: {group}")
        if not 0 <= bank <= 7:
            raise ValueError(f"bank out of range: {bank}")
        if not 0 <= pattern <= 127:
            raise ValueError(f"pattern out of range: {pattern}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_PATTERN_CHANGE, bytes([group, bank, pattern])))
        payload = self.wait_for_sysex(CMD_PATTERN_CHANGE, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short PATTERN_CHANGE reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise RuntimeError(f"PATTERN_CHANGE dispatch status {payload[4]:#04x}")
        return payload[3] == CMD_STATUS_OK

    def set_force_scale(self, track: int, on: bool, timeout: float = 1.0) -> None:
        """Toggle a track's FORCE_SCALE flag (bit 3 of MODE_FLAGS). The Tension
        Workbench POC rule wants FTS OFF on gripped tracks so the emission snap
        doesn't re-correct the field's push."""
        MODE_FLAGS = 0x41  # SEQ_CC_MODE_FLAGS; FORCE_SCALE is bit 3
        flags = self.cc_get(track, MODE_FLAGS, timeout=timeout)
        if on:
            flags |= 0x08
        else:
            flags &= ~0x08
        self.cc_set(track, MODE_FLAGS, flags & 0x7F, timeout=timeout)

    def session_name_get(self, timeout: float = 1.0) -> str:
        """Return the currently-active session name (e.g. "DEFAULT", "AUTO_TEST")."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_SESSION_NAME_GET))
        payload = self.wait_for_sysex(CMD_SESSION_NAME_GET, timeout=timeout, since=since)
        if len(payload) < 1:
            raise RuntimeError(f"short SESSION_NAME_GET reply: {payload!r}")
        nlen = payload[0]
        if 1 + nlen > len(payload):
            raise RuntimeError(
                f"SESSION_NAME_GET reply truncated: name_len={nlen}, payload={payload!r}"
            )
        return payload[1 : 1 + nlen].decode("ascii", errors="replace")

    def session_load(
        self, name: str, timeout: float = 8.0, discard_dirty: bool = True
    ) -> str:
        """Switch the active session by name. Mirrors the SAVE/SESSIONS menu flow.

        The load chain reads B/M/S/G/BM/C across all banks for the named session,
        so it's significantly slower than `pattern_load` — 8s default timeout.

        FEARLESS SWITCHING: on the device, a session hop auto-writes any dirty
        group back to its working slot first ("nothing lost"). In the harness
        that semantic is a baseline killer — the per-test fixture parks group 0
        on AUTOTEST A1, so a test that edited track 0 and then hops sessions
        would commit its debris INTO A1 (exactly how A1 was corrupted on
        2026-06-12; see fixtures/AUTOTEST/CONTENTS.md incident log). Default
        `discard_dirty=True` clears all four dirty bits before the hop so
        harness session swaps never commit. Pass False only when a test pins
        the writeback-on-hop behavior itself (test_fearless_switching).

        Returns the active session name AFTER the call (which will be `name` on
        success, or the previous name if the load failed and was rolled back).
        Raises RuntimeError on dispatch error (empty / too-long name) and
        RuntimeError on load failure so tests don't silently inherit the wrong
        session.
        """
        if not 1 <= len(name) <= SESSION_NAME_MAX_LEN:
            raise ValueError(
                f"session name must be 1..{SESSION_NAME_MAX_LEN} chars, got {len(name)!r}"
            )
        if not all(0x20 <= ord(c) < 0x80 for c in name):
            raise ValueError(f"session name must be 7-bit ASCII: {name!r}")
        if discard_dirty:
            try:
                for group in range(4):
                    self.dirty_set(group, False)
            except (TimeoutError, RuntimeError):
                pass  # firmware predates DIRTY_SET — nothing to discard
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_SESSION_LOAD, name.encode("ascii")))
        payload = self.wait_for_sysex(CMD_SESSION_LOAD, timeout=timeout, since=since)
        if len(payload) < 3:
            raise RuntimeError(f"short SESSION_LOAD reply: {payload!r}")
        load_ok, dispatch_status, nlen = payload[0], payload[1], payload[2]
        if 3 + nlen > len(payload):
            raise RuntimeError(
                f"SESSION_LOAD reply truncated: name_len={nlen}, payload={payload!r}"
            )
        active_name = payload[3 : 3 + nlen].decode("ascii", errors="replace")
        if dispatch_status == SESSION_STATUS_NAME_TOO_LONG:
            raise RuntimeError(f"SESSION_LOAD rejected name as too long: {name!r}")
        if dispatch_status != CMD_STATUS_OK:
            raise RuntimeError(
                f"SESSION_LOAD dispatch status {dispatch_status:#04x} (name={name!r}, active={active_name!r})"
            )
        if load_ok != CMD_STATUS_OK:
            raise RuntimeError(
                f"SESSION_LOAD failed for {name!r}; firmware rolled back to {active_name!r}"
            )
        return active_name

    def trg_byte_get(
        self,
        track: int,
        trg_layer: int = 0,
        instrument: int = 0,
        step8_start: int = 0,
        step8_count: int = 2,
        timeout: float = 1.0,
    ) -> tuple[bytes, bytes]:
        """Read raw trigger bytes for diagnostic comparison.

        Returns `(layer_bytes, output_bytes)` — both `step8_count` bytes long.
        `layer_bytes` is the source buffer (`seq_trg_layer_value`); `output_bytes`
        is the phase-A render-cache mirror (`seq_trg_output_value`) that the tick
        path actually reads from. Divergence indicates a stale cache.

        Each byte covers 8 steps, bit 0 = step (8*step8_start), bit 7 = step
        (8*step8_start + 7). step8_count is capped at 32 on the firmware side.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= trg_layer <= 7:
            raise ValueError(f"trg_layer out of range: {trg_layer}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        if not 1 <= step8_count <= 32:
            raise ValueError(f"step8_count out of range: {step8_count}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(
                CMD_TRG_BYTE_GET,
                bytes([track, trg_layer, instrument, step8_start, step8_count]),
            )
        )
        payload = self.wait_for_sysex(CMD_TRG_BYTE_GET, timeout=timeout, since=since)
        if len(payload) < 6:
            raise RuntimeError(f"short TRG_BYTE_GET reply: {payload!r}")
        status = payload[5]
        if status != CMD_STATUS_OK:
            raise RuntimeError(
                f"TRG_BYTE_GET status {status:#04x} for track={track} layer={trg_layer} "
                f"instr={instrument} start={step8_start} count={step8_count}"
            )
        raw = unpack7(payload[6:])
        expected = step8_count * 2
        if len(raw) < expected:
            raise RuntimeError(
                f"TRG_BYTE_GET underflow: got {len(raw)} bytes, expected {expected}"
            )
        layer_bytes = bytes(raw[0:expected:2])
        output_bytes = bytes(raw[1:expected:2])
        return layer_bytes, output_bytes

    def msp_query(self, timeout: float = 1.0) -> dict:
        """Read MSP/handler-stack high-water (phase D.0 measurement).

        Returns a dict with:
          high_water_bytes     — peak MSP usage since paint
          paint_extent_bytes   — size of the painted region (lo..hi)
          paint_initial_depth  — _estack - paint_hi (MSP already used at paint time)
          paint_lo             — absolute address of paint floor
          paint_hi             — absolute address of paint ceiling
          peak_usage_bytes     — paint_initial_depth + high_water_bytes (convenience)
        """
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_MSP_QUERY))
        payload = self.wait_for_sysex(CMD_MSP_QUERY, timeout=timeout, since=since)
        raw = unpack7(payload)
        if len(raw) < 20:
            raise RuntimeError(f"short MSP_QUERY reply ({len(raw)}b): {raw!r}")

        def u32(off: int) -> int:
            return raw[off] | (raw[off + 1] << 8) | (raw[off + 2] << 16) | (raw[off + 3] << 24)

        high_water = u32(0)
        extent     = u32(4)
        initial    = u32(8)
        lo         = u32(12)
        hi         = u32(16)
        return {
            "high_water_bytes": high_water,
            "paint_extent_bytes": extent,
            "paint_initial_depth": initial,
            "paint_lo": lo,
            "paint_hi": hi,
            "peak_usage_bytes": initial + high_water,
        }

    def generator_query(
        self,
        track: int,
        instrument: int,
        timeout: float = 1.0,
        with_anchor: bool = False,
    ) -> GeneratorState | None:
        """Read the live state of the generator slot for (track, instrument).

        Returns a GeneratorState if a slot is allocated, or None if there is
        no slot. Used by behavioral tests to verify LOCK / ROLL / depth math
        without needing to capture MIDI. with_anchor=True additionally ships
        anchor[64] (Stage B byte-identical persistence pins).
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        since = time.monotonic() - self._t0
        req = bytes([track, instrument, 0x01]) if with_anchor \
            else bytes([track, instrument])
        self.send_raw(frame(CMD_GENERATOR_QUERY, req))
        payload = self.wait_for_sysex(
            CMD_GENERATOR_QUERY, timeout=timeout, since=since
        )
        if len(payload) < 3:
            raise RuntimeError(f"short GENERATOR_QUERY reply: {payload!r}")
        status = payload[2]
        if status == 0x03:
            return None
        if status != CMD_STATUS_OK:
            raise RuntimeError(f"GENERATOR_QUERY status {status:#04x}")
        if len(payload) < 10:
            raise RuntimeError(f"short GENERATOR_QUERY ok-reply: {payload!r}")
        raw = unpack7(payload[10:])
        need = 168 if with_anchor else 104
        if len(raw) < need:
            raise RuntimeError(
                f"GENERATOR_QUERY raw underflow: {len(raw)} bytes, need {need}"
            )
        loop = bytes(raw[:64])
        locks_bitmap = bytes(raw[64:72])
        mult_packed = bytes(raw[72:104])
        anchor = bytes(raw[104:168]) if with_anchor else None
        locks = tuple(
            bool((locks_bitmap[s >> 3] >> (s & 7)) & 1) for s in range(64)
        )
        # MULT is 4 bits per step, packed two-per-byte: low nibble even,
        # high nibble odd. Matches SEQ_GENERATOR_MultGet on the firmware side.
        mult = tuple(
            (mult_packed[s >> 1] >> 4) & 0x0f if (s & 1)
            else mult_packed[s >> 1] & 0x0f
            for s in range(64)
        )
        return GeneratorState(
            track=payload[0],
            instrument=payload[1],
            range_min=payload[3],
            range_max=payload[4],
            mutation_rate=payload[5],
            mutation_depth=payload[6],
            contour_shape=payload[7],
            engaged=bool(payload[8]),
            anchor_valid=bool(payload[9]),
            loop=loop,
            locks=locks,
            mult=mult,
            anchor=anchor,
        )

    def generator_lock_set(
        self,
        track: int,
        instrument: int,
        step: int,
        on: bool,
        timeout: float = 1.0,
    ) -> None:
        """Set/clear the per-step LOCK bit on the (track, instrument) slot.

        Stage B companion to generator_mult_set: lets persistence pins sculpt
        the lock bitmap deterministically without driving the PITCHGEN page
        cursor. Raises ValueError if no slot exists or step is out of range.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        if not 0 <= step <= 63:
            raise ValueError(f"step out of range: {step}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(
                CMD_GENERATOR_LOCK_SET,
                bytes([track, instrument, step, 1 if on else 0]),
            )
        )
        payload = self.wait_for_sysex(
            CMD_GENERATOR_LOCK_SET, timeout=timeout, since=since
        )
        if len(payload) < 3:
            raise RuntimeError(f"short GENERATOR_LOCK_SET reply: {payload!r}")
        if payload[2] != CMD_STATUS_OK:
            raise ValueError(
                f"GENERATOR_LOCK_SET status {payload[2]:#04x} — no slot?"
            )

    def generator_tick_force(
        self,
        track: int,
        instrument: int,
        timeout: float = 1.0,
    ) -> None:
        """Force one mutate cycle on the (track, instrument) generator slot.

        Equivalent to what SEQ_GENERATOR_Tick would do on a real measure-
        boundary track wrap, but synchronous and decoupled from playback.
        Lets behavioral tests pin mutate-path contracts (depth=0 freezing,
        LOCK preservation, MULT scaling) deterministically — ROLL bypasses
        rate/depth/MULT and can't substitute.

        Raises ValueError if no slot is allocated for the pair.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(CMD_GENERATOR_TICK_FORCE, bytes([track, instrument]))
        )
        payload = self.wait_for_sysex(
            CMD_GENERATOR_TICK_FORCE, timeout=timeout, since=since
        )
        if len(payload) < 3:
            raise RuntimeError(f"short GENERATOR_TICK_FORCE reply: {payload!r}")
        if payload[2] != CMD_STATUS_OK:
            raise ValueError(
                f"GENERATOR_TICK_FORCE status {payload[2]:#04x} — no slot?"
            )

    def generator_dial_set(
        self,
        track: int,
        instrument: int,
        dial_id: int,
        value: int,
        timeout: float = 1.0,
    ) -> None:
        """Set a single generator dial directly on the slot.

        `dial_id` is one of DIAL_RANGE_MIN / DIAL_RANGE_MAX / DIAL_RATE /
        DIAL_DEPTH / DIAL_CONTOUR. Bypasses SEQ_UI_Var8_Inc so behavioral
        tests can land on exact target values (0, 127, etc.) without
        spamming encoder events. The firmware does not range-clamp on
        write; callers are responsible for staying inside each dial's
        documented range.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        if not 0 <= dial_id <= 4:
            raise ValueError(f"dial_id out of range: {dial_id}")
        if not 0 <= value <= 127:
            raise ValueError(f"value out of range: {value}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(
                CMD_GENERATOR_DIAL_SET,
                bytes([track, instrument, dial_id, value]),
            )
        )
        payload = self.wait_for_sysex(
            CMD_GENERATOR_DIAL_SET, timeout=timeout, since=since
        )
        if len(payload) < 4:
            raise RuntimeError(f"short GENERATOR_DIAL_SET reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise ValueError(
                f"GENERATOR_DIAL_SET status {payload[3]:#04x} (dial={dial_id})"
            )

    def generator_mult_set(
        self,
        track: int,
        instrument: int,
        step: int,
        code: int,
        timeout: float = 1.0,
    ) -> None:
        """Set the per-step MULT code directly (0..15; 0..3 are the live
        cycle 0×/0.5×/1×/2×, 4..15 reserved → 1×).

        Bypasses the GP7 cycle gesture so tests can stamp any exact code.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        if not 0 <= step < 64:
            raise ValueError(f"step out of range: {step}")
        if not 0 <= code <= 15:
            raise ValueError(f"code out of range: {code}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(
                CMD_GENERATOR_MULT_SET,
                bytes([track, instrument, step, code]),
            )
        )
        payload = self.wait_for_sysex(
            CMD_GENERATOR_MULT_SET, timeout=timeout, since=since
        )
        if len(payload) < 4:
            raise RuntimeError(f"short GENERATOR_MULT_SET reply: {payload!r}")
        if payload[3] != CMD_STATUS_OK:
            raise ValueError(
                f"GENERATOR_MULT_SET status {payload[3]:#04x} "
                f"(step={step}, code={code})"
            )

    def lcd_snapshot(self, timeout: float = 1.0) -> LCDSnapshot:
        """Read the current 2x80 LCD buffer from the firmware."""
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_LCD_SNAPSHOT))
        payload = self.wait_for_sysex(CMD_LCD_SNAPSHOT, timeout=timeout, since=since)
        if len(payload) < 2:
            raise RuntimeError(f"short LCD_SNAPSHOT reply: {payload!r}")
        lines, cols = payload[0], payload[1]
        raw = unpack7(payload[2:])
        expected = lines * cols
        if len(raw) < expected:
            raise RuntimeError(
                f"LCD snapshot underflow: got {len(raw)} bytes, expected {expected}"
            )
        return LCDSnapshot(lines=lines, columns=cols, raw=raw[:expected])


def list_ports() -> dict[str, list[str]]:
    """Diagnostic helper used by `make test-discover`."""
    return {
        "inputs": rtmidi.MidiIn().get_ports(),
        "outputs": rtmidi.MidiOut().get_ports(),
    }
