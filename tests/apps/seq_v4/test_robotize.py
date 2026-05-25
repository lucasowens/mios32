"""Robotize tests: drive the FX_ROBOTIZE page and verify the live note
stream gets modified.

Robotize layout (seq_ui_fx_robotize.c):
  GP1  = GXTY (track select)
  GP2  = ACTIVE          (encoder: 0/1)
  GP3  = PROBABILITY     (encoder: 0..31 — chance per step that robotize fires)
  GP4  = SKIP value      (0..31 — conditional chance the note is skipped)
  GP5  = OCTAVE value
  GP6  = NOTE value
  GP7  = VELOCITY value
  GP8  = LENGTH value
  GP9  = SUSTAIN value
  GP10 = NOFX value
  GP11 = ECHO value
  GP12 = DUPLICATE value

The ROBOTIZE_MASK1/MASK2 CCs form a 16-bit step mask — each bit = "robotize is
armed on this step". Mask bits are toggled by SELECT+GPn chords, not plain
button presses. To get robotize firing on every step, hold SELECT and press
GP1..GP16 (any bit that's already on stays on under XOR, so we always
re-arm them all to a known state).
"""

import time

import pytest

from harness import Button, Encoder, MidiPort, Page
from harness.sysex import CC

PLAY_SECONDS = 2.0


def _setup_all_triggers_on_track0(board):
    """Configure track 0 to USB1 and arm every step via Euclidean(16, 16)."""
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
    board.page_set(Page.TRKEUCLID)
    time.sleep(0.1)
    # Force generator type to EUCLID (0) regardless of previous state.
    board.turn(Encoder.GP(12), -10)
    # Length 16 (display), pulses 16 (every step triggers).
    board.turn(Encoder.GP(9), -99)
    board.turn(Encoder.GP(10), -99)
    board.turn(Encoder.GP(9), +15)   # length = 16
    board.turn(Encoder.GP(10), +16)  # pulses = 16 (== length, every step on)
    time.sleep(0.15)


def _play_and_capture(board, seconds: float = PLAY_SECONDS) -> list:
    """PLAY for `seconds`, STOP, return list of NoteOn events on track 0's channel."""
    capture_t0 = board.capture_start()
    board.press(Button.PLAY)
    try:
        time.sleep(seconds)
    finally:
        board.press(Button.STOP)
        time.sleep(0.1)
    return [
        e for e in board.capture_notes(since=capture_t0)
        if e.is_on and e.channel == 0
    ]


def _play_and_count(board, seconds: float = PLAY_SECONDS) -> int:
    return len(_play_and_capture(board, seconds))


def _disable_robotize(board):
    """Undo robotize state. The board fixture's reset clears MASK1/MASK2 + ACTIVE
    on every test, but during the test itself we still want to disable so the
    user-visible board state matches what's expected if they walk away mid-test.
    """
    board.page_set(Page.FX_ROBOTIZE)
    time.sleep(0.05)
    board.encoder(Encoder.GP(2), -1)
    board.turn(Encoder.GP(3), -32)
    board.turn(Encoder.GP(4), -32)
    board.turn(Encoder.GP(7), -32)
    time.sleep(0.05)


def _arm_all_robotize_steps(board):
    """Set MASK1/MASK2 directly so every step is armed regardless of prior state.

    The user-facing equivalent (hold SELECT, press GP1..GP16) XOR-toggles the
    bits, which only lands at "all-on" if the masks started at 0. Patterns
    loaded from disk often have masks set from prior edits, so we drive the
    CCs directly. Two SysEx round-trips beats the SELECT+16x button dance for
    speed too.
    """
    board.cc_set(track=0, cc=CC.ROBOTIZE_MASK1, value=0xFF)
    board.cc_set(track=0, cc=CC.ROBOTIZE_MASK2, value=0xFF)


@pytest.mark.hardware
def test_robotize_skip_reduces_note_count(board):
    """Enabling robotize SKIP at high probability should drop most notes."""
    _setup_all_triggers_on_track0(board)
    baseline = _play_and_count(board)
    assert baseline >= 8, (
        f"baseline (every-step pattern) only produced {baseline} notes; "
        f"setup is broken before we even enable robotize"
    )

    board.page_set(Page.FX_ROBOTIZE)
    time.sleep(0.1)
    try:
        board.encoder(Encoder.GP(2), +1)   # ACTIVE = 1
        board.turn(Encoder.GP(3), +32)     # PROBABILITY = max (clamped to 31)
        board.turn(Encoder.GP(4), +32)     # SKIP value = max
        _arm_all_robotize_steps(board)     # MASK1=0xFF, MASK2=0xFF (all 16 steps)

        robotized = _play_and_count(board)

        # With every step armed and SKIP+PROBABILITY both maxed, robotize
        # should skip the large majority of notes (>50% drop).
        assert robotized < baseline * 0.5, (
            f"robotize SKIP didn't meaningfully reduce note count: "
            f"baseline={baseline}, robotized={robotized}\n"
        )
    finally:
        _disable_robotize(board)


@pytest.mark.hardware
def test_robotize_velocity_introduces_variation(board):
    """With velocity randomization enabled, captured velocities should vary.

    A track without robotize plays every note at its fixed param-layer velocity.
    With robotize VELOCITY at high probability, the velocity field jitters per
    note, so we should observe a wider set of distinct velocity values.
    """
    _setup_all_triggers_on_track0(board)
    baseline_notes = _play_and_capture(board)
    baseline_vels = {e.velocity for e in baseline_notes}
    assert len(baseline_notes) >= 8, (
        f"baseline only produced {len(baseline_notes)} notes; setup is broken"
    )

    board.page_set(Page.FX_ROBOTIZE)
    time.sleep(0.1)
    try:
        board.encoder(Encoder.GP(2), +1)   # ACTIVE = 1
        board.turn(Encoder.GP(3), +32)     # PROBABILITY = max
        board.turn(Encoder.GP(7), +32)     # VELOCITY variation = max
        _arm_all_robotize_steps(board)

        robotized_notes = _play_and_capture(board)
        robotized_vels = {e.velocity for e in robotized_notes}

        assert len(robotized_vels) > len(baseline_vels), (
            f"robotize VELOCITY didn't introduce variation: "
            f"baseline distinct velocities={sorted(baseline_vels)}, "
            f"robotized distinct={sorted(robotized_vels)}"
        )
    finally:
        _disable_robotize(board)
