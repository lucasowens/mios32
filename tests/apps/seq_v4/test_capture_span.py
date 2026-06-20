"""Retroactive CAPTURE — the ring + re-sim + materialize machinery (2026-06-19).

The box always rings the visible track's per-bar generative FRAME; CAPTURE rewinds
the ring K bars and re-drives the engine forward WITH generator wander on, recording
the EMITTED stream (traversal order + robotize live in the emitted notes, not the
rendered mirror) and quantizing it into a static dst track. First cut: transport
STOPPED, melodic/normal tracks, note-on-only materialize with a default gate length.

These pins exercise the machinery (ring fills/caps, refusals, determinism,
non-destructiveness, notes produced). The ultimate "sounds like what just played"
is the by-ear GO. Driven via CMD_CLOCK_STEP (advances the engine while stopped so
the ring fills). Tick math: ref_step advances every 96 ticks, a measure every
steps_per_measure(16) 16ths -> ~1536 ticks/measure.
"""

import time

import pytest

from harness import Board, Button, CC, Encoder, Page
from harness.sysex import DIAL_DEPTH, DIAL_RANGE_MAX, DIAL_RANGE_MIN, DIAL_RATE

TRACK = 0
DST = 2
MEASURE_TICKS = 16 * 96  # steps_per_measure(16) * ticks_per_16th(96)
GP1 = Button.GP(1)
SETTLE = 0.10

SEQ_CC_LENGTH = 0x4d
SEQ_CC_DIRECTION = 0x48
TRKDIR_RANDOM_STEP = 5
EVENT_MODE_NOTE = 0  # melodic (the first-cut capture target; drum has no note par layer)


def _drive_measures(board: Board, measures: int) -> None:
    """Advance the (stopped) engine `measures` musical measures, in clock_step
    chunks (the verb's tick payload is 14-bit, max 16383)."""
    remaining = measures * MEASURE_TICKS
    while remaining > 0:
        chunk = min(remaining, 16000)
        board.clock_step(chunk)
        remaining -= chunk


def _setup_wander_track(board: Board) -> None:
    """Engage a wide, fully-active pitch generator on a MELODIC (Note) TRACK with
    random-step traversal — a real generative line for the ring to capture. Melodic
    (not drum) so the note par-layer exists for the materialize to write into."""
    board.track_drum_init(TRACK)                      # clean known init
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)  # -> Note: re-partitions to a note layer
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)            # 16 steps = one whole measure
    board.ui_track_set(TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    board.press(GP1)  # ENGAGE
    time.sleep(0.85)
    board.generator_dial_set(TRACK, 0, DIAL_RANGE_MIN, 36)
    board.generator_dial_set(TRACK, 0, DIAL_RANGE_MAX, 84)
    board.generator_dial_set(TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(TRACK, 0, DIAL_DEPTH, 64)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_RANDOM_STEP)
    # Gate every step so the re-sim actually emits notes (the generator drives PITCH;
    # gates are a separate trigger layer). trg layer 0 = gate; 2 bytes = 16 steps.
    board.trg_byte_set(TRACK, 0, 0xff)
    board.trg_byte_set(TRACK, 1, 0xff)
    time.sleep(SETTLE)


@pytest.mark.hardware
def test_capture_ring_fills_and_caps(board):
    """Driving the stopped engine fills the ring; depth caps at 16 (discard-
    oldest) and the ring records the visible track."""
    board.reset()
    board.ui_track_set(TRACK)
    _drive_measures(board, 20)  # > 16 -> capped
    q = board.capture_ring_query()
    assert q["depth"] == 16, f"ring depth should cap at 16, got {q['depth']}"
    assert q["track"] == TRACK, f"ring should record visible track {TRACK}, got {q['track']}"


@pytest.mark.hardware
def test_capture_refuses_non_whole_measure(board):
    """A track whose length is not a whole measure is refused (status 0x18 =
    0x10|8) — the ring index aligns to the global measure, not the track wrap."""
    board.reset()
    board.ui_track_set(TRACK)
    board.cc_set(TRACK, SEQ_CC_LENGTH, 10)  # 11 steps: not a multiple/divisor of 16
    _drive_measures(board, 3)
    status = board.capture_span(TRACK, 1, DST)
    assert status == 0x18, f"expected whole-measure refusal 0x18, got {hex(status)}"


@pytest.mark.hardware
def test_capture_refuses_wrong_track(board):
    """Capturing a track the ring is not recording is refused (0x14 = 0x10|4)."""
    board.reset()
    board.ui_track_set(TRACK)
    _drive_measures(board, 3)
    status = board.capture_span(TRACK + 1, 1, DST)  # ring records TRACK, not TRACK+1
    assert status == 0x14, f"expected wrong-track refusal 0x14, got {hex(status)}"


@pytest.mark.hardware
def test_capture_produces_notes_and_is_deterministic(board):
    """With a wander generator engaged, CAPTURE writes notes into dst, and two
    captures of the same (non-destructively preserved) ring produce identical dst
    — the re-sim + materialize are deterministic."""
    board.reset()
    _setup_wander_track(board)
    _drive_measures(board, 4)

    # K=2: a multi-bar span — exercises the par/trg independent-instrument-count
    # path (a K=1 single bar slipped past the conflation bug; K=2 would not have).
    status1 = board.capture_span(TRACK, 2, DST)
    assert status1 == 0x01, f"capture should succeed, got {hex(status1)}"
    trg1 = board.trg_byte_get(DST, 0, 0)  # some gate bits expected
    par1 = [board.track_par_get(DST, s, 0, 0) for s in range(32)]
    assert any(par1), "captured dst has no notes"

    status2 = board.capture_span(TRACK, 2, DST)
    assert status2 == 0x01
    par2 = [board.track_par_get(DST, s, 0, 0) for s in range(32)]
    assert par2 == par1, (
        "re-sim/materialize not deterministic across two captures:\n"
        f"  A: {par1}\n  B: {par2}"
    )


@pytest.mark.hardware
def test_capture_is_nondestructive(board):
    """The capture borrow restores the live engine: the source generator seed and
    traversal seed are unchanged after a capture."""
    board.reset()
    _setup_wander_track(board)
    _drive_measures(board, 4)

    gen_before = board.gen_seed_get(TRACK, 0)
    trv_before = board.traverse_seed_get(TRACK)
    assert board.capture_span(TRACK, 1, DST) == 0x01
    gen_after = board.gen_seed_get(TRACK, 0)
    trv_after = board.traverse_seed_get(TRACK)
    assert gen_after == gen_before, f"gen seed changed by capture: {gen_before}->{gen_after}"
    assert trv_after == trv_before, f"traverse seed changed by capture: {trv_before}->{trv_after}"
