"""CAPTURE of a SYNCH_TO_MEASURE track — route the synch'd loop as a 1-bar grab (2026-06-27).

A SYNCH_TO_MEASURE track is force-reset to step 0 at every global-measure boundary
(SEQ_CORE_ResetTrkPos in the tick body), so its AUDIBLE loop is exactly ONE global
measure regardless of tcc->length: a track LONGER than the measure is truncated at the
bar, a SHORTER one re-aligns at the bar. The old CAPTURE keyed the loop length off the
raw tcc->length, so a synch'd track whose length != gspm was either refused outright
(stopped re-sim -> 0x18) or sliced by its raw period (the tape's non-aligned branch
captured 1.5 loops as if 24 distinct steps). SEQ_CORE_CaptureLoopSteps now reports gspm
for a synch'd track, so BOTH paths route through the well-validated whole-measure (n=1)
machinery and the captured bar is exactly what sounds.

Pins, against the default gspm=16:
  - LONGER (24 steps, synch'd): the bar is source steps 0..15 (steps 16..23 never play).
  - SHORTER (12 steps, synch'd): the bar re-aligns -> source steps 0..11,0..3 (s % 12).
Both transport states (live tape + stopped re-sim) reproduce the bar note-for-note, and
the static dst comes out as a plain 16-step loop with SYNCH_TO_MEASURE stripped.
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
SEQ_CC_CLKDIV_FLAGS = 0x4F   # bit 0 = SYNCH_TO_MEASURE
TRKDIR_FORWARD = 0
CLKDIV_16TH = 15             # (15+1)*6 = 96 ticks/step = one 16th = the global grid
CLKDIV_8TH = 31             # (31+1)*6 = 192 ticks/step = one 8th = 2x the global 16th (FOREIGN)
DST_SPM_8TH = 8             # a global bar (16 sixteenths = 1536 ticks) holds 1536/192 = 8 eighths


def _setup_synched_line(board: Board, n_steps: int) -> None:
    """A fixed, recognizable forward line of `n_steps` steps with SYNCH_TO_MEASURE ON.
    Step 0 is a high accent (72), the rest an ascending ramp; gated on every step, no
    generator, forward traversal -> the captured bar is comparable element-for-element."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)   # -> Note: a note par-layer exists
    board.cc_set(TRACK, SEQ_CC_LENGTH, n_steps - 1)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.cc_set(TRACK, SEQ_CC_CLKDIV_FLAGS, 1)           # SYNCH_TO_MEASURE on
    board.ui_track_set(TRACK)                             # ring follows the visible track
    for b in range((n_steps + 7) // 8):                  # gate every step
        board.trg_byte_set(TRACK, b, 0xFF)
    board.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, n_steps):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 36 + step)


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
@pytest.mark.parametrize("n_steps", [24, 12])
def test_synch_track_while_playing_grabs_one_bar(board, n_steps):
    """WHILE PLAYING: a synch'd track of length != gspm is grabbable (it was sliced by
    its raw period before). The grab is one global measure and the dst reproduces the
    AUDIBLE bar — src step (s % n_steps) for each of the 16 captured steps."""
    board.reset()
    _setup_synched_line(board, n_steps)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        status = board.capture_span(TRACK, 1, DST)        # one bar
        assert status == 0x01, f"synch'd while-playing capture should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(n_steps)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(GSPM)]
    assert any(dst), "captured dst has no notes"
    for s in range(GSPM):
        assert dst[s] == src[s % n_steps], (
            f"synch'd tape bar (len={n_steps}): dst[{s}]={dst[s]}, expected src[{s % n_steps}]={src[s % n_steps]}\n"
            f"  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
@pytest.mark.parametrize("n_steps", [24, 12])
def test_synch_track_stopped_resim_grabs_one_bar(board, n_steps):
    """WHILE STOPPED: the headline of this bundle. A synch'd track of length != gspm used
    to refuse the stopped re-sim with 0x18 ('play to grab'); routing it as a 1-bar loop
    (n=1) lets the re-sim drive run — it re-applies the synch reset at the drive's first
    tick, so the regenerated bar matches the audible bar (src step s % n_steps)."""
    board.reset()
    _setup_synched_line(board, n_steps)
    board.clock_step(16000)   # advance the stopped engine so the ring fills (~10 measures)
    status = board.capture_span(TRACK, 1, DST)
    assert status == 0x01, f"synch'd stopped re-sim should now succeed (was 0x18), got {hex(status)}"

    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(n_steps)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(GSPM)]
    assert any(dst), "captured dst has no notes"
    for s in range(GSPM):
        assert dst[s] == src[s % n_steps], (
            f"synch'd re-sim bar (len={n_steps}): dst[{s}]={dst[s]}, expected src[{s % n_steps}]={src[s % n_steps]}\n"
            f"  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
def test_capture_dst_clockdiv_pinned_to_grid_not_transient_config(board):
    """A self-bus / `Ctrl` routing on the clock divider modulates clkdiv.value per step,
    so at the grab instant the track's CONFIG divider can be a transient value (e.g. 0 ->
    divide-by-1) that disagrees with the step spacing the notes actually played at. The
    full-CC inherit used to copy that transient -> the frozen copy ran 16x too fast (found
    by ear, synch + Ctrl-on-divider). PrepDst now pins the dst divider to the CAPTURE GRID
    (tps). Simulated here by driving at the 16th grid (step_length 96 cached), then poking
    the config divider to 0 before a stopped grab; the dst must come out at CLKDIV_16TH."""
    board.reset()
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, GSPM - 1)         # one bar, not synch'd: general case
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.cc_set(TRACK, SEQ_CC_CLK_DIVIDER, CLKDIV_16TH)  # 16th grid -> step_length 96
    board.ui_track_set(TRACK)
    for b in range((GSPM + 7) // 8):
        board.trg_byte_set(TRACK, b, 0xFF)
    for step in range(GSPM):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 48 + step)
    board.clock_step(16000)                              # cached step_length settles at 96
    board.cc_set(TRACK, SEQ_CC_CLK_DIVIDER, 0)           # transient self-bus value: divide-by-1
    status = board.capture_span(TRACK, 1, DST)
    assert status == 0x01, f"stopped capture should succeed, got {hex(status)}"
    assert board.cc_get(DST, SEQ_CC_CLK_DIVIDER) == CLKDIV_16TH, (
        "dst clock divider must be pinned to the capture grid (16th), not the transient "
        f"config 0; got {board.cc_get(DST, SEQ_CC_CLK_DIVIDER)}"
    )


def _setup_synched_line_8th(board: Board, n_steps: int) -> None:
    """Same recognizable forward line, but on a FOREIGN clkdiv: 8th notes (step_length 192),
    twice the 96-tick global 16th grid. SYNCH resets it every global bar (1536 ticks), so its
    AUDIBLE loop is DST_SPM_8TH=8 of its OWN steps regardless of n_steps — not gspm=16."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, n_steps - 1)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.cc_set(TRACK, SEQ_CC_CLK_DIVIDER, CLKDIV_8TH)   # FOREIGN grid: tps=192 != 96
    board.cc_set(TRACK, SEQ_CC_CLKDIV_FLAGS, 1)           # SYNCH_TO_MEASURE on
    board.ui_track_set(TRACK)
    for b in range((n_steps + 7) // 8):                  # gate every step
        board.trg_byte_set(TRACK, b, 0xFF)
    board.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, n_steps):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 36 + step)


@pytest.mark.hardware
@pytest.mark.parametrize("n_steps", [12, 6])
def test_synch_foreign_clkdiv_while_playing_one_bar(board, n_steps):
    """REGRESSION (foreign-clkdiv synch period-doubling): a SYNCH_TO_MEASURE track on an 8th
    clkdiv (tps=192) must capture a TRUE 1-bar loop of 8 steps, NOT a 16-step / 2-bar loop.
    The bug dimensioned the dst by gspm=16 at the 192-tick step (16*192 = 3072 = 2 bars) so
    the tape grab came out as the bar + a silent bar. dst_steps is now gspm*96/tps = 8."""
    board.reset()
    _setup_synched_line_8th(board, n_steps)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        status = board.capture_span(TRACK, 1, DST)        # one bar
        assert status == 0x01, f"foreign-clkdiv synch tape capture should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    assert board.cc_get(DST, SEQ_CC_LENGTH) == DST_SPM_8TH - 1, (
        f"dst must be ONE bar = {DST_SPM_8TH} steps (length {DST_SPM_8TH-1}), not period-doubled; "
        f"got length {board.cc_get(DST, SEQ_CC_LENGTH)}"
    )
    assert board.cc_get(DST, SEQ_CC_CLK_DIVIDER) == CLKDIV_8TH, (
        "dst must play at the source 8th grid (8 steps x 192 = 1536 = one bar)"
    )
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(n_steps)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(DST_SPM_8TH)]
    assert any(dst), "captured dst has no notes"
    for s in range(DST_SPM_8TH):
        assert dst[s] == src[s % n_steps], (
            f"foreign-clkdiv synch tape bar (len={n_steps}): dst[{s}]={dst[s]}, "
            f"expected src[{s % n_steps}]={src[s % n_steps]}\n  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
@pytest.mark.parametrize("n_steps", [12, 6])
def test_synch_foreign_clkdiv_stopped_resim_one_bar(board, n_steps):
    """STOPPED re-sim companion: the drive runs ONE bar (dst_spm*tps = 8*192 = 1536), not
    two, so the bar deposits ONCE into an 8-step dst. The bug drove 16*192 = 3072 ticks =
    2 bars, re-applying the synch reset mid-drive -> the bar captured TWICE."""
    board.reset()
    _setup_synched_line_8th(board, n_steps)
    board.clock_step(16000)   # settle cached step_length to 192 + fill the ring
    status = board.capture_span(TRACK, 1, DST)
    assert status == 0x01, f"foreign-clkdiv synch re-sim should succeed, got {hex(status)}"

    assert board.cc_get(DST, SEQ_CC_LENGTH) == DST_SPM_8TH - 1, (
        f"dst must be ONE bar = {DST_SPM_8TH} steps; got length {board.cc_get(DST, SEQ_CC_LENGTH)}"
    )
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(n_steps)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(DST_SPM_8TH)]
    assert any(dst), "captured dst has no notes"
    for s in range(DST_SPM_8TH):
        assert dst[s] == src[s % n_steps], (
            f"foreign-clkdiv synch re-sim bar (len={n_steps}): dst[{s}]={dst[s]}, "
            f"expected src[{s % n_steps}]={src[s % n_steps]}\n  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
def test_synch_capture_dst_is_plain_loop(board):
    """The captured copy is a static 16-step loop: length follows the one-bar grab and
    SYNCH_TO_MEASURE is stripped (ResetGenerativeForBounce) so the frozen bar plays
    straight through and never re-resets itself."""
    board.reset()
    _setup_synched_line(board, 24)
    board.clock_step(16000)
    status = board.capture_span(TRACK, 1, DST)
    assert status == 0x01, f"synch'd stopped capture should succeed, got {hex(status)}"
    assert board.cc_get(DST, SEQ_CC_LENGTH) == GSPM - 1, "dst should be one global measure long"
    assert (board.cc_get(DST, SEQ_CC_CLKDIV_FLAGS) & 1) == 0, "dst must not inherit SYNCH_TO_MEASURE"
