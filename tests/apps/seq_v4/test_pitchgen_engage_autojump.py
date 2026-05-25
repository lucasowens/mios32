"""Phase F.3: ENGAGE auto-jump — §3 "first empty legal layer" default
destination for the GP1 ENGAGE gesture on the PITCHGEN page.

Behavior under test (seq_ui_trkpitchgen.c, Button_Handler GP1 disengaged
branch):
  - Cursor on a drum that already has Note content AND has no allocated
    gen slot → jump cursor to the first empty legal drum, then engage.
    LCD popup reads "ENGAGED on D<N>" (vs the plain "ENGAGED" of the
    no-jump path).
  - Cursor on a drum that has content but no other drum is empty → no
    jump; engage in-place (the existing destructive path; UNDO is the
    live-safety net).
  - Cursor on an already-empty drum → no jump; engage in-place at the
    cursor exactly.

"Empty" = no engaged gen AND Note par-layer == 0 across all steps.
"""

import time

import pytest

from harness import Board, Button, Page


PITCHGEN_TRACK = 0
GP1_ENGAGE = Button.GP(1)

SETTLE = 0.15
ENGAGE_MSG_MS = 750
ENGAGE_JUMPED_MSG_MS = 1000


def _setup_pitchgen_empty(board: Board) -> None:
    """Reinit track 0 to an empty drum mode, open PITCHGEN, park cursor
    on drum 0."""
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)


def _seed_drum(board: Board, instr: int, note: int = 60) -> None:
    """Plant a single non-zero value into drum `instr` step 0 so the
    drum is no longer 'empty' for the auto-jump check."""
    board.track_drum_par_set(PITCHGEN_TRACK, instr, 0, note)


def _lcd_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _wait_msg_clear(ms: int) -> None:
    time.sleep((ms / 1000.0) + SETTLE)


@pytest.mark.hardware
def test_engage_jumps_when_cursor_drum_populated(board):
    """Cursor on D0 (populated), GP1 → jumps to D1 (first empty),
    engages there, popup names the destination drum."""
    _setup_pitchgen_empty(board)
    _seed_drum(board, 0)

    board.press(GP1_ENGAGE)
    # Read while the longer "ENGAGED on Dnn" popup is still showing —
    # that string IS the dispatch witness.
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "ENGAGED on D" in popup, (
        f"populated cursor should trigger the auto-jump popup; "
        f"LCD reads:\n{popup}"
    )

    _wait_msg_clear(ENGAGE_JUMPED_MSG_MS)

    # Cursor should now point at D2 (instr index 1, displayed as "D 2").
    # Confirm via the slot allocation, not just the LCD: query both D0
    # (must be unallocated — was seeded with note data, no gen) and D1
    # (must be engaged after the jump).
    g0 = board.generator_query(PITCHGEN_TRACK, 0)
    g1 = board.generator_query(PITCHGEN_TRACK, 1)
    assert g0 is None, (
        f"D0 was the populated cursor; auto-jump should NOT have allocated "
        f"a gen there. Got: {g0!r}"
    )
    assert g1 is not None and g1.engaged, (
        f"D1 was the first empty legal drum; auto-jump should have engaged "
        f"a gen there. Got: {g1!r}"
    )


@pytest.mark.hardware
def test_engage_in_place_when_cursor_drum_empty(board):
    """Cursor on D5 (empty), GP1 → no jump, engages in place at D5.
    Popup is the plain "ENGAGED" (no "on Dnn")."""
    _setup_pitchgen_empty(board)
    board.ui_instrument_set(5)
    time.sleep(SETTLE)

    board.press(GP1_ENGAGE)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "ENGAGED on D" not in popup, (
        f"empty cursor should NOT trigger the auto-jump popup; "
        f"LCD reads:\n{popup}"
    )
    assert "ENGAGED" in popup, (
        f"empty cursor + GP1 should still show the standard ENGAGED popup; "
        f"LCD reads:\n{popup}"
    )

    _wait_msg_clear(ENGAGE_MSG_MS)
    g5 = board.generator_query(PITCHGEN_TRACK, 5)
    assert g5 is not None and g5.engaged, (
        f"in-place engage should have allocated a gen at the cursor (D5). "
        f"Got: {g5!r}"
    )


@pytest.mark.hardware
def test_engage_in_place_when_all_drums_populated(board):
    """Every drum has content → FindFirstEmptyDrum returns 0 → fall
    through to in-place engage at the cursor (D5)."""
    _setup_pitchgen_empty(board)
    # Seed all 16 drums with a single note each, varying so we can prove
    # this code path didn't accidentally clear them.
    for instr in range(16):
        _seed_drum(board, instr, 60 + instr)

    board.ui_instrument_set(5)
    time.sleep(SETTLE)
    board.press(GP1_ENGAGE)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "ENGAGED on D" not in popup, (
        f"no empty target should NOT trigger the auto-jump popup; "
        f"LCD reads:\n{popup}"
    )
    assert "ENGAGED" in popup, (
        f"GP1 should still engage in-place when no empty target exists; "
        f"LCD reads:\n{popup}"
    )

    _wait_msg_clear(ENGAGE_MSG_MS)
    g5 = board.generator_query(PITCHGEN_TRACK, 5)
    assert g5 is not None and g5.engaged, (
        f"in-place engage should have allocated a gen at D5. Got: {g5!r}"
    )
