"""Encoder behavior tests."""

import re
import time

import pytest

from harness import Button, Encoder


@pytest.mark.hardware
def test_reset_returns_to_edit_page(board):
    """RESET_STATE returns the device to the EDIT page even if we navigated away."""
    # Hop to MENU page so reset has work to do.
    board.button(Button.MENU, depressed=False)
    try:
        # While MENU is held, the screen is the page-jump grid.
        held = board.lcd_snapshot().text
    finally:
        board.button(Button.MENU, depressed=True)

    board.reset()
    time.sleep(0.1)

    edit = board.lcd_snapshot()
    # The edit page starts with "G<n>T<n>" (group/track).
    assert re.match(r"G\dT\d", edit.line(0)), (
        f"reset didn't return to EDIT page; line0={edit.line(0)!r}"
    )


@pytest.mark.hardware
def test_gp_encoder_selects_corresponding_step(board):
    """On the EDIT page, GPn selects step n (then increments that step's value).

    The Encoder_Handler in seq_ui_edit.c sets `ui_selected_step = encoder` on
    every GP encoder turn. So turning GP5 should make 'Step  5' appear on the
    LCD, regardless of where the cursor was before.
    """
    # Touch GP1 first so we're guaranteed to be at step 1 — then verify GP5 jumps to 5.
    board.encoder(Encoder.GP(1), +1)
    time.sleep(0.05)
    before = board.lcd_snapshot()
    step_before = int(re.search(r"Step\s*(\d+)", before.text).group(1))
    assert step_before == 1, f"expected step 1 after GP1 turn, got {step_before}"

    board.encoder(Encoder.GP(5), +1)
    time.sleep(0.05)
    after = board.lcd_snapshot()
    step_after = int(re.search(r"Step\s*(\d+)", after.text).group(1))

    assert step_after == 5, (
        f"expected step 5 after GP5 turn, got {step_after}.\n"
        f"line0: {after.line(0)!r}"
    )


@pytest.mark.hardware
def test_datawheel_changes_step_value(board):
    """Turning the datawheel modifies the currently-selected step's value.

    On the EDIT page with parameter layer = Note, the datawheel raises/lowers
    the note of the selected step. The note appears between 'Step  N' and 'Vel:'.
    """
    before = board.lcd_snapshot()
    note_pattern = re.compile(r"Step\s*\d+\s+([A-G][-#]\d)")
    match = note_pattern.search(before.text)
    assert match, f"EDIT page didn't show a step note; line0={before.line(0)!r}"
    note_before = match.group(1)

    board.encoder(Encoder.DATAWHEEL, +1)
    time.sleep(0.1)
    after = board.lcd_snapshot()

    # Restore so we don't leave the pattern modified.
    board.encoder(Encoder.DATAWHEEL, -1)
    time.sleep(0.05)

    note_after = note_pattern.search(after.text).group(1)
    assert note_after != note_before, (
        f"Datawheel didn't change the selected note (still {note_before!r}).\n"
        f"before line0: {before.line(0)!r}\nafter  line0: {after.line(0)!r}"
    )


@pytest.mark.hardware
def test_encoder_out_of_range_raises(board):
    """An encoder index outside 0..17 is rejected by the firmware."""
    with pytest.raises(ValueError):
        board.encoder(99, +1)
