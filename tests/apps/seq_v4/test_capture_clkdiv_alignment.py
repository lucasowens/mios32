"""CAPTURE whole-measure classification on a FOREIGN clkdiv (non-synch) — 2026-06-28.

The "is this loop a whole number of global measures?" test must be in TICKS, not in steps.
The old gate compared the track's OWN step count to gspm (a count of global 16th-steps),
which only agrees at the 16th grid (tps==96). On a foreign clkdiv it misfired BOTH ways:

  - an 8th-note 16-step loop is 16*192 = 3072 ticks = TWO global bars, but `16 % 16 == 0`
    read it as ONE measure (n=1) -> while PLAYING it captured only the first bar into a
    2-bar geometry (bar + silent bar); the stopped re-sim wrongly ACCEPTED it as 1 bar.
  - an 8th-note 8-step loop is 8*192 = 1536 ticks = ONE true bar, but `8 % 16 != 0` pushed
    it to the non-aligned path / refused the stopped re-sim outright.

SEQ_CORE_CaptureLoopMeasures now classifies by loop_ticks: the 16-step loop is n=2 (PLAYING
captures both bars; STOPPED is refused — multi-measure stopped = the deferred A2 kernel),
the 8-step loop is n=1 (grabbable in BOTH transport states). Distinct from the SYNCH fix
(test_capture_synch_measure.py); this is the plain non-synch foreign-clkdiv case.
"""

import time

import pytest

from harness import Board, CC

TRACK = 0
DST = 2
NOTE_LAYER = 0
EVENT_MODE_NOTE = 0
GSPM = 16  # default global Steps-per-Measure

SEQ_CC_LENGTH = 0x4D
SEQ_CC_DIRECTION = 0x48
SEQ_CC_CLK_DIVIDER = 0x4C    # clkdiv.value; step_length = (value+1)*6 (non-triplet)
TRKDIR_FORWARD = 0
CLKDIV_8TH = 31             # (31+1)*6 = 192 ticks/step = one 8th = 2x the global 16th grid

STATUS_OK = 0x01
STATUS_NOT_ONE_MEASURE = 0x18  # -8 refusal: multi-measure stopped grab (A2 pending)


def _setup_8th_line(board: Board, n_steps: int) -> None:
    """A NON-synch 8th-note (step_length 192) forward line of `n_steps` distinct notes, gated
    every step. No generator, forward traversal -> the captured loop is comparable element-
    for-element. Distinct notes per step so a 2-bar loop's two bars can't be confused."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, n_steps - 1)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.cc_set(TRACK, SEQ_CC_CLK_DIVIDER, CLKDIV_8TH)   # FOREIGN grid: tps=192 != 96
    board.ui_track_set(TRACK)
    for b in range((n_steps + 7) // 8):                  # gate every step
        board.trg_byte_set(TRACK, b, 0xFF)
    board.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, n_steps):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 36 + step)


def _wait_ring_depth(board: Board, target: int, timeout: float = 30.0) -> int:
    deadline = time.monotonic() + timeout
    depth = 0
    while time.monotonic() < deadline:
        depth = board.capture_ring_query()["depth"]
        if depth >= target:
            return depth
        time.sleep(0.25)
    raise AssertionError(f"ring only reached depth {depth} (< {target}) — transport not advancing?")


@pytest.mark.hardware
def test_8th_two_bar_loop_while_playing_captures_both_bars(board):
    """REGRESSION: an 8th-note 16-step loop is TWO global bars (n=2). While PLAYING, a k=1
    grab must capture the WHOLE loop — all 16 steps — not just the first bar (the old gate
    read 16 % 16 == 0 as n=1 and deposited bar 1 + a silent bar)."""
    board.reset()
    _setup_8th_line(board, 16)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 6)                         # >= 3 complete 2-bar loops
        status = board.capture_span(TRACK, 1, DST)         # one loop = two bars
        assert status == STATUS_OK, f"2-bar 8th tape capture should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    assert board.cc_get(DST, SEQ_CC_LENGTH) == 16 - 1, (
        f"dst must hold the whole 16-step (2-bar) loop; got length {board.cc_get(DST, SEQ_CC_LENGTH)}"
    )
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(16)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(16)]
    assert any(dst[8:]), "second bar (steps 8..15) is empty — only the first bar was captured"
    for s in range(16):
        assert dst[s] == src[s], (
            f"2-bar 8th loop: dst[{s}]={dst[s]}, expected src[{s}]={src[s]}\n  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
def test_8th_two_bar_loop_stopped_is_refused(board):
    """STOPPED re-sim of an 8th-note 16-step (2-bar) loop must be REFUSED (0x18 'play to
    grab') — multi-measure stopped drive-phase is the deferred A2 kernel. The old gate
    wrongly accepted it as one measure and produced a garbage half-loop."""
    board.reset()
    _setup_8th_line(board, 16)
    board.clock_step(16000)                                # settle step_length=192 + fill ring
    status = board.capture_span(TRACK, 1, DST)
    assert status == STATUS_NOT_ONE_MEASURE, (
        f"2-bar 8th stopped grab should refuse with 0x18, got {hex(status)}"
    )


@pytest.mark.hardware
def test_8th_one_bar_loop_stopped_resim(board):
    """An 8th-note 8-step loop is ONE true global bar (8*192 = 1536 = gspm*96). The tick-
    based gate now accepts it for the STOPPED re-sim (the old step-count gate refused it
    because 8 % 16 != 0) and reproduces the bar note-for-note in an 8-step dst."""
    board.reset()
    _setup_8th_line(board, 8)
    board.clock_step(16000)
    status = board.capture_span(TRACK, 1, DST)
    assert status == STATUS_OK, f"1-bar 8th stopped re-sim should succeed, got {hex(status)}"

    assert board.cc_get(DST, SEQ_CC_LENGTH) == 8 - 1, (
        f"dst must be one 8-step bar; got length {board.cc_get(DST, SEQ_CC_LENGTH)}"
    )
    assert board.cc_get(DST, SEQ_CC_CLK_DIVIDER) == CLKDIV_8TH, "dst must play at the 8th grid"
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(8)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(8)]
    assert any(dst), "captured dst has no notes"
    for s in range(8):
        assert dst[s] == src[s], (
            f"1-bar 8th re-sim: dst[{s}]={dst[s]}, expected src[{s}]={src[s]}\n  src: {src}\n  dst: {dst}"
        )
