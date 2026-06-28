"""The track-hold PULL gesture (RECOMBINE Stage B) — the mirror of the
PATTERN-hold push.

Hold a select-row track button = the pull DESTINATION (transfusion target).
While held: another select-row press picks the SOURCE column (bank = col/4,
section = col%4; default = the held track's own column), GP1-8 picks the
source pattern letter, GP9-16 picks the number and COMMITS the pull
(SEQ_CORE_LoadTrackFromSlot: bar-aligned, track undo armed). Releasing the
held button still performs the stock track select, so the cursor follows the
transfusion target. A bare tap stays a plain track select.

Destructive: overwrites (0, 61) on SD and rewrites tracks in groups 0/1.
Same scratch-slot conventions as test_track_load.py.
"""

import time

import pytest

from harness import Board, Button, CC
from harness.sysex import RESET_UNMUTE_ALL, JRNL_UNDOABLE, JRNL_REDOABLE


SETTLE = 0.15

SCRATCH_BANK = 0
SCRATCH_PATTERN = 61   # = letter H (GP8) + number 6 (GP14): (7<<3)|5
LETTER_GP = 8
NUMBER_GP = 14

SEED_T0 = {0: 60, 1: 62, 7: 67, 17: 72, 31: 48, 63: 55}
SEED_T1 = {0: 36, 2: 38, 5: 43, 11: 51, 30: 46, 62: 49}
LENGTH_T0 = 7
LENGTH_T1 = 15

EVENT_MODE_DRUM = 3


def _seed_drum(board: Board, track: int, seed: dict[int, int]) -> None:
    for step, note in seed.items():
        board.track_drum_par_set(track, 0, step, note)


def _build_scratch_slot(board: Board) -> None:
    for track, seed, length in ((0, SEED_T0, LENGTH_T0), (1, SEED_T1, LENGTH_T1)):
        board.track_drum_init(track)
        _seed_drum(board, track, seed)
        board.cc_set(track, CC.LENGTH, length)
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_PATTERN)


def _pull_gesture(board: Board, hold_track: int, src_col: int | None,
                  letter_gp: int, number_gp: int) -> None:
    """Hold select-row [hold_track], optionally pick a source column, aim
    letter + number (commit), release."""
    board.button(Button.DIRECT_TRACK(hold_track + 1), depressed=False)  # hold
    try:
        if src_col is not None:
            board.press(Button.DIRECT_TRACK(src_col + 1))  # source column
        board.press(Button.GP(letter_gp))                  # pattern letter
        # number -> COMMIT: the reply is delayed by the synchronous SD read
        # inside the button handler, so press with an explicit long timeout
        board.button(Button.GP(number_gp), depressed=False, timeout=4.0)
        time.sleep(0.02)
        board.button(Button.GP(number_gp), depressed=True, timeout=4.0)
    finally:
        board.button(Button.DIRECT_TRACK(hold_track + 1), depressed=True)  # release


@pytest.mark.hardware
def test_pull_gesture_cross_column(board):
    """Hold T7 (track 6), pick column 1 as source, aim H6 (= scratch slot 61):
    section 1 lands on track 6, the undo is armed, the cursor follows the
    held (destination) track."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)

    _pull_gesture(board, hold_track=6, src_col=1,
                  letter_gp=LETTER_GP, number_gp=NUMBER_GP)
    time.sleep(SETTLE)

    assert board.cc_get(6, CC.EVENT_MODE) == EVENT_MODE_DRUM, (
        "pulled section's event mode must arrive on the held track"
    )
    assert board.cc_get(6, CC.LENGTH) == LENGTH_T1
    for step, note in SEED_T1.items():
        v = board.track_drum_par_get(6, 0, step)
        assert v == note, (
            f"track 6 step {step}: expected section-1 byte {note}, got {v}"
        )

    # Middle field is now the unified journal state (UNDOABLE after a pull).
    valid, state, track, _ = board.track_undo_query()
    assert (valid, state, track) == (True, JRNL_UNDOABLE, 6), (
        "the gesture commit must arm the journal (UNDOABLE) for the held track"
    )
    # Stock release-select fired: the cursor followed the transfusion target.
    assert board.ui_track_get() == 6, (
        "releasing the held button should still select it (cursor follows)"
    )


@pytest.mark.hardware
def test_pull_gesture_default_column(board):
    """No column pick: the source column defaults to the held track's own
    (hold track 1 = column 1 -> section 1 of the scratch slot)."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)
    # Distinct victim on track 1 so the pull is observable.
    board.track_drum_init(1)
    _seed_drum(board, 1, {0: 96, 3: 99})

    _pull_gesture(board, hold_track=1, src_col=None,
                  letter_gp=LETTER_GP, number_gp=NUMBER_GP)
    time.sleep(SETTLE)

    assert board.cc_get(1, CC.LENGTH) == LENGTH_T1
    assert board.track_drum_par_get(1, 0, 0) == SEED_T1[0], (
        "default source column must be the held track's own column"
    )
    # One gesture back: the victim returns.
    assert board.track_undo() == 1
    time.sleep(SETTLE)
    assert board.track_drum_par_get(1, 0, 0) == 96


@pytest.mark.hardware
def test_pull_gesture_select_clear_undo(board):
    """SELECT+CLEAR restores the pull victim — the one-gesture-back of the
    by-ear loop (CLEAR destroys; SELECT+CLEAR un-destroys). The midiphy panel
    has no UNDO button, so this is the pull's performance-surface undo. With
    nothing armed it must NOT fall through to a destructive clear."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)
    board.track_drum_init(6)
    _seed_drum(board, 6, {0: 96, 3: 99})

    _pull_gesture(board, hold_track=6, src_col=1,
                  letter_gp=LETTER_GP, number_gp=NUMBER_GP)
    time.sleep(SETTLE)
    assert board.track_drum_par_get(6, 0, 0) == SEED_T1[0]

    board.button(Button.SELECT, depressed=False)
    try:
        board.press(Button.CLEAR)
    finally:
        board.button(Button.SELECT, depressed=True)
    time.sleep(SETTLE)
    assert board.track_drum_par_get(6, 0, 0) == 96, (
        "SELECT+CLEAR must restore the pull victim (undo)"
    )
    # The unified journal makes SELECT+CLEAR a TOGGLE: undo leaves it REDOABLE,
    # not consumed (the 2026-06-23 net added the missing redo).
    valid, state, _, _ = board.track_undo_query()
    assert not valid, "after the undo the slot is no longer UNDOABLE"
    assert state == JRNL_REDOABLE, "undo must leave the gesture REDOABLE"

    # Press again: SELECT+CLEAR now REDOES the pull (re-applies the victim's
    # replacement). It NEVER falls through to a destructive clear.
    board.button(Button.SELECT, depressed=False)
    try:
        board.press(Button.CLEAR)
    finally:
        board.button(Button.SELECT, depressed=True)
    time.sleep(SETTLE)
    assert board.track_drum_par_get(6, 0, 0) == SEED_T1[0], (
        "second SELECT+CLEAR must REDO the pull (and never clear the track)"
    )


@pytest.mark.hardware
def test_pull_gesture_bare_tap_keeps_select(board):
    """A bare select-row tap stays a plain track select — no pull, no undo arm,
    no content change."""
    board.reset(RESET_UNMUTE_ALL)
    before_len = board.cc_get(3, CC.LENGTH)
    undo_before = board.track_undo_query()

    board.press(Button.DIRECT_TRACK(4))  # plain tap on track 4's button
    time.sleep(0.1)

    assert board.ui_track_get() == 3, "bare tap must still select the track"
    assert board.cc_get(3, CC.LENGTH) == before_len
    assert board.track_undo_query() == undo_before, (
        "a bare tap must not arm or consume the track undo"
    )


@pytest.mark.hardware
def test_pull_arm_blocked_in_phrase_view(board):
    """Phantom-pull guard (2026-06-14 hardening). The RECOMBINE pull must arm and
    commit ONLY in TRACKS sel-view. In PHRASE view the select row is phrase
    waypoints, so a held waypoint (e.g. mid capture-hold) followed by a GP press
    must NOT fire a pull. Pre-fix, the arm ran regardless of view and a GP press
    during the hold committed a real SEQ_CORE_LoadTrackFromSlot into the held
    track — a silent destructive overwrite from ordinary phrase use.

    A real pull ALWAYS arms the track-undo (a phrase recall never does), so the
    undo slot is the view-independent detector. Slot 6 is left un-captured, so the
    phrase gesture degrades to a harmless refused recall (never a capture)."""
    board.reset(RESET_UNMUTE_ALL)

    # Known victim content + length marker on track 6.
    board.track_drum_init(6)
    board.track_drum_par_set(6, 0, 0, 96)
    board.cc_set(6, CC.LENGTH, 11)

    # Enter PHRASE sel-view (the PHRASE button sets it; it sticks). NB: SONG is now the
    # unified Capture page (sel-view TRACKS) since 2026-06-28 — PHRASE is the phrase view.
    board.press(Button.PHRASE)
    time.sleep(0.1)

    undo_before = board.track_undo_query()
    len_before = board.cc_get(6, CC.LENGTH)

    # The phantom-commit attempt: hold phrase-6 waypoint, press a GP number, release
    # promptly (a tap => refused recall of the empty slot, never a 1s capture-hold).
    board.button(Button.DIRECT_TRACK(7), depressed=False)   # hold waypoint 6
    try:
        board.press(Button.GP(14))                          # would-be pull commit
    finally:
        board.button(Button.DIRECT_TRACK(7), depressed=True)  # release
    time.sleep(SETTLE)

    assert board.track_undo_query() == undo_before, (
        "a GP press while a PHRASE-view waypoint is held must NOT arm a pull "
        "(pull is TRACKS-view only)"
    )
    assert board.cc_get(6, CC.LENGTH) == len_before, "held track must be untouched"
    assert board.track_drum_par_get(6, 0, 0) == 96, "held track content must be untouched"
    assert not board.phrase_present(6), "the empty waypoint must not have been captured"
