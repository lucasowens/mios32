"""PITCHGEN GP8 dispatch — in-place freeze only.

After the bounce unification, GP8 on the PITCHGEN page no longer guesses a
destination (the relocate / cross-track-capture auto-routing is gone — those
violated the no-smart-default rule). It freezes in place on the visible track:

  1. Cursor IS the engaged gen → SEQ_GENERATOR_Bounce (slot freed, state drops
     to '--', distinct from DISENGAGE which leaves state='disengaged').
  2. No gen at the cursor but the visible track has an enabled processor →
     SEQ_CORE_ProcessorBounce (output→source, stack cleared, playmode back to
     Normal). LCD popup "BOUNCED (proc)".
  3. Nothing applicable → "nothing to bounce" guidance.

Destination captures (→ pattern slot, → track) moved to the deliberate
PATTERN-hold gesture (test_capture_to_slot via the bounce() helper) and the
CaptureToTrack verb (test_capture_to_track) — GP8 is purely the in-place freeze.

Verification is LCD-scrape + CC readback. Track 0 is reinitialized to drum mode
via CMD_TRACK_DRUM_INIT in each test.
"""

import time

import pytest

from harness import Board, Button, CC, Page


PITCHGEN_TRACK = 0
GP1_ENGAGE = Button.GP(1)
GP8_BOUNCE = Button.GP(8)

# Mirror of seq_core_trk_playmode_t (seq_core.h): playmode CC = CC.MODE (0x40).
TRKMODE_NORMAL = 1
TRKMODE_CHORDMASK = 4

SETTLE = 0.15
ENGAGE_MSG_MS = 750     # "Pitch Gen: ENGAGED"
BOUNCE_MSG_MS = 1000    # "BOUNCED in place" / "BOUNCED (proc)"
NOTHING_MSG_MS = 1500   # "nothing to bounce"


def _setup_pitchgen(board: Board) -> None:
    """Reinit track 0 to drum mode, open PITCHGEN, park cursor on drum 0."""
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)


def _lcd_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _wait_msg_clear(ms: int) -> None:
    time.sleep((ms / 1000.0) + SETTLE)


def _enable_chordmask(board: Board, track: int, *, strength: int = 0) -> None:
    board.cc_set(track, CC.MODE, TRKMODE_CHORDMASK)
    board.cc_set(track, CC.CHORDMASK_STRENGTH, strength)
    time.sleep(0.05)


@pytest.mark.hardware
def test_bounce_in_place_frees_slot(board):
    """GP8 with cursor on the engaged gen frees the pool slot.

    After SEQ_GENERATOR_Bounce, SEQ_GENERATOR_Get returns NULL, so the LCD
    state field drops to '--' (the "no slot allocated" branch). This is
    observably distinct from GP1 DISENGAGE, which leaves in_use=1 ('disengaged').
    """
    _setup_pitchgen(board)

    board.press(GP1_ENGAGE)
    _wait_msg_clear(ENGAGE_MSG_MS)
    assert "state:ENGAGED" in _lcd_text(board), "GP1 should have engaged"

    board.press(GP8_BOUNCE)
    _wait_msg_clear(BOUNCE_MSG_MS)
    text = _lcd_text(board)
    assert "state:--" in text, (
        f"after in-place BOUNCE the slot should be freed (state ':--', "
        f"not 'disengaged' or 'ENGAGED'):\n{text}"
    )


@pytest.mark.hardware
def test_bounce_processor_freezes_in_place(board):
    """GP8 on a track with an enabled processor (no gen at the cursor) →
    SEQ_CORE_ProcessorBounce: 'BOUNCED (proc)' popup, playmode back to Normal
    (the chord_mask tcc mirror is untangled so it doesn't re-arm)."""
    _setup_pitchgen(board)
    _enable_chordmask(board, PITCHGEN_TRACK, strength=0)
    assert board.cc_get(PITCHGEN_TRACK, CC.MODE) == TRKMODE_CHORDMASK

    board.press(GP8_BOUNCE)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "BOUNCED (proc)" in popup, (
        f"GP8 with an enabled processor should freeze it in place "
        f"('BOUNCED (proc)'); LCD reads:\n{popup}"
    )

    _wait_msg_clear(BOUNCE_MSG_MS)
    assert board.cc_get(PITCHGEN_TRACK, CC.MODE) == TRKMODE_NORMAL, (
        "after processor bounce the chord_mask playmode mirror should be "
        "reset to Normal so the next SlotSync doesn't re-arm it"
    )


@pytest.mark.hardware
def test_nothing_to_bounce_guidance(board):
    """GP8 on an empty track (no engaged gen, no enabled processor) → the
    'nothing to bounce' guidance, and no playmode change."""
    _setup_pitchgen(board)

    board.press(GP8_BOUNCE)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "nothing to bounce" in popup, (
        f"GP8 on an empty track should show 'nothing to bounce'; "
        f"LCD reads:\n{popup}"
    )
    _wait_msg_clear(NOTHING_MSG_MS)
