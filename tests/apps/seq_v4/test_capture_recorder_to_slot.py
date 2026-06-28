"""Recorder -> slot CAPTURE, the CANVAS model (2026-06-28).

The N-bar grab (CMD_CAPTURE_TO_SLOT_TRACK with k>0) no longer RESIZES the dst track
to the grab. It TILES the captured W=k*spm window into the dst track's EXISTING max
length (the "canvas") and never touches the geometry — which makes the old trg-floor
"!!!" (LENGTH > num_steps) structurally impossible. Two fit modes:

  FILL (fit_mode=0, default): tile across the whole canvas, loop AT the canvas length
                              (grid-locked; a seam when W doesn't divide the canvas).
  LOOP (fit_mode=1):          loop AT the window length (drifts free); steps past the
                              window stay rest.

A CHORD event-mode source can't round-trip through the note materialize (it stores
chord INDICES, not notes), so that path copies the source's chord par loop directly.

These pin: geometry never changes, LENGTH never exceeds num_steps (no "!!!"), FILL vs
LOOP loop-length, the tiling, the chord-index round-trip, and the source-geometry
regression (b128eecb). Destructive: overwrites (bank 0, pattern 63) on SD and reads it
back via group 1 (tracks 4..7) so the bytes can only have come from the slot.
"""

import time

import pytest

from harness import Board, CC
from harness.sysex import RESET_UNMUTE_ALL

TRACK = 0
NOTE_LAYER = 0
EVENT_MODE_NOTE = 0
EVENT_MODE_CHORD = 1

SEQ_CC_LENGTH = 0x4D
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0

FIT_FILL = 0
FIT_LOOP = 1
W = 16             # window = k*spm; spm = LENGTH+1 = 16, k = 1

SLOT_BANK = 0
SLOT_PATTERN = 63
VERIFY_GROUP = 1
VERIFY_TRACK = 4   # slot track 0 -> group-1 track 0 = global track 4

MARKER = 100       # pre-seed value; the capture must overwrite it
SETTLE = 0.15


def _line_note(step: int) -> int:
    """The recognizable played line: step 0 accent, then an ascending ramp."""
    return 72 if step == 0 else 48 + step


def _chord_idx(step: int) -> int:
    """A recognizable chord-INDEX ramp (1..16) for the chord-source pin."""
    return 1 + step


def _setup_line(board: Board) -> None:
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, W - 1)            # 16 steps = one bar
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)                            # ring records the visible track
    board.trg_byte_set(TRACK, 0, 0xFF)                  # gate every step
    board.trg_byte_set(TRACK, 1, 0xFF)
    for s in range(W):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, _line_note(s))


def _num_steps(board: Board, track: int) -> int:
    """Probe the track's allocated trg step count via the trg_byte_get range guard
    (the firmware rejects a read past num_steps). num_steps = 8 * highest valid block."""
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


def _preseed_marker(board: Board) -> None:
    """Stamp a MARKER into the slot's track 0 (static path) so a later assertion proves
    the recorder OVERWROTE it rather than reading leftover slot content."""
    for s in range(W):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, MARKER)
    assert board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN), \
        "static pre-seed of the slot should commit"


def _grab_playing(board: Board, fit_mode: int) -> None:
    board.ui_track_set(TRACK)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        assert board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN,
                                           k=1, fit_mode=fit_mode), \
            "recorder capture-to-slot should commit"
    finally:
        board.transport(start=False)


def _load_slot(board: Board) -> None:
    assert board.pattern_load(group=VERIFY_GROUP, bank=SLOT_BANK, pattern=SLOT_PATTERN)
    time.sleep(SETTLE)


@pytest.mark.hardware
def test_recorder_capture_fill_tiles_canvas(board):
    """FILL: the dst keeps its canvas; the window tiles across the WHOLE canvas and the
    loop length is the canvas. num_steps unchanged, LENGTH = canvas-1, never "!!!"."""
    board.reset(RESET_UNMUTE_ALL)
    _setup_line(board)
    canvas = _num_steps(board, TRACK)
    assert canvas > W, f"need a canvas larger than the window to test tiling, got {canvas}/{W}"

    _preseed_marker(board)
    _setup_line(board)                                   # re-lay the real line (pre-seed clobbered it)
    src_before = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(W)]
    _grab_playing(board, FIT_FILL)

    # non-destructive borrow: the live source must be byte-stable
    src_after = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(W)]
    assert src_after == src_before, f"source disturbed:\n  {src_before}\n  {src_after}"

    _load_slot(board)
    ns = _num_steps(board, VERIFY_TRACK)
    length = board.cc_get(VERIFY_TRACK, SEQ_CC_LENGTH)
    assert ns == canvas, f"FILL must NOT resize the canvas: {ns} != {canvas}"
    assert length + 1 <= ns, f'"!!!" — LENGTH+1 ({length + 1}) > num_steps ({ns})'
    assert length == canvas - 1, f"FILL must loop at the canvas: LENGTH {length} != {canvas - 1}"
    slot = [board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, s) for s in range(canvas)]
    assert all(v != MARKER for v in slot), f"recorder did not overwrite the pre-seed: {slot}"
    for s in range(canvas):
        assert slot[s] == _line_note(s % W), (
            f"FILL tile step {s} = {slot[s]}, expected {_line_note(s % W)} (line tiled)\n  {slot}"
        )


@pytest.mark.hardware
def test_recorder_capture_loop_loops_window(board):
    """LOOP: the dst keeps its canvas, but loops at the WINDOW length; steps past the
    window stay rest. num_steps unchanged, LENGTH = W-1, never "!!!"."""
    board.reset(RESET_UNMUTE_ALL)
    _setup_line(board)
    canvas = _num_steps(board, TRACK)
    assert canvas > W, f"need canvas > window, got {canvas}/{W}"

    _preseed_marker(board)
    _setup_line(board)
    _grab_playing(board, FIT_LOOP)

    _load_slot(board)
    ns = _num_steps(board, VERIFY_TRACK)
    length = board.cc_get(VERIFY_TRACK, SEQ_CC_LENGTH)
    assert ns == canvas, f"LOOP must NOT resize the canvas: {ns} != {canvas}"
    assert length + 1 <= ns, f'"!!!" — LENGTH+1 ({length + 1}) > num_steps ({ns})'
    assert length == W - 1, f"LOOP must loop at the window: LENGTH {length} != {W - 1}"
    slot = [board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, s) for s in range(canvas)]
    for s in range(W):
        assert slot[s] == _line_note(s), f"LOOP window step {s} = {slot[s]}, expected {_line_note(s)}"
    for s in range(W, canvas):
        assert slot[s] == 0, f"LOOP step {s} past the window must be rest, got {slot[s]}"


@pytest.mark.hardware
def test_recorder_capture_chord_source_keeps_chords(board):
    """A CHORD event-mode source stores chord INDICES, not notes — the note materialize
    would write garbage into the index slot. The chord path copies the source chord par
    loop directly: event mode survives and the indices round-trip (tiled, FILL). Captured
    here STOPPED, because the chord path copies the source par and is transport-agnostic
    (unlike the note tape path)."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_CHORD)
    board.cc_set(TRACK, SEQ_CC_LENGTH, W - 1)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)
    board.trg_byte_set(TRACK, 0, 0xFF)
    board.trg_byte_set(TRACK, 1, 0xFF)
    for s in range(W):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, _chord_idx(s))

    canvas = _num_steps(board, TRACK)
    assert canvas > W, f"need canvas > window, got {canvas}/{W}"
    src_before = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(W)]

    assert board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN,
                                       k=1, fit_mode=FIT_FILL), "chord capture-to-slot should commit"

    src_after = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(W)]
    assert src_after == src_before, f"chord source disturbed:\n  {src_before}\n  {src_after}"

    _load_slot(board)
    assert board.cc_get(VERIFY_TRACK, CC.EVENT_MODE) == EVENT_MODE_CHORD, \
        "captured track must stay a CHORD track"
    ns = _num_steps(board, VERIFY_TRACK)
    length = board.cc_get(VERIFY_TRACK, SEQ_CC_LENGTH)
    assert ns == canvas, f"chord capture must NOT resize the canvas: {ns} != {canvas}"
    assert length + 1 <= ns, f'"!!!" — LENGTH+1 ({length + 1}) > num_steps ({ns})'
    for s in range(canvas):
        assert board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, s) == _chord_idx(s % W), (
            f"chord index step {s} = {board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, s)}, "
            f"expected {_chord_idx(s % W)} (indices tiled, not note-materialized)"
        )


@pytest.mark.hardware
def test_recorder_capture_preserves_source_geometry(board):
    """The capture borrows a scratch track + restages the dst group on SD, then restores
    the group's live RAM — which must re-apply each track's par/trg GEOMETRY (b128eecb).
    Without it, a sub-measure grab over a longer SOURCE shrank the source's num_steps,
    leaving its LENGTH invalid ("!!!/8"). Regression for that report."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, 7)               # LENGTH 8 (sub-measure) over a longer alloc
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
