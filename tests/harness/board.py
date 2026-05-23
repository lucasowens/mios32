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
    CMD_BUTTON,
    CMD_ENCODER,
    CMD_LCD_SNAPSHOT,
    CMD_PAGE_SET,
    CMD_PING,
    CMD_RESET_STATE,
    CMD_TICK_QUERY,
    CMD_TRACK_CONFIG,
    ENCODER_STATUS_DISPATCHED,
    ENCODER_STATUS_OUT_OF_RANGE,
    MidiPort,
    RESET_DEFAULT,
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
