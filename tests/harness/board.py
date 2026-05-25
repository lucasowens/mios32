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
    CMD_UI_TRACK_SET,
    CMD_TRACK_DRUM_PAR_SET,
    CMD_TRACK_DRUM_PAR_GET,
    CMD_STATUS_OK,
    CMD_TICK_QUERY,
    CMD_TRACK_CONFIG,
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
    """

    track: int
    instrument: int
    range_min: int
    range_max: int
    mutation_rate: int
    mutation_depth: int
    contour_shape: int
    engaged: bool
    loop: bytes              # 64 bytes
    locks: tuple[bool, ...]  # 64 entries


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

    def bounce(
        self,
        src_track: int,
        dst_bank: int,
        dst_pattern: int,
        num_measures: int = 1,
        dst_group: int = 0,
        timeout: float = 4.0,
    ) -> bool:
        """Trigger SEQ_CAPTURE_CommitToSlot on the firmware.

        The captured tape is whatever's currently in the per-track ring buffer
        (filled by recent live playback). The destination slot at
        (dst_bank, dst_pattern) is overwritten with the sanitized source CC +
        captured layers. Source state is restored from an in-RAM snapshot.

        Returns True if the firmware committed successfully (SEQ_CAPTURE
        returned >=0). Note: an empty ring (no recent playback) returns False.
        The 4s default timeout accommodates SD write latency.
        """
        if not 0 <= src_track <= 15:
            raise ValueError(f"src_track out of range: {src_track}")
        if not 0 <= dst_bank <= 7:
            raise ValueError(f"dst_bank out of range: {dst_bank}")
        if not 0 <= dst_pattern <= 127:
            raise ValueError(f"dst_pattern out of range: {dst_pattern}")
        if not 1 <= num_measures <= 16:
            raise ValueError(f"num_measures out of range: {num_measures}")
        since = time.monotonic() - self._t0
        self.send_raw(
            frame(
                CMD_BOUNCE,
                bytes([src_track, dst_group, dst_bank, dst_pattern, num_measures]),
            )
        )
        payload = self.wait_for_sysex(CMD_BOUNCE, timeout=timeout, since=since)
        if len(payload) < 5:
            raise RuntimeError(f"short BOUNCE reply: {payload!r}")
        if payload[4] != CMD_STATUS_OK:
            raise RuntimeError(f"BOUNCE dispatch status {payload[4]:#04x}")
        return payload[3] == CMD_STATUS_OK

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

    def session_load(self, name: str, timeout: float = 8.0) -> str:
        """Switch the active session by name. Mirrors the SAVE/SESSIONS menu flow.

        The load chain reads B/M/S/G/BM/C across all banks for the named session,
        so it's significantly slower than `pattern_load` — 8s default timeout.

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
    ) -> GeneratorState | None:
        """Read the live state of the generator slot for (track, instrument).

        Returns a GeneratorState if a slot is allocated, or None if there is
        no slot. Used by behavioral tests to verify LOCK / ROLL / depth math
        without needing to capture MIDI.
        """
        if not 0 <= track <= 15:
            raise ValueError(f"track out of range: {track}")
        if not 0 <= instrument <= 15:
            raise ValueError(f"instrument out of range: {instrument}")
        since = time.monotonic() - self._t0
        self.send_raw(frame(CMD_GENERATOR_QUERY, bytes([track, instrument])))
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
        if len(payload) < 9:
            raise RuntimeError(f"short GENERATOR_QUERY ok-reply: {payload!r}")
        raw = unpack7(payload[9:])
        if len(raw) < 72:
            raise RuntimeError(
                f"GENERATOR_QUERY raw underflow: {len(raw)} bytes, need 72"
            )
        loop = bytes(raw[:64])
        locks_bitmap = bytes(raw[64:72])
        locks = tuple(
            bool((locks_bitmap[s >> 3] >> (s & 7)) & 1) for s in range(64)
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
            loop=loop,
            locks=locks,
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
