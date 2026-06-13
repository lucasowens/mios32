"""FREEZE — the generator-mutation master switch (seq_core_state.FREEZE), on the
repurposed METRONOME button.

While FREEZE is engaged, SEQ_GENERATOR_Tick skips the per-measure auto-mutate so
every engaged generator loop holds static (the design's frozen organism);
releasing FREEZE lets them evolve again. This is reversible and global.

Tested through the REAL playback wrap path — PLAY, let the track wrap a measure,
STOP — because that's where the gate lives. generator_tick_force calls
mutate_loop directly and bypasses the gate, so it can't substitute here.

Two-face phrase recall is an emergent consequence (recall while frozen lands the
organism as static tape); it needs no separate code and is covered by the
posture/freeze behaviour pinned here.
"""

import time

import pytest

from harness import Board, Button, Encoder, Page
from harness.sysex import DIAL_DEPTH, DIAL_RANGE_MAX, DIAL_RANGE_MIN, DIAL_RATE


PITCHGEN_TRACK = 0
GP1 = Button.GP(1)
SETTLE = 0.10
ENGAGE_MSG_MS = 750
# One bar at the default tempo runs in ~1.6s (the emission pins play that long);
# 2.5s guarantees at least one step→0 wrap, which is what fires the auto-mutate.
PLAY_ACROSS_WRAP_S = 2.5


def _engage_wide_mutator(board: Board) -> None:
    """Drum-mode track 0 + an ENGAGED generator dialed to mutate aggressively
    (rate=127, depth=127, 4-octave range) so a single measure wrap almost
    certainly moves the loop — makes the live-vs-frozen contrast unambiguous."""
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    time.sleep(SETTLE)
    board.press(GP1)  # ENGAGE
    time.sleep(ENGAGE_MSG_MS / 1000.0 + SETTLE)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MIN, 36)   # C-1
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MAX, 84)   # C-5
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_DEPTH, 127)


def _play_across_wrap(board: Board) -> None:
    board.press(Button.PLAY)
    time.sleep(PLAY_ACROSS_WRAP_S)
    board.press(Button.STOP)
    time.sleep(SETTLE)


@pytest.mark.hardware
def test_freeze_holds_engaged_loop_release_resumes(board):
    """The master mutation switch, end to end: live evolves, FROZEN holds,
    released evolves again — all through the real measure-wrap path."""
    _engage_wide_mutator(board)

    # 1) LIVE: a measure wrap should mutate the loop.
    board.freeze_set(False)
    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None
    _play_across_wrap(board)
    after_live = board.generator_query(PITCHGEN_TRACK, 0)
    assert after_live is not None
    assert after_live.loop != before.loop, (
        "live: an engaged rate=127/depth=127 loop should mutate across a "
        f"measure wrap.\n  before: {before.loop.hex()}\n  after:  {after_live.loop.hex()}"
    )

    # 2) FROZEN: a measure wrap must NOT mutate (loops hold).
    assert board.freeze_set(True) is True
    frozen_before = board.generator_query(PITCHGEN_TRACK, 0)
    assert frozen_before is not None
    _play_across_wrap(board)
    frozen_after = board.generator_query(PITCHGEN_TRACK, 0)
    assert frozen_after is not None
    assert frozen_after.loop == frozen_before.loop, (
        "frozen: the loop must hold across a measure wrap.\n"
        f"  before: {frozen_before.loop.hex()}\n  after:  {frozen_after.loop.hex()}"
    )

    # 3) RELEASE: mutation resumes (reversible — not a destructive clear).
    assert board.freeze_set(False) is False
    _play_across_wrap(board)
    resumed = board.generator_query(PITCHGEN_TRACK, 0)
    assert resumed is not None
    assert resumed.loop != frozen_after.loop, (
        "released: the loop should evolve again after FREEZE is lifted.\n"
        f"  frozen: {frozen_after.loop.hex()}\n  resumed: {resumed.loop.hex()}"
    )
