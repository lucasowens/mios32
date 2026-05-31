"""PATTERN-hold capture gesture — end-to-end button-driven coverage.

These drive the actual gesture via injected buttons (the select row / DirectTrack
is injectable), covering what the engine tests don't.

Model (locked): the gesture is "capture-only everywhere" — hold PATTERN, pick a
destination track on the select row (default = the source's own track index),
pick group (GP1-8) + pattern (GP9-16); the pattern press commits
SEQ_CORE_CaptureToSlotTrack. It NEVER auto-loads and NEVER jumps: a static copy
lands in the chosen slot on SD, every group's live RAM is left untouched, so
whatever is playing keeps playing. You navigate to the slot deliberately to hear
the capture.

So the behaviors pinned here are:
  - the capture lands in the SLOT YOU PICKED and PERSISTS there: load that slot
    (into any group) and the captured track is present and plays; it survives
    switching the group away and back (no manual save).
  - NO jump: neither the destination group nor the source group's live RAM is
    replaced by the capture (cross-group and same-group).
  - the button_state regression: a select-press straddling the PATTERN-hold
    boundary must not corrupt the select row's track-select afterward.

Source is a drum track so we can read it back with track_drum_par_get.
"""

import time

import pytest

from harness import Board, Button, Page
from harness.sysex import RESET_UNMUTE_ALL


SRC_TRACK = 0          # G1T1 (group 0)
DST_XGROUP = 4         # G2T1 (group 1) — a cross-group destination, slot position 0
DST_SAMEGROUP = 2      # G1T3 (group 0) — a same-group destination, slot position 2
BYSTANDER = 1          # G1T2 (group 0) — a source group-mate we never bounce
SETTLE = 0.2

# GP8 (group H) + GP16 (pattern 8) -> pattern 63 (the scratch slot the other
# bounce tests use): num=7, group=7 -> 7 | 7<<3 = 63.
GROUP_GP = 8
PATTERN_GP = 16
SLOT_BANK = 0
SLOT_PATTERN = 63

SEED = {0: 60, 1: 62, 7: 67, 17: 72}

# A bystander/destination marker on a step the seed never touches, with a value
# distinct from the Note default (0x3c=60) — so a stray group reload (the
# auto-load regression) reads back as something other than this marker.
MARK_STEP = 5
MARK_VAL = 99


@pytest.fixture(autouse=True)
def _release_pattern_after(board):
    """Never leave PATTERN held: a gesture aborted mid-hold (assertion or a
    button the firmware rejects) would otherwise poison every following test
    (they'd see the CAPTURE overlay). PATTERN is always configured, so the
    release dispatches; swallow anything just in case."""
    yield
    try:
        board.button(Button.PATTERN, depressed=True)
    except Exception:
        pass


def _seed(board: Board, track: int, seed: dict[int, int]) -> None:
    for step, note in seed.items():
        board.track_drum_par_set(track, 0, step, note)


def _bounce_gesture(board: Board, dst_track: int, group_gp: int, pattern_gp: int) -> None:
    """Drive the full PATTERN-hold capture gesture via injected buttons.
    dst_track is 0-based; select-row buttons are 1-based (DIRECT_TRACK)."""
    board.button(Button.PATTERN, depressed=False)        # PATTERN down (hold)
    try:
        board.press(Button.DIRECT_TRACK(dst_track + 1))  # select-row: destination track
        board.press(Button.GP(group_gp))                 # GP1-8: destination group
        board.press(Button.GP(pattern_gp))               # GP9-16: pattern -> commit
    finally:
        board.button(Button.PATTERN, depressed=True)     # always release the hold


@pytest.mark.hardware
def test_bounce_gesture_lands_in_selected_slot_and_persists(board):
    """The headline guarantee: the gesture's capture lands in the slot you
    picked and stays there — load it (move to it) and the captured track plays;
    switching the group away and back keeps it (persisted to SD, no manual save).
    """
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.ui_track_set(SRC_TRACK)                # visible/source = track 0
    time.sleep(SETTLE)
    _seed(board, SRC_TRACK, SEED)

    # bounce source -> dst track 4 (slot position 0) of the slot picked by
    # GP8 (group H) + GP16 (pattern 8) == bank0/pattern63.
    _bounce_gesture(board, dst_track=DST_XGROUP, group_gp=GROUP_GP, pattern_gp=PATTERN_GP)
    time.sleep(SETTLE)

    # "move to it": load the SELECTED slot into a group; the captured track is
    # present at the position we bounced to (dst track 4 -> slot position 0 ->
    # that group's first track).
    assert board.pattern_load(group=2, bank=SLOT_BANK, pattern=SLOT_PATTERN), "load slot"
    time.sleep(SETTLE)
    for step, note in SEED.items():
        assert board.track_drum_par_get(8, 0, step) == note, (
            f"captured track should be in slot {SLOT_PATTERN} right after the "
            f"gesture (step {step})"
        )

    # persists: switch the group away to another pattern, then back to the slot.
    board.pattern_load(group=2, bank=0, pattern=0)
    time.sleep(SETTLE)
    assert board.pattern_load(group=2, bank=SLOT_BANK, pattern=SLOT_PATTERN)
    time.sleep(SETTLE)
    for step, note in SEED.items():
        v = board.track_drum_par_get(8, 0, step)
        assert v == note, (
            f"captured track step {step} lost after switching away and back "
            f"(expected {note}, got {v}) — it must persist on SD with no save"
        )

    # source untouched (no jump).
    for step, note in SEED.items():
        assert board.track_drum_par_get(SRC_TRACK, 0, step) == note, (
            f"source step {step} changed after the gesture"
        )


@pytest.mark.hardware
def test_bounce_gesture_never_jumps_destination(board):
    """Never auto-load (cross-group): after the gesture the destination group's
    LIVE RAM is untouched — the capture is only on SD, not loaded over the dst
    track. A marker on the destination track survives."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.track_drum_init(DST_XGROUP)            # group 1 destination track
    board.ui_track_set(SRC_TRACK)
    time.sleep(SETTLE)
    _seed(board, SRC_TRACK, SEED)
    board.track_drum_par_set(DST_XGROUP, 0, MARK_STEP, MARK_VAL)

    _bounce_gesture(board, dst_track=DST_XGROUP, group_gp=GROUP_GP, pattern_gp=PATTERN_GP)
    time.sleep(SETTLE)

    # the destination track's live RAM still holds the marker, NOT the capture
    # (a regression that auto-loaded the slot would replace it with slot pos 0).
    assert board.track_drum_par_get(DST_XGROUP, 0, MARK_STEP) == MARK_VAL, (
        "destination group was reloaded by the gesture (auto-load regression) — "
        "the live track being played would be replaced by the frozen copy"
    )
    # source untouched.
    for step, note in SEED.items():
        assert board.track_drum_par_get(SRC_TRACK, 0, step) == note


@pytest.mark.hardware
def test_bounce_gesture_same_group_leaves_source_live(board):
    """Never auto-load (same group): bouncing within the source's OWN group must
    not reload that group — which would swap the live track being tweaked for the
    frozen copy. The source and a bystander group-mate keep their live values.
    (Regression guard for the same-group auto-load that violated 'no jump'.)"""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.track_drum_init(BYSTANDER)             # same group (0), never bounced
    board.ui_track_set(SRC_TRACK)
    time.sleep(SETTLE)
    _seed(board, SRC_TRACK, SEED)
    board.track_drum_par_set(BYSTANDER, 0, MARK_STEP, MARK_VAL)

    # bounce source (track 0) -> track 2, both in group 0 (same group).
    _bounce_gesture(board, dst_track=DST_SAMEGROUP, group_gp=GROUP_GP, pattern_gp=PATTERN_GP)
    time.sleep(SETTLE)

    # the live source must be exactly what we seeded — not the slot's stored copy.
    for step, note in SEED.items():
        v = board.track_drum_par_get(SRC_TRACK, 0, step)
        assert v == note, (
            f"same-group bounce reloaded the source's group: source step {step} "
            f"became {v}, expected {note} — the track you're tweaking was clobbered"
        )
    # the untouched group-mate keeps its marker (the group was not reloaded).
    assert board.track_drum_par_get(BYSTANDER, 0, MARK_STEP) == MARK_VAL, (
        "a source group-mate changed — the source group was reloaded from the slot"
    )


@pytest.mark.hardware
def test_select_row_button_state_survives_pattern_straddle(board):
    """Regression: a select-row press whose press/release straddles the
    PATTERN-hold boundary must not leave button_state with a stuck bit and
    corrupt the track-select radio afterward (symptom: presses toggle instead
    of switching)."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    time.sleep(SETTLE)

    # establish track-select view + confirm the select row switches tracks
    board.press(Button.DIRECT_TRACK(4))          # select track 3
    time.sleep(SETTLE)
    if board.ui_track_get() != 3:
        pytest.skip("select row not in track-select mode on this hardware config")

    # straddle: hold a select button across the PATTERN press/release
    board.button(Button.DIRECT_TRACK(6), depressed=False)  # DT6 (track 5) DOWN, no PATTERN
    board.button(Button.PATTERN, depressed=False)          # PATTERN DOWN (DT6 still held)
    board.button(Button.DIRECT_TRACK(6), depressed=True)   # DT6 UP during the hold (the straddle)
    board.button(Button.PATTERN, depressed=True)           # PATTERN UP
    time.sleep(SETTLE)

    # track-select must still radio cleanly to a single track.
    board.press(Button.DIRECT_TRACK(8))          # select track 7
    time.sleep(SETTLE)
    got = board.ui_track_get()
    assert got == 7, (
        f"track-select corrupted after a select press straddling the PATTERN "
        f"hold (button_state stuck): selecting track 7 landed on {got}"
    )
