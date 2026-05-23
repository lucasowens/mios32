"""Diagnostic: confirm that pressing PLAY via the harness actually starts transport.

Doesn't touch generators; just plays whatever pattern is currently loaded.
"""

import time

from harness import Board, Button, MidiPort


def main():
    with Board() as board:
        print("PING:", board.ping())
        print("setting track 0 -> USB1 ch 1 ...")
        board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
        time.sleep(0.1)

        print("snapshot before PLAY:")
        print(" ", board.lcd_snapshot().line(0))

        print("\nEDIT line 1 (trigger row):")
        snap = board.lcd_snapshot()
        print(" ", snap.line(1))

        print("\nstate before PLAY:")
        for k, v in board.tick_query().items():
            print(f"  {k}: {v}")

        print("\nPRESSING PLAY via harness...")
        capture_t0 = board.capture_start()
        board.press(Button.PLAY)
        print(" snapshot 0.5s after PLAY:")
        time.sleep(0.5)
        print(" ", board.lcd_snapshot().line(0))
        print(" state 0.5s after PLAY:")
        for k, v in board.tick_query().items():
            print(f"   {k}: {v}")

        print(" sleeping 2s for capture...")
        time.sleep(2.0)

        notes = [e for e in board.capture_notes(since=capture_t0) if e.is_on]
        print(f" captured {len(notes)} Note Ons:")
        for e in notes[:10]:
            print(f"   t={e.timestamp - capture_t0:.3f}s port[{e.port_idx}] ch={e.channel+1} note={e.note} vel={e.velocity}")

        print("\nPRESSING STOP...")
        board.press(Button.STOP)
        time.sleep(0.1)
        print(" snapshot after STOP:")
        print(" ", board.lcd_snapshot().line(0))


if __name__ == "__main__":
    main()
