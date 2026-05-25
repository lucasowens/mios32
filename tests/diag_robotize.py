"""Diagnose what state the robotize tests actually set up.

The robotize tests started failing after the conftest per-test pattern_load(A1)
landed. We need to see which CC the GP encoders are actually writing to (the
FX_ROBOTIZE page splits each item across two CCs: a magnitude under no
modifier, a probability under SELECT held).
"""

import time

from harness import Board, Button, Encoder, MidiPort, Page
from harness.sysex import CC


ROBOTIZE_CCS = {
    0x80: "MASK1",
    0x81: "MASK2",
    0x82: "ACTIVE",
    0x83: "PROBABILITY",
    0x84: "SKIP_PROBABILITY",
    0x87: "VEL",
    0x88: "VEL_PROBABILITY",
}


def _arm_all_robotize_steps(board) -> None:
    board.button(Button.SELECT, depressed=False)
    try:
        for gp in range(1, 17):
            board.press(Button.GP(gp), hold_seconds=0.005)
    finally:
        board.button(Button.SELECT, depressed=True)
    time.sleep(0.1)


def _setup_track0_euclid_full(board) -> None:
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
    board.page_set(Page.TRKEUCLID)
    time.sleep(0.1)
    board.turn(Encoder.GP(12), -10)
    board.turn(Encoder.GP(9), -99)
    board.turn(Encoder.GP(10), -99)
    board.turn(Encoder.GP(9), +15)
    board.turn(Encoder.GP(10), +16)
    time.sleep(0.15)


def _dump_ccs(board, label: str) -> None:
    print(f"\n--- {label} ---")
    for cc, name in ROBOTIZE_CCS.items():
        try:
            v = board.cc_get(0, cc)
        except Exception as e:
            v = f"<err: {e}>"
        print(f"  {name:18s} (0x{cc:02x}) = {v}")


def main():
    with Board() as board:
        print("PING:", board.ping())
        board.reset()
        # Mirror what conftest does now: reload AUTOTEST A1 so state matches.
        board.pattern_load(group=0, bank=0, pattern=0)
        time.sleep(0.2)

        _dump_ccs(board, "after pattern_load(A1)")

        _setup_track0_euclid_full(board)
        _dump_ccs(board, "after euclid setup")

        # Mirror the SKIP test's setup.
        board.page_set(Page.FX_ROBOTIZE)
        time.sleep(0.1)
        board.encoder(Encoder.GP(2), +1)   # ACTIVE = 1
        board.turn(Encoder.GP(3), +32)     # PROBABILITY = max
        board.turn(Encoder.GP(4), +32)     # SKIP probability = max
        _arm_all_robotize_steps(board)
        _dump_ccs(board, "after SKIP encoder dial + arm steps")

        # Now also set VEL_PROBABILITY via SELECT+GP7, and VEL via GP7.
        board.button(Button.SELECT, depressed=False)
        board.turn(Encoder.GP(7), +32)    # VEL_PROBABILITY (SELECT held)
        board.button(Button.SELECT, depressed=True)
        board.turn(Encoder.GP(7), +32)    # VEL magnitude (no SELECT)
        _dump_ccs(board, "after VEL_PROBABILITY (SELECT+GP7) + VEL (GP7)")

        # Capture some output.
        capture_t0 = board.capture_start()
        board.press(Button.PLAY)
        time.sleep(2.0)
        board.press(Button.STOP)
        time.sleep(0.2)
        notes = [
            e for e in board.capture_notes(since=capture_t0)
            if e.is_on and e.channel == 0
        ]
        vels = sorted({e.velocity for e in notes})
        print(f"\n{len(notes)} notes captured, distinct velocities = {vels}")


if __name__ == "__main__":
    main()
