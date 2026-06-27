"""Capture the live RECORDER into another pattern of the same track (2026-06-27).

The PATTERN-hold "capture a track into a pattern slot" gesture used to freeze the
static render mirror. Option 1 makes it transport-conditional: while PLAYING it
sources the retroactive RECORDER (the tape — what actually sounded) instead, via
SEQ_CORE_CaptureSpanToSlotTrack. With dst_track == src_track that is "same track,
another pattern" — perform a loop, drop it into pattern N, keep performing.

Driven through the CMD_CAPTURE_TO_SLOT_TRACK verb with a k>0 byte (the recorder
path). The slot is pre-seeded with a MARKER via the static path so the assertion
isolates the recorder write (the slot must change FROM the marker TO the played
line), and the source track is checked byte-stable (non-destructive borrow).

Destructive: overwrites (bank 0, pattern 63) on the SD card and loads it into
group 1 (tracks 4..7) to read it back without leftover source RAM.
"""

import time

import pytest

from harness import Board, CC
from harness.sysex import RESET_UNMUTE_ALL

TRACK = 0
NOTE_LAYER = 0
EVENT_MODE_NOTE = 0

SEQ_CC_LENGTH = 0x4D
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0

# Safe scratch slot (same one the bounce suite uses) + a verify group that is NOT
# the source group, so the bytes we read can only have come from SD.
SLOT_BANK = 0
SLOT_PATTERN = 63
VERIFY_GROUP = 1
VERIFY_TRACK = 4   # slot track 0 -> group-1 track 0 = global track 4

MARKER = 100       # pre-seed value; the played line must overwrite it
SETTLE = 0.15


def _line_note(step: int) -> int:
    """The recognizable played line: step 0 accent, then an ascending ramp."""
    return 72 if step == 0 else 48 + step


def _setup_line(board: Board) -> None:
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)                # 16 steps = one bar
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)                             # ring records the visible track
    board.trg_byte_set(TRACK, 0, 0xFF)                   # gate every step
    board.trg_byte_set(TRACK, 1, 0xFF)


def _num_steps(board: Board, track: int) -> int:
    """Probe the track's allocated trg step count via the trg_byte_get range guard
    (the firmware rejects a read past num_steps). num_steps = 8 * highest valid 8-step
    block. Used to pin that a slot-capture doesn't shrink the SOURCE's geometry."""
    n = 0
    for s8 in range(32):
        try:
            board.trg_byte_get(track, 0, 0, step8_start=s8, step8_count=1)
            n = s8 + 1
        except RuntimeError:
            break
    return n * 8


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
def test_recorder_capture_lands_in_same_track_other_pattern(board):
    board.reset(RESET_UNMUTE_ALL)
    _setup_line(board)

    # Pre-seed the slot's track 0 with a MARKER (static path, stopped) so the final
    # assertion proves the RECORDER overwrote it (not leftover slot content).
    for s in range(16):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, MARKER)
    assert board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN), \
        "static pre-seed of the slot should commit"

    # Now lay down the real line on the SAME track and perform it.
    for s in range(16):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, _line_note(s))
    src_before = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(16)]

    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        # Recorder -> same track, another pattern (k=1 = the last loop).
        ok = board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN, k=1)
        assert ok, "recorder capture-to-slot should commit"
    finally:
        board.transport(start=False)

    # The performed track must be byte-stable (non-destructive borrow + group restore).
    src_after = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(16)]
    assert src_after == src_before, (
        f"source track disturbed by capture-to-slot:\n  before: {src_before}\n  after:  {src_after}"
    )

    # Read the slot back from SD via a different group: track 0 must now be the
    # PLAYED line, not the marker.
    assert board.pattern_load(group=VERIFY_GROUP, bank=SLOT_BANK, pattern=SLOT_PATTERN)
    time.sleep(SETTLE)
    slot = [board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, s) for s in range(16)]
    assert all(v != MARKER for v in slot), (
        f"slot still holds the pre-seed marker — recorder did not write it: {slot}"
    )
    for s in range(16):
        assert slot[s] == _line_note(s), (
            f"slot track step {s} = {slot[s]}, expected played line {_line_note(s)}\n  slot: {slot}"
        )


@pytest.mark.hardware
def test_recorder_capture_preserves_source_geometry(board):
    """The recorder→slot capture borrows a scratch track + restages the dst group on
    SD, then restores the group's live RAM. That restore must re-apply each track's
    par/trg GEOMETRY (num_steps/layers/instr) — which lives in seq_par/trg metadata,
    not in the CC struct or byte buffers. Without it, capturing a sub-measure loop
    (8 steps) over a 64-step SOURCE shrank the source's num_steps to 8, leaving its
    longer LENGTH invalid ("!!!/8" on the TRKLEN page). Regression for that report."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)  # melodic -> 64 allocated steps
    board.cc_set(TRACK, SEQ_CC_LENGTH, 7)               # LENGTH 8 (sub-measure) over 64 alloc
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)
    board.trg_byte_set(TRACK, 0, 0xFF)
    for s in range(8):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, _line_note(s))

    ns_before = _num_steps(board, TRACK)
    len_before = board.cc_get(TRACK, SEQ_CC_LENGTH)
    assert ns_before > 8, f"precondition: source must be allocated >8 steps, got {ns_before}"

    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        assert board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN, k=1), \
            "recorder capture-to-slot should commit"
    finally:
        board.transport(start=False)

    assert _num_steps(board, TRACK) == ns_before, (
        f"source num_steps shrank from {ns_before} to {_num_steps(board, TRACK)} — the "
        f"slot-capture restore must re-apply geometry (the '!!!/8' bug)"
    )
    assert board.cc_get(TRACK, SEQ_CC_LENGTH) == len_before, "source LENGTH disturbed"
