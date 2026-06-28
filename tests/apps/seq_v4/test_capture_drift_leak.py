"""phrase_drift leak — the three slot-capture verbs (design §9 leak note).

CaptureToSlotTrack / CaptureSpanToSlotTrack / CopyTrackLiveToSlot all do a STAGED
load-modify-save into the destination group: they read the target slot INTO the
live dst group (CC-replay through SEQ_CC_Set raises drift for that group), modify
one track, write the slot, then restore the dst group's live RAM byte-identical.
They already snapshot+restore seq_pattern_dirty around this — but until this fix
they did NOT restore phrase_drift, so a clean dst group came out flagged "delib-
erately edited". The cost: the next phrase recall's drift-gated writeback saw the
group as drifted and paid a spurious ~290 ms flash save.

These pin the cure: a slot capture whose DESTINATION group was NOT touched by the
user must leave that group's drift bit clear. The source track lives in group 0
(edited by setup -> its bit may be set, irrelevant); the capture targets group 1,
which the user never edited -> its bit must stay 0. Uses the per-group drift mask
(CMD_PHRASE_META DRIFT_QUERY reply[3]) to isolate the destination group.

Destructive: overwrites (bank 0, pattern 63) on SD.
"""

import time

import pytest

from harness import Board, CC
from harness.sysex import RESET_UNMUTE_ALL

SRC_TRACK = 0       # group 0
DST_TRACK = 4       # group 1, slot track 0
DST_GROUP = 1
DST_BIT = 1 << DST_GROUP

SLOT_BANK = 0
SLOT_PATTERN = 63

SEQ_CC_LENGTH = 0x4D
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0
EVENT_MODE_NOTE = 0
NOTE_LAYER = 0
W = 16              # one bar


def _line_note(step: int) -> int:
    return 72 if step == 0 else 48 + step


def _setup_src_line(board: Board) -> None:
    """Lay a recognizable static line on the source (group 0) and arm the ring on it."""
    board.track_drum_init(SRC_TRACK)
    board.cc_set(SRC_TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(SRC_TRACK, SEQ_CC_LENGTH, W - 1)
    board.cc_set(SRC_TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(SRC_TRACK)
    board.trg_byte_set(SRC_TRACK, 0, 0xFF)
    board.trg_byte_set(SRC_TRACK, 1, 0xFF)
    for s in range(W):
        board.track_par_set(SRC_TRACK, NOTE_LAYER, 0, s, _line_note(s))


def _preseed_slot(board: Board) -> None:
    """Make (SLOT_BANK, SLOT_PATTERN) a readable slot — the verbs read it before
    they write. Pre-seed via a static capture into group 0 (src's own group), so it
    does NOT touch group 1 (the destination group under test)."""
    assert board.capture_to_slot_track(SRC_TRACK, SRC_TRACK, SLOT_BANK, SLOT_PATTERN), \
        "static pre-seed of the slot should commit"


def _wait_ring_depth(board: Board, target: int, timeout: float = 25.0) -> int:
    deadline = time.monotonic() + timeout
    depth = 0
    while time.monotonic() < deadline:
        depth = board.capture_ring_query()["depth"]
        if depth >= target:
            return depth
        time.sleep(0.25)
    raise AssertionError(f"ring only reached depth {depth} (< {target}) — transport not advancing?")


@pytest.mark.hardware
def test_static_slot_capture_no_drift_leak(board):
    """CaptureToSlotTrack (static, k=0): capturing src's frozen output INTO group 1's
    slot must not flag group 1 as drifted."""
    board.reset(RESET_UNMUTE_ALL)
    _setup_src_line(board)
    _preseed_slot(board)

    assert (board.phrase_drift_mask() & DST_BIT) == 0, \
        "precondition: the destination group must start undrifted"

    assert board.capture_to_slot_track(SRC_TRACK, DST_TRACK, SLOT_BANK, SLOT_PATTERN), \
        "static slot capture should commit"

    assert (board.phrase_drift_mask() & DST_BIT) == 0, (
        "phrase_drift LEAK: the static slot capture flagged its (untouched) "
        "destination group as deliberately edited"
    )


@pytest.mark.hardware
def test_copy_live_to_slot_no_drift_leak(board):
    """CopyTrackLiveToSlot (the keep-generators companion): depositing a living track
    INTO group 1's slot must not flag group 1 as drifted."""
    board.reset(RESET_UNMUTE_ALL)
    _setup_src_line(board)
    _preseed_slot(board)

    assert (board.phrase_drift_mask() & DST_BIT) == 0, \
        "precondition: the destination group must start undrifted"

    assert board.copy_track_live_to_slot(SRC_TRACK, DST_TRACK, SLOT_BANK, SLOT_PATTERN), \
        "copy-live to slot should commit"

    assert (board.phrase_drift_mask() & DST_BIT) == 0, (
        "phrase_drift LEAK: copy-live-to-slot flagged its (untouched) destination "
        "group as deliberately edited"
    )


@pytest.mark.hardware
def test_recorder_slot_capture_no_drift_leak(board):
    """CaptureSpanToSlotTrack (the while-playing recorder grab, k>0): capturing the
    last k loops INTO group 1's slot — which borrows a scratch track in group 1 and
    stages the slot load there — must not flag group 1 as drifted."""
    board.reset(RESET_UNMUTE_ALL)
    _setup_src_line(board)
    _preseed_slot(board)

    assert (board.phrase_drift_mask() & DST_BIT) == 0, \
        "precondition: the destination group must start undrifted"

    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        assert board.capture_to_slot_track(SRC_TRACK, DST_TRACK, SLOT_BANK, SLOT_PATTERN,
                                           k=1), \
            "recorder slot capture should commit"
    finally:
        board.transport(start=False)

    assert (board.phrase_drift_mask() & DST_BIT) == 0, (
        "phrase_drift LEAK: the recorder slot capture flagged its (untouched) "
        "destination group as deliberately edited"
    )
