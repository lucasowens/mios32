"""Capture → track: SEQ_CORE_CaptureToTrack engine coverage.

The lossless computed-output capture verb that lands a track's rendered output
onto another track in the current pattern (RAM only — no SD). The UI trigger is
deferred; these pin the engine via the CMD_CAPTURE_TO_TRACK testctrl.

What's pinned:
  - whole-buffer lossless copy (widely-spaced seeded steps all land on dst)
  - src is left byte-identical (capture is non-destructive on the source)
  - src == dst is refused (in-place is the freeze verb, not this one)
  - geometry inherit: a non-drum dst is repartitioned to the src's drum layout
    so the captured bytes read back correctly (the COPY/PASTE-TRACK path)
  - sweep-safety: a capture right after a dial touch still copies the WHOLE
    buffer (the primitive forces a full quiet render — without it, sweep regime
    would only refresh a ~4-step slice)

A plain drum track (no enabled processor) renders identity (output ≡ source), so
the captured dst par-layer equals the seeded src par-layer byte-for-byte.
"""

import time

import pytest

from harness import Board, CC, Page


SRC_TRACK = 0
DST_TRACK = 1

EVENT_MODE_NOTE = 0
EVENT_MODE_DRUM = 3

# Mirror of seq_core_trk_playmode_t (seq_core.h): playmode CC = CC.MODE (0x40).
TRKMODE_CHORDMASK = 4

SETTLE = 0.15

# Widely-spaced steps with distinct values — a partial / sweep-sliced copy
# (only the first few steps) or an off-by-N stride bug shows immediately.
SEED = {0: 60, 1: 62, 7: 67, 17: 72, 31: 48, 63: 55}


def _setup_two_drum_tracks(board: Board) -> None:
    board.track_drum_init(SRC_TRACK)
    board.track_drum_init(DST_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)


def _seed_source(board: Board, track: int, instr: int, seed: dict[int, int]) -> None:
    for step, note in seed.items():
        board.track_drum_par_set(track, instr, step, note)


@pytest.mark.hardware
def test_capture_copies_output_to_track(board):
    """capture_to_track copies src's whole computed output onto dst.

    src is a plain drum track (identity render → output ≡ source), so dst's
    par-layer must equal the seeded src bytes, including widely-spaced steps,
    and unseeded steps must stay 0 (capture writes, not OR's)."""
    _setup_two_drum_tracks(board)
    _seed_source(board, SRC_TRACK, 0, SEED)

    assert board.capture_to_track(SRC_TRACK, DST_TRACK), "capture should succeed"

    for step, note in SEED.items():
        v = board.track_drum_par_get(DST_TRACK, 0, step)
        assert v == note, (
            f"dst D0 step {step} should hold captured byte {note}; got {v}"
        )
    # an unseeded step must remain 0 (whole-buffer copy, not a 4-step slice).
    assert board.track_drum_par_get(DST_TRACK, 0, 8) == 0, (
        "dst unseeded step should be 0 after capture"
    )


@pytest.mark.hardware
def test_capture_preserves_src(board):
    """Capture is non-destructive on the source track."""
    _setup_two_drum_tracks(board)
    _seed_source(board, SRC_TRACK, 0, SEED)

    assert board.capture_to_track(SRC_TRACK, DST_TRACK)

    assert board.cc_get(SRC_TRACK, CC.EVENT_MODE) == EVENT_MODE_DRUM
    for step, note in SEED.items():
        v = board.track_drum_par_get(SRC_TRACK, 0, step)
        assert v == note, (
            f"src D0 step {step} should still be {note} after capture; got {v}"
        )


@pytest.mark.hardware
def test_capture_refuses_in_place(board):
    """src == dst is refused (the in-place freeze is GP8, not this verb)."""
    _setup_two_drum_tracks(board)
    _seed_source(board, SRC_TRACK, 0, SEED)

    assert not board.capture_to_track(SRC_TRACK, SRC_TRACK), (
        "capture_to_track with src == dst must be refused"
    )


@pytest.mark.hardware
def test_capture_inherits_geometry(board):
    """A non-drum dst is repartitioned to the src's drum layout so the captured
    bytes read back correctly (the COPY/PASTE-TRACK inherit path — the decisive
    cross-config correctness item)."""
    board.track_drum_init(SRC_TRACK)
    _seed_source(board, SRC_TRACK, 0, SEED)

    # Force dst to a different event mode so the inherit/repartition branch runs.
    board.cc_set(DST_TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    time.sleep(SETTLE)
    assert board.cc_get(DST_TRACK, CC.EVENT_MODE) == EVENT_MODE_NOTE

    assert board.capture_to_track(SRC_TRACK, DST_TRACK), "capture should succeed"

    assert board.cc_get(DST_TRACK, CC.EVENT_MODE) == EVENT_MODE_DRUM, (
        "dst should have inherited the src's Drum event mode"
    )
    for step, note in SEED.items():
        v = board.track_drum_par_get(DST_TRACK, 0, step)
        assert v == note, (
            f"after geometry inherit, dst D0 step {step} should hold {note}; got {v}"
        )


@pytest.mark.hardware
def test_capture_whole_buffer_after_dial_touch(board):
    """Sweep-safety regression: a capture immediately after a dial touch (which
    arms the sweep regime) must still copy the WHOLE buffer, not just the ~4-step
    sweep slice. The primitive forces a full quiet render to guarantee this.

    chord_mask strength=0 is a source passthrough (output ≡ source), so the
    widely-spaced seeded bytes must all survive the capture."""
    _setup_two_drum_tracks(board)
    _seed_source(board, SRC_TRACK, 0, SEED)

    # Enable the processor then touch its strength dial — this calls
    # RenderTouched and arms sweep regime on the source track.
    board.cc_set(SRC_TRACK, CC.MODE, TRKMODE_CHORDMASK)
    board.cc_set(SRC_TRACK, CC.CHORDMASK_STRENGTH, 0)
    board.cc_set(SRC_TRACK, CC.CHORDMASK_STRENGTH, 0)  # touch again to (re)arm sweep
    # capture immediately — no settle, so the renderer is still in sweep regime.
    assert board.capture_to_track(SRC_TRACK, DST_TRACK), "capture should succeed"

    # Every widely-spaced step must be present — a sweep-sliced capture would
    # only carry the steps near the current window and drop the far ones.
    for step, note in SEED.items():
        v = board.track_drum_par_get(DST_TRACK, 0, step)
        assert v == note, (
            f"dst D0 step {step} should hold {note} even after a dial touch "
            f"(whole-buffer capture); got {v} — sweep-staleness regression?"
        )


# Persisted variant — capture → a track WITHIN a pattern slot, written to SD.
# Same safe scratch slot the bounce suite uses.
SLOT_BANK = 0
SLOT_PATTERN = 63


@pytest.mark.hardware
def test_capture_to_slot_track_persists(board):
    """capture_to_slot_track renders the source into one track of a stored slot,
    persisted to SD — the captured track survives a switch (unlike the RAM-only
    capture_to_track). Cross-group: source in group A, destination track 5
    (group B, slot position 1); read it back by loading the slot into group C.
    """
    board.reset()
    board.track_drum_init(SRC_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)
    _seed_source(board, SRC_TRACK, 0, SEED)

    # render source (track 0, group A) -> track 5 (group B, slot position 1) of
    # slot (0, 63), persisted.
    assert board.capture_to_slot_track(SRC_TRACK, 5, SLOT_BANK, SLOT_PATTERN), (
        "capture_to_slot_track should succeed"
    )

    # source must be untouched (non-destructive, and its group was restored).
    for step, note in SEED.items():
        assert board.track_drum_par_get(SRC_TRACK, 0, step) == note, (
            f"source step {step} should be unchanged after capture-to-slot-track"
        )

    # load the slot into a THIRD group (group C, tracks 8-11). Slot position 1
    # -> track 9. The captured bytes can only have come from the persisted slot.
    assert board.pattern_load(group=2, bank=SLOT_BANK, pattern=SLOT_PATTERN), (
        "loading the slot should succeed"
    )
    time.sleep(SETTLE)
    for step, note in SEED.items():
        v = board.track_drum_par_get(9, 0, step)
        assert v == note, (
            f"slot position-1 track (loaded as track 9) step {step} should hold "
            f"captured byte {note}; got {v} — capture-to-slot-track not persisted?"
        )
