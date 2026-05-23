"""Diagnostic: configure track 0 to USB1 (where harness listens), then capture
every byte for 15 seconds while user presses PLAY manually.

Usage:
    cd tests && .venv/bin/python diag_listen.py
"""

import time

from harness import Board, Button, MidiPort

DURATION = 15.0


def main():
    with Board() as board:
        print("inputs opened:")
        for name in board.input_port_names:
            print(f"  {name}")
        print(f"output: {board.output_port_name}\n")

        print("PING:", board.ping())

        print("setting track 0 -> USB1 ch 1 ...")
        board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
        time.sleep(0.1)

        # Don't drain — we want to see *everything*, including whatever was
        # produced by track_config itself.
        print(f"\n>>> PRESS PLAY ON THE SEQ NOW. listening for {DURATION}s. <<<\n")
        t0 = time.monotonic() - board._t0
        last = 0
        while time.monotonic() - board._t0 - t0 < DURATION:
            elapsed = time.monotonic() - board._t0 - t0
            with board._lock:
                count = sum(1 for m in board._messages if m.timestamp >= t0)
            if int(elapsed) > last:
                last = int(elapsed)
                print(f"  ... {last}s elapsed, messages captured: {count}")
            time.sleep(0.2)

        with board._lock:
            msgs = [m for m in board._messages if m.timestamp >= t0]

        print(f"\ncaptured {len(msgs)} messages total:")
        for m in msgs[:60]:
            status = m.data[0] if m.data else 0
            high = status & 0xF0
            if high == 0x90:
                kind = f"NoteOn ch={(status & 0x0F) + 1} note={m.data[1]} vel={m.data[2]}"
            elif high == 0x80:
                kind = f"NoteOff ch={(status & 0x0F) + 1} note={m.data[1]} vel={m.data[2]}"
            elif status == 0xF0:
                kind = f"SysEx ({len(m.data)} bytes)"
            elif status == 0xF8:
                kind = "Clock"
            elif status == 0xFA:
                kind = "Start"
            elif status == 0xFC:
                kind = "Stop"
            else:
                kind = f"raw {m.data[:6].hex()}"
            print(f"  t={m.timestamp - t0:6.3f}s  port[{m.port_idx}]  {kind}")
        if len(msgs) > 60:
            print(f"  ... and {len(msgs) - 60} more")


if __name__ == "__main__":
    main()
