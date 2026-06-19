"""SWITCH-QUANTIZE — global switch-quantize grid + auto-measured forward-delay
margin (feature B).

The grid (CMD_SWITCH_QUANTIZE) selects the musical boundary a deferred pattern
switch lands on: 0=Instant, 1=1/16, 2=1/8, 3=1/4(beat), 4=1/2 bar, 5=1 bar
(default), 6=2 bar, 7=4 bar, 8=8 bar. grid>0 implies synched switching; grid 0 =
immediate. The actual bar-landing FEEL (and the auto-measured margin tightening)
is a by-ear judgement — these pins cover the value plumbing, the clamp, and that
the engine holds the selected grid.
"""

import pytest

from harness import Board

GRID_DEFAULT = 0  # Instant (the firmware default; old pattern-boundary feel when synched)


@pytest.mark.hardware
def test_switch_quantize_roundtrip(board):
    """Every ladder index 0..8 round-trips through set -> get."""
    try:
        for g in range(9):
            got = board.switch_quantize_set(g)
            assert got == g, f"set grid {g} returned {got}"
            grid, _measured = board.switch_quantize_get()
            assert grid == g, f"get after set {g} returned {grid}"
    finally:
        board.switch_quantize_set(GRID_DEFAULT)  # leave the board at the default


@pytest.mark.hardware
def test_switch_quantize_clamps_high(board):
    """Out-of-range grid clamps to 8 (8 bars), never wraps."""
    try:
        assert board.switch_quantize_set(9) == 8, "grid 9 should clamp to 8"
        assert board.switch_quantize_set(127) == 8, "grid 127 should clamp to 8"
        grid, _ = board.switch_quantize_get()
        assert grid == 8, f"clamped grid should read back 8, got {grid}"
    finally:
        board.switch_quantize_set(GRID_DEFAULT)  # leave the board at the default
