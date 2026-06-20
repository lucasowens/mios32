"""CMD_CLOCK_STEP (CAPTURE bundle, 2026-06-19) — the synchronous clock-step driver.

The per-track-RNG keystone shipped with a documented HIL gap: there was no
clock-step harness verb to advance playback through the NextStep path, so the
random-traversal trajectory (and SCRUB save/restore) could only be validated by
ear. CMD_CLOCK_STEP closes that gap: with the transport STOPPED it drives N
bpm-ticks of SEQ_CORE_Tick directly (SEQ_CORE_Handler no-ops Tick while stopped,
so there is no concurrent +4 driver to race), advancing the step pointer,
random-traversal stream, generator wander and the per-bar ref_step==0 hook
exactly like a live clock. It is also the re-sim driver the retroactive CAPTURE
build is built on.

Robustness note for the traversal pins: Random_Step sets
`t->step = GenRangeXorshift(&random_traverse_state, loop, length)` — the next
step is drawn ENTIRELY from the per-track seed stream, independent of the current
step (seq_core.c, SEQ_CORE_NextStep). So after `traverse_seed_set(S)` the ORDER
of drawn steps is a pure function of S; the tick phase only affects WHEN each
draw fires, not its value or order. Collecting the trajectory on value-change is
therefore phase-independent: two runs with the same seed produce the identical
ordered sequence even though SEQ_BPM_Stop does not reset the tick phase.
"""

import pytest

from harness import Board

# Mirror seq_cc.h / seq_core.h — keep in sync with the firmware.
SEQ_CC_DIRECTION = 0x48
TRKDIR_RANDOM_STEP = 5

TRACK = 0

SEED_A = 0x1234_5678
SEED_B = 0x0BAD_F00D


_MEASURE_TICKS = 16 * 96  # steps_per_measure(16) * ticks_per_16th(96), step_length 96


def _traversal_walk_seed(board: Board, seed: int, measures: int = 2) -> int:
    """Drive the engine a whole number of measures off a known traversal seed and
    return the resulting traversal-stream state. Phase-INDEPENDENT by construction:
    a window of an integer number of step-periods crosses exactly `measures*16`
    step boundaries regardless of where the phase starts, so the seed advances by a
    fixed number of xorshift draws → a deterministic function of the input seed.
    (`track_drum_init` pins step_length=96; a priming drive clears FIRST_CLK so the
    first measured tick really advances; the seed is set AFTER priming.)"""
    board.track_drum_init(TRACK)                          # known geometry, step_length 96
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_RANDOM_STEP)
    board.clock_step(96)                                  # prime: clear FIRST_CLK
    board.traverse_seed_set(TRACK, seed)                  # reset the stream AFTER priming
    board.clock_step(measures * _MEASURE_TICKS)
    return board.traverse_seed_get(TRACK)


@pytest.mark.hardware
def test_clock_step_advances_tick_exactly(board):
    """The verb must advance the BPM tick by exactly N per call (deterministic
    tick arithmetic) and only while the transport is stopped."""
    board.reset()
    assert not board.tick_query()["running"], "transport should be stopped after reset"

    before = board.tick_query()["bpm_tick"]
    r1 = board.clock_step(50)
    assert r1["bpm_tick"] == before + 50, (
        f"clock_step(50) -> {r1['bpm_tick']}, expected {before + 50}"
    )
    r2 = board.clock_step(50)
    assert r2["bpm_tick"] == before + 100, (
        f"second clock_step(50) -> {r2['bpm_tick']}, expected {before + 100}"
    )


@pytest.mark.hardware
def test_clock_step_traversal_resimulates_from_seed(board):
    """THE keystone-gap closer: the random-traversal stream re-simulates exactly
    from a captured seed. Same seed + same whole-measure drive -> same resulting
    traversal state."""
    board.reset()
    a = _traversal_walk_seed(board, SEED_A)
    board.reset()
    b = _traversal_walk_seed(board, SEED_A)
    assert a == b, (
        f"random-traversal did not re-simulate identically from the same seed: "
        f"{a:#010x} vs {b:#010x}"
    )


@pytest.mark.hardware
def test_clock_step_traversal_seed_matters(board):
    """Different traversal seeds must drive the walk to different states — the seed
    is a real determinant, not ignored."""
    board.reset()
    a = _traversal_walk_seed(board, SEED_A)
    board.reset()
    b = _traversal_walk_seed(board, SEED_B)
    assert a != b, f"different seeds produced the same traversal state: {a:#010x}"
