"""Diagnostic: rung 6 — does seq_midexp MIDI export carry the Voicing strum stagger?

OPEN_ITEMS §4 (written during the act-1 voicing session) claims export renders
unstrummed onsets. Source reading says otherwise: strum_ofs is composed into the
scheduled TIMESTAMP (seq_core.c, the 4 OnOffEvent schedule sites), and
SEQ_MIDEXP_GenerateFile replays the real scheduler tick-by-tick — the hooked
SEQ_MIDI_OUT_Handler drains strictly by timestamp <= export_tick and writes the
delta from the drain tick. Echo trains already export this way.

This one-shot proves it end-to-end on hardware with NO new firmware:
  1. chord track 0: gates on steps 0+8, Chord1 layer painted Maj.I (3 voices)
  2. UI-walk the Disk page MIDI-export flow (Track mode, 1 measure, file "A")
  3. pull /MIDI/A.MID back over the MIOS Filebrowser SysEx protocol
  4. parse the file (mido) and assert the per-voice tick offsets:
     strum=64 (detent)  -> each chord's 3 note-ons at the SAME tick
     strum=64+20 (up)   -> ticks t, t+20, t+40 in ascending pitch order

Perturbs the device (stops transport, rewrites track 0, writes/deletes
/MIDI/A.MID) and restores AUTOTEST A3 + neutral dials at the end. Don't run
while a jam or the HIL suite is in flight.

Usage:
    cd tests && .venv/bin/python diag_strum_export.py
"""

import io
import sys
import time

import mido

from harness import Board, Button, CC, MidiPort
from harness.sysex import Page

# --- protocol constants ------------------------------------------------------

MIOS32_SYSEX_HEADER = bytes([0xF0, 0x00, 0x00, 0x7E, 0x32])
MIOS32_DEVICE_ID = 0x00
MIOS32_MIDI_SYSEX_DEBUG = 0x0D
DEBUG_INPUT_STRING = 0x01   # host -> device: string for FILE_BrowserHandler
DEBUG_OUTPUT_STRING = 0x41  # device -> host: filebrowser reply frames

PAR_TYPE_CHORD1 = 2
MAJ_I = 0x40    # Maj.I, transpose-0 octave bits -> 3 voices
TRACK = 0
STRUM_TICKS = 20

NEUTRAL_DIALS = {
    CC.VOICE_SPREAD: 0,
    CC.VOICE_INV: 0,
    CC.VOICE_STRUM: 64,
    CC.VOICE_DROP: 0,
    CC.VOICE_TILT: 64,
}

EXPORT_PATH = "/MIDI/A.MID"  # one GP2 press in the keypad editor = 'A'


# --- MIOS Filebrowser client (device already speaks it via seq_terminal) ------

class Filebrowser:
    """Minimal client for FILE_BrowserHandler over the debug-string SysEx pipe.

    Command:  F0 00 00 7E 32 <devid> 0D 01 <ascii>\\n F7
    Reply:    F0 00 00 7E 32 <devid> 0D 41 <type char><payload> F7  (per frame)
      dir   -> 'D' + ",Fname,Dname..."   ('!' no card, '-' error)
      del   -> 'X' + '#' ok / '-' fail
      mkdir -> 'M' + '#' ok / '-' fail
      read  -> 'R' + decimal length, then 'r' + "%08X " + hex pairs per 32-byte
               block until the whole file has been streamed
    """

    REPLY_PREFIX = MIOS32_SYSEX_HEADER + bytes(
        [MIOS32_DEVICE_ID, MIOS32_MIDI_SYSEX_DEBUG, DEBUG_OUTPUT_STRING]
    )

    def __init__(self, board: Board):
        self.board = board

    def _send(self, command: str) -> float:
        since = time.monotonic() - self.board._t0
        msg = (
            MIOS32_SYSEX_HEADER
            + bytes([MIOS32_DEVICE_ID, MIOS32_MIDI_SYSEX_DEBUG, DEBUG_INPUT_STRING])
            + command.encode("ascii")
            + b"\n\xf7"
        )
        self.board.send_raw(msg)
        return since

    def _frames(self, since: float) -> list[bytes]:
        with self.board._lock:
            snapshot = list(self.board._messages)
        frames = []
        for m in snapshot:
            if m.timestamp < since:
                continue
            if m.data.startswith(self.REPLY_PREFIX) and m.data[-1] == 0xF7:
                # body chars are sent in 3-byte groups NUL-padded at the end
                frames.append(m.data[len(self.REPLY_PREFIX):-1].rstrip(b"\x00"))
        return frames

    def cmd(self, command: str, reply_type: bytes, timeout: float = 5.0) -> bytes:
        """Run a one-frame command (dir/del/mkdir); return the frame payload."""
        since = self._send(command)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for f in self._frames(since):
                if f[:1] == reply_type:
                    return f[1:]
            time.sleep(0.02)
        raise TimeoutError(f"filebrowser: no {reply_type!r} reply to {command!r}")

    def read(self, path: str, timeout: float = 30.0) -> bytes:
        since = self._send(f"read {path}")
        deadline = time.monotonic() + timeout
        length = None
        blocks: dict[int, bytes] = {}
        while time.monotonic() < deadline:
            for f in self._frames(since):
                if f[:1] == b"R" and length is None:
                    body = f[1:]
                    if body[:1] in (b"-", b"!"):
                        raise RuntimeError(f"filebrowser read {path!r}: {body!r}")
                    length = int(body.decode("ascii"))
                elif f[:1] == b"r":
                    addr, hexpart = f[1:].split(b" ", 1)
                    blocks[int(addr, 16)] = bytes.fromhex(hexpart.decode("ascii"))
            if length is not None:
                data = b"".join(blocks[k] for k in sorted(blocks))
                if len(data) >= length:
                    return data[:length]
            time.sleep(0.02)
        got = sum(len(b) for b in blocks.values())
        raise TimeoutError(f"filebrowser read {path!r}: {got}/{length} bytes")


# --- device-side setup + export walk ------------------------------------------

def setup_chord_track(board: Board) -> None:
    """Track 0: 16 steps x 4 par layers, gates on steps 0+8 only, layer A ->
    Chord1 painted Maj.I everywhere, all voicing dials neutral."""
    board.track_note_init(TRACK)
    board.trg_byte_set(track=TRACK, step8=0, value=0x01, trg_layer=0, instrument=0)
    board.trg_byte_set(track=TRACK, step8=1, value=0x01, trg_layer=0, instrument=0)
    board.track_config(track=TRACK, midi_port=MidiPort.USB0, channel=0)
    board.cc_set(TRACK, CC.LAY_CONST_A1, PAR_TYPE_CHORD1)
    for cc, v in NEUTRAL_DIALS.items():
        board.cc_set(TRACK, cc, v)
    for step in range(16):
        board.track_par_set(TRACK, 0, 0, step, MAJ_I)
        # track_note_init zero-fills the par buffer, and layer B stays the
        # Velocity layer here — vel-0 notes are suppressed at emission (and
        # therefore in the export too), so paint an audible velocity.
        board.track_par_set(TRACK, 1, 0, step, 100)
    board.ui_track_set(TRACK)


def run_export(board: Board, fb: Filebrowser) -> bytes:
    """UI-walk Disk -> MIDI Files Export -> Track mode -> filename 'A' -> SAVE.
    The GP15 press dispatches DoMfExport synchronously in cmd_button, so its
    reply doubles as the completion signal. Returns the exported file bytes."""
    # no-exists path keeps the walk deterministic (no FEXISTS dialog)
    fb.cmd(f"del {EXPORT_PATH}", reply_type=b"X")

    board.page_set(Page.DISK)
    board.press(Button.GP(6))       # -> DIALOG_MF_EXPORT (ui_selected_item=MODE)
    board.encoder(0, -3)            # datawheel: clamp mode to 0 (AllGroups)
    board.encoder(0, 1)             # -> 1 = Track (exports the visible track)
    board.press(Button.GP(9))       # -> filename dialog (keypad, name cleared)
    board.press(Button.GP(2))       # keypad charset row 1 -> 'A'
    board.button(Button.GP(15), depressed=False, timeout=60.0)  # SAVE: export runs here
    board.button(Button.GP(15), depressed=True, timeout=5.0)

    return fb.read(EXPORT_PATH)


# --- MIDI file analysis ---------------------------------------------------------

def chord_onsets(data: bytes) -> list[list[tuple[int, int]]]:
    """Parse note-on (tick, pitch) events and cluster them into chords.
    Returns a list of chords, each [(tick, pitch), ...] sorted by pitch."""
    mf = mido.MidiFile(file=io.BytesIO(data))
    ons = []
    for tr in mf.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            if msg.type == "note_on" and msg.velocity > 0:
                ons.append((t, msg.note))
    ons.sort()
    chords: list[list[tuple[int, int]]] = []
    for tick, note in ons:
        if chords and tick - chords[-1][0][0] < 200:
            chords[-1].append((tick, note))
        else:
            chords.append([(tick, note)])
    return [sorted(c, key=lambda tn: tn[1]) for c in chords]


def describe(chords) -> str:
    return "; ".join(
        "chord @" + ",".join(f"{t}:{n}" for t, n in c) for c in chords
    )


def check(cond: bool, msg: str, failures: list[str]) -> None:
    tag = "PASS" if cond else "FAIL"
    print(f"  [{tag}] {msg}")
    if not cond:
        failures.append(msg)


def main() -> int:
    failures: list[str] = []
    with Board() as board:
        print("PING:", board.ping())
        fb = Filebrowser(board)
        board.reset()
        try:
            fb.cmd("mkdir /MIDI", reply_type=b"M")  # ok if it already exists

            setup_chord_track(board)

            print(f"\n--- export 1: strum dial at detent (64) — baseline ---")
            data = run_export(board, fb)
            print(f"  {len(data)} bytes")
            chords = chord_onsets(data)
            print(f"  {describe(chords)}")
            check(len(chords) == 2, f"2 chords in file (got {len(chords)})", failures)
            for c in chords:
                ticks = [t for t, _ in c]
                check(len(c) == 3, f"3 voices per chord (got {len(c)})", failures)
                check(
                    max(ticks) == min(ticks),
                    f"detent chord voices simultaneous (ticks {ticks})",
                    failures,
                )

            print(f"\n--- export 2: strum dial 64+{STRUM_TICKS} (up-strum) ---")
            board.cc_set(TRACK, CC.VOICE_STRUM, 64 + STRUM_TICKS)
            data = run_export(board, fb)
            print(f"  {len(data)} bytes")
            chords = chord_onsets(data)
            print(f"  {describe(chords)}")
            check(len(chords) == 2, f"2 chords in file (got {len(chords)})", failures)
            for c in chords:
                ticks = [t for t, _ in c]  # already sorted by ascending pitch
                base = ticks[0]
                expect = [base, base + STRUM_TICKS, base + 2 * STRUM_TICKS]
                check(
                    ticks == expect,
                    f"up-strum staggers {STRUM_TICKS} ticks/rank low-first "
                    f"(got {ticks}, want {expect})",
                    failures,
                )
        finally:
            print("\n--- restore ---")
            try:
                fb.cmd(f"del {EXPORT_PATH}", reply_type=b"X")
            except Exception as e:
                print(f"  WARNING: cleanup del failed: {e}")
            try:
                board.pattern_load(group=0, bank=0, pattern=2)  # AUTOTEST A3
                for cc, v in NEUTRAL_DIALS.items():
                    board.cc_set(TRACK, cc, v)
                board.reset()
            except Exception as e:
                print(f"  WARNING: restore failed: {e}")
                try:
                    print(board.lcd_snapshot())
                except Exception:
                    pass

    if failures:
        print(f"\nRESULT: {len(failures)} FAILURE(S) — export does NOT carry strum as-is")
        return 1
    print("\nRESULT: strum stagger IS in the exported MIDI file — OPEN_ITEMS claim is stale")
    return 0


if __name__ == "__main__":
    sys.exit(main())
