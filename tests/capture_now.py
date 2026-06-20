#!/usr/bin/env python3
"""Retroactive CAPTURE — by-ear / by-eye test driver (temporary; until the physical
CAPTURE gesture is built). Run from the tests/ dir with the venv active:

    source .venv/bin/activate

  python capture_now.py setup        # Test A: a fixed FORWARD reference melody on track 1
  python capture_now.py setup-gen    # Test B: a pitch-generator wander on track 1
  python capture_now.py ring         # show the always-on ring (track / depth / overflow)
  python capture_now.py grab K DST   # capture the last K bars into track (DST+1)  [0-indexed DST]

Flow: setup -> PLAY on the device a few bars -> STOP -> `grab` -> audition the dst track.
Track numbers in the commands are 0-indexed (track 1 on the panel = 0 here).
"""

import sys
import time

from harness import Board, Button, CC, Page
from harness.sysex import DIAL_DEPTH, DIAL_RANGE_MAX, DIAL_RANGE_MIN, DIAL_RATE

TRACK = 0                 # panel track 1 = the generative source
GP1 = Button.GP(1)
SEQ_CC_LENGTH = 0x4d
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0
TRKDIR_RANDOM_STEP = 5
EVENT_MODE_NOTE = 0
NOTE_LAYER = 0            # melodic note par-layer


def _melodic_track(b: Board) -> None:
    b.track_drum_init(TRACK)                          # clean known init
    b.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)   # -> Note (melodic): note par-layer exists
    b.cc_set(TRACK, SEQ_CC_LENGTH, 15)               # 16 steps = one whole measure
    b.ui_track_set(TRACK)
    # gate every step so the line actually sounds (gates are separate from pitch)
    b.trg_byte_set(TRACK, 0, 0xFF)
    b.trg_byte_set(TRACK, 1, 0xFF)


def setup_forward(b: Board) -> None:
    """Test A — a FIXED, recognizable forward line so the captured copy can be
    compared note-for-note (the unambiguous step-PHASE check). Step 0 is a high
    accent (72); steps 1..15 ascend from 48 — a rotation jumps the accent off the
    downbeat and the ascent wraps, both obvious by eye and ear."""
    _melodic_track(b)
    b.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    b.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, 16):
        b.track_par_set(TRACK, NOTE_LAYER, 0, step, 48 + step)
    print("Test A ready on panel track 1: forward line [72, 49, 50, ... 63], gates on.")
    print("Set the track's MIDI port/channel to your sound source, then PLAY a couple")
    print("bars, STOP, and run:  python capture_now.py grab 1 1")
    print("Then compare track 2's notes to track 1 (step 0 must be 72).")


def setup_gen(b: Board) -> None:
    """Test B — a pitch-generator wander + random-step traversal: the real
    generative material the capture is for."""
    _melodic_track(b)
    b.page_set(Page.PITCHGEN)
    b.ui_instrument_set(0)
    b.press(GP1)                                     # ENGAGE the generator
    time.sleep(0.85)
    b.generator_dial_set(TRACK, 0, DIAL_RANGE_MIN, 48)
    b.generator_dial_set(TRACK, 0, DIAL_RANGE_MAX, 72)
    b.generator_dial_set(TRACK, 0, DIAL_RATE, 80)    # moderate wander
    b.generator_dial_set(TRACK, 0, DIAL_DEPTH, 40)
    b.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_RANDOM_STEP)
    print("Test B ready on panel track 1: pitch-gen wander, random-step, gates on.")
    print("(This is a 16-instrument drum-layout track playing pitches on instrument 1;")
    print(" capture is capped at K<=4 bars by its par buffer — a true 1-voice Note track")
    print(" would allow K<=16, but needs a melodic-track-init that isn't built yet.)")
    print("Set the track's MIDI port/channel to your sound source, then PLAY several")
    print("bars (listen to it evolve), STOP, and run:  python capture_now.py grab 4 1")
    print("Then solo track 2 and play it — it should sound like the last 4 bars you heard.")


def ring(b: Board) -> None:
    q = b.capture_ring_query()
    tr = q["track"]
    print(f"ring: recording track {tr + 1 if tr < 16 else '(none)'} "
          f"(0-idx {tr}) | depth {q['depth']} bars | overflow {q['overflow']}")


def grab(b: Board, k: int, dst: int) -> None:
    q = b.capture_ring_query()
    src = q["track"]
    print(f"ring: track {src} depth {q['depth']} overflow {q['overflow']}")
    if src >= 16:
        print("  -> ring not recording any track (play a few bars on a visible track first).")
        return
    if q["depth"] <= k:
        print(f"  -> only {q['depth']} bars buffered; need > K={k}. Play more bars before STOP.")
    st = b.capture_span(src, k, dst)
    ok = (st == 0x01)
    print(f"capture_span(src={src}, K={k}, dst={dst}) -> {hex(st)}  {'OK' if ok else 'REFUSED'}")
    if ok:
        print(f"  captured into panel track {dst + 1}. Solo/audition it.")
    else:
        codes = {0x12: "src==dst", 0x13: "transport RUNNING (stop first)",
                 0x14: "ring not recording src", 0x15: "gen-slot overflow",
                 0x16: "not enough history (K too big)", 0x17: "steps>256",
                 0x18: "not a whole measure (length must be 15)",
                 0x19: "dst par overflow (K too large for this track's layout; try K<=4)",
                 0x1b: "arp playmode",
                 0x1c: "dst trg overflow (K too large for this track's layout; try K<=4)"}
        print(f"  reason: {codes.get(st, 'unknown')}")


def main() -> None:
    cmd = sys.argv[1] if len(sys.argv) > 1 else "ring"
    b = Board()
    if cmd == "setup":
        setup_forward(b)
    elif cmd == "setup-gen":
        setup_gen(b)
    elif cmd == "ring":
        ring(b)
    elif cmd == "grab":
        k = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        dst = int(sys.argv[3]) if len(sys.argv) > 3 else 1
        grab(b, k, dst)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
