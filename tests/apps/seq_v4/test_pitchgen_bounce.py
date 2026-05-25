"""Phase F.2: PITCHGEN BOUNCE dispatch — cursor-aware destination semantic.

Pins the routing that surfaced from the listen-test:

  1. Cursor IS the engaged gen → in-place bounce (slot freed, state drops
     to '--', distinct from DISENGAGE which leaves state='disengaged').
  2. Cursor on empty drum + gen engaged elsewhere on track → relocate
     (row-1 hint switches from 'GEN on Dnn  GP8 relocates here' back to
     the default engage prompt; the gen slot is freed).
  3. Undo stale (after UNDO+re-ENGAGE, the undo snapshot wasn't refreshed)
     → relocate refuses with -3; the engaged gen on the original drum is
     untouched.

Verification is LCD-scrape only. The behavioral contract "src restored,
dst plays the loop" is audible-by-ear and stays a listen-test; what these
tests pin is the *dispatch* layer that decides which BOUNCE flavor runs.
A silent regression in the dispatch would route the wrong way — these
tests catch that.

Track 0 is reinitialized to drum mode in each test via the testctrl
CMD_TRACK_DRUM_INIT (1 par-layer of type Note × 16 drum instruments × 64
steps). No pre-saved drum-mode pattern is needed.
"""

import time

import pytest

from harness import Board, Button, Page


PITCHGEN_TRACK = 0
GP1_ENGAGE = Button.GP(1)
GP2_UNDO = Button.GP(2)
GP8_BOUNCE = Button.GP(8)

# SEQ_UI_Msg popups overlay the page LCD until their timeout expires, so
# the LCD-scrape assertions only see the page's own content after the
# popup clears. Every button press in this file pops a message — sleep
# through it before reading the LCD. The trailing 0.15s is the page-redraw
# settle once the popup has cleared.
SETTLE = 0.15
ENGAGE_MSG_MS = 750    # "Pitch Gen: ENGAGED" / "DISENGAGED" / "UNDO restored"
BOUNCE_MSG_MS = 1000   # "BOUNCED in place"
RELOCATE_MSG_MS = 1200 # "BOUNCED -> drum slot"
REFUSE_MSG_MS = 2200   # "undo stale - reENGAGE"


def _setup_pitchgen(board: Board) -> None:
    """Reinit track 0 to drum mode, open PITCHGEN, park cursor on drum 0."""
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)


def _lcd_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _wait_msg_clear(ms: int) -> None:
    """Sleep through a SEQ_UI_Msg popup so the next LCD read sees the page,
    not the message overlay."""
    time.sleep((ms / 1000.0) + SETTLE)


def _press_engage(board: Board) -> None:
    """GP1 ENGAGE + wait through the 750 ms popup."""
    board.press(GP1_ENGAGE)
    _wait_msg_clear(ENGAGE_MSG_MS)


def _press_undo(board: Board) -> None:
    """GP2 UNDO + wait through the 750 ms popup."""
    board.press(GP2_UNDO)
    _wait_msg_clear(ENGAGE_MSG_MS)


@pytest.mark.hardware
def test_bounce_in_place_frees_slot(board):
    """GP8 with cursor on the engaged gen frees the pool slot.

    After SEQ_GENERATOR_Bounce, SEQ_GENERATOR_Get returns NULL, so the LCD
    state field drops to '--' (the "no slot allocated" branch in
    seq_ui_trkpitchgen.c's LCD_Handler). This is observably distinct from
    GP1 DISENGAGE, which leaves in_use=1 and reports 'disengaged'.
    """
    _setup_pitchgen(board)

    _press_engage(board)
    assert "state:ENGAGED" in _lcd_text(board), "GP1 should have engaged"

    board.press(GP8_BOUNCE)
    _wait_msg_clear(BOUNCE_MSG_MS)
    text = _lcd_text(board)
    assert "state:--" in text, (
        f"after in-place BOUNCE the slot should be freed (state ':--', "
        f"not 'disengaged' or 'ENGAGED'):\n{text}"
    )


@pytest.mark.hardware
def test_bounce_relocate_clears_other_engaged_marker(board):
    """GP8 with cursor on empty drum + gen engaged elsewhere → relocate.

    The row-1 hint is the visible witness:
      - while cursor is parked on empty D5 with a gen still engaged on D0:
        'GEN on D 1  GP8 relocates here'
      - after BOUNCE freed the slot: no engaged gen anywhere on the track,
        the hint falls back to 'press GP1 to ENGAGE   GP8=bounce proc'.
    """
    _setup_pitchgen(board)

    _press_engage(board)
    assert "state:ENGAGED" in _lcd_text(board)

    board.ui_instrument_set(5)
    time.sleep(SETTLE)
    text = _lcd_text(board)
    assert "GEN on D" in text, (
        f"cursor on empty drum w/ gen engaged elsewhere should show the "
        f"'GEN on Dnn' relocate hint:\n{text}"
    )

    board.press(GP8_BOUNCE)
    _wait_msg_clear(RELOCATE_MSG_MS)
    text = _lcd_text(board)
    assert "GEN on D" not in text, (
        f"after relocate, the slot is freed; row-1 hint should no longer "
        f"mention 'GEN on D':\n{text}"
    )
    assert "press GP1 to ENGAGE" in text, (
        f"after relocate, the row-1 hint should default back to the engage "
        f"prompt:\n{text}"
    )


@pytest.mark.hardware
def test_relocate_refuses_when_undo_stale(board):
    """UNDO consumes the snapshot; re-ENGAGE does NOT refresh it because
    the slot already exists in the pool (re-engage path does not snapshot
    undo). With undo invalid, BOUNCE-relocate must refuse with -3
    ('undo stale - reENGAGE') and leave the gen untouched.
    """
    _setup_pitchgen(board)

    _press_engage(board)
    _press_undo(board)               # restores par, disengages, invalidates undo
    _press_engage(board)             # re-engages, but does NOT re-snapshot undo
    assert "state:ENGAGED" in _lcd_text(board), (
        "re-ENGAGE after UNDO should still show ENGAGED state"
    )

    board.ui_instrument_set(5)
    time.sleep(SETTLE)
    board.press(GP8_BOUNCE)
    _wait_msg_clear(REFUSE_MSG_MS)

    # The relocate was refused, so the engaged gen should still be there.
    # Move the cursor back to D0 and confirm.
    board.ui_instrument_set(0)
    time.sleep(SETTLE)
    assert "state:ENGAGED" in _lcd_text(board), (
        "stale-undo refusal should leave the original gen engaged"
    )
