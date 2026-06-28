"""As-heard windowing for note grabs — the Phase: GRID/HEARD toggle (2026-06-28).

A while-PLAYING note grab is GRID (loop-aligned) by default: the tape window keeps the
last k COMPLETE loops, ending at the last loop downbeat. HEARD ends the window at the
PLAYHEAD instead — the last k bars exactly as they sounded — so the deposited loop
RESTARTS from the grab phase (rotated off the source downbeat). Tape (PLAYING) only;
the STOPPED re-sim has no playhead and is always GRID.

These pins run the engine for real (CMD_TRANSPORT) over a FIXED forward line, so the
captured copy is comparable note-for-note. HEARD's phase is mid-loop and non-deterministic
in wall-clock, so the firmware REPORTS the absolute tick window it used (win_start/win_end
+ src ticks-per-step) in the CMD_CAPTURE_SPAN reply — the test derives the deposit rotation
from those numbers race-free, instead of racing a separate tick read against the grab.

Math: with win_start = now - k*P (P = spm*tps) and a source note emitted at tick g*tps,
its dst step = floor((g*tps - win_start)/tps) = g - ceil(win_start/tps). So the deposit is
the source rotated: dst[d] == src[(d + ceil(win_start/tps)) mod spm]. GRID lands win_start
on a downbeat (rotation 0, dst[0]==src[0]); HEARD lands it mid-loop (rotation = playhead).
"""

import time

import pytest

from harness import Board, CC

TRACK = 0
DST = 2
NOTE_LAYER = 0
EVENT_MODE_NOTE = 0
SPM = 16  # one whole global measure

SEQ_CC_LENGTH = 0x4d
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0


def _setup_forward_line(board: Board) -> None:
    """A fixed, recognizable 16-step forward line on a melodic (Note) track: step 0 a high
    accent (72), steps 1..15 ascend from 49, gated on every step. No generator + forward
    traversal => every played bar is identical, so the captured copy is comparable
    element-for-element regardless of which bar(s) the grab lands on."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, SPM - 1)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)                            # ring follows the visible track
    board.trg_byte_set(TRACK, 0, 0xFF)
    board.trg_byte_set(TRACK, 1, 0xFF)
    board.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, SPM):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 48 + step)


def _wait_ring_depth(board: Board, target: int, timeout: float = 25.0) -> int:
    deadline = time.monotonic() + timeout
    depth = 0
    while time.monotonic() < deadline:
        depth = board.capture_ring_query()["depth"]
        if depth >= target:
            return depth
        time.sleep(0.25)
    raise AssertionError(f"ring only reached depth {depth} (< {target}) — transport not advancing?")


def _find_rotation(src: list, dst: list, spm: int):
    """The unique r in [0, spm) for which dst is src rotated by r (dst[s]==src[(s+r)%spm]
    for every dst step), or None if dst is not a clean rotation of src (smear/drop/torn)."""
    for r in range(spm):
        if all(dst[s] == src[(s + r) % spm] for s in range(len(dst))):
            return r
    return None


@pytest.mark.hardware
def test_as_heard_window_ends_at_playhead(board):
    """The defining as-heard property: HEARD's window ends AT/AFTER the playhead, so the
    grab keeps the last k bars right up to now. GRID would snap win_end back to the last
    loop downbeat (< the playhead whenever we sample mid-loop) — so `win_end >= playhead`
    is exactly what separates HEARD from GRID, and it needs no note comparison."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)                       # >= 2 loops of history for k=2
        t0 = board.tick_query()["bpm_tick"]              # the playhead just BEFORE the grab
        res = board.capture_span(TRACK, 2, DST, phase=1, full=True)  # HEARD
    finally:
        board.transport(start=False)

    assert res["status"] == 0x01, f"HEARD while-playing grab should succeed, got {hex(res['status'])}"
    ws, we, tps = res["win_start"], res["win_end"], res["tps"]
    assert tps and tps > 0, f"tps not reported by the firmware: {res}"
    # the grab's `now` is sampled AFTER t0, so HEARD's win_end (== now) must be >= t0.
    assert we >= t0, (
        f"HEARD win_end {we} should be >= the playhead {t0} sampled before the grab "
        "(GRID would snap win_end back to the last loop downbeat)"
    )
    # the window is exactly k loops long (k=2): P = spm*tps, span = 2*P.
    assert we - ws == 2 * SPM * tps, f"HEARD window should span k*P = {2*SPM*tps} ticks, got {we-ws}"


@pytest.mark.hardware
def test_as_heard_deposit_rotates_to_grab_phase(board):
    """Note-for-note: the HEARD deposit is the source rotated to the grab phase. Three
    independent pins — (B) dst is a CLEAN rotation of src (no smear/drop), (C) that
    rotation matches ceil(win_start/tps) mod spm computed from the reported window (the
    phase really tracks the playhead), and (A) win_end >= the sampled playhead (as-heard)."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        t0 = board.tick_query()["bpm_tick"]
        res = board.capture_span(TRACK, 2, DST, phase=1, full=True)  # HEARD, k=2 -> 32 steps
    finally:
        board.transport(start=False)

    assert res["status"] == 0x01, f"HEARD grab should succeed, got {hex(res['status'])}"
    ws, we, tps = res["win_start"], res["win_end"], res["tps"]
    assert tps and tps > 0, f"tps not reported: {res}"

    # NOTE: both reads are SEQ_PAR_Get = the transposed/force-scaled OUTPUT mirror; this pin
    # is mirror-vs-mirror and only valid because src and dst are reset defaults (no transpose,
    # no global scale). Adding either to this track would silently weaken the comparison.
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(SPM)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(2 * SPM)]
    # full distinctness over one period => the rotation that maps dst onto src is UNIQUE
    # (a sub-period source could match multiple r and make r_emp != r_expected a false fail).
    assert len(set(src)) == SPM, f"source line must be fully distinct over one period: {src}"
    assert any(dst), "captured dst has no notes"

    # (B) fidelity / no smear: dst must be SOME clean rotation of the source line.
    r_emp = _find_rotation(src, dst, SPM)
    assert r_emp is not None, (
        f"HEARD dst is not a clean rotation of the source line (smear/drop/torn window)\n"
        f"  src: {src}\n  dst: {dst}"
    )

    # (C) the rotation == the playhead phase. win_start sits mid-step for HEARD; the first
    # source-step boundary >= win_start becomes dst step 0 -> rotation = ceil(ws/tps) % spm.
    r_expected = (-(-ws // tps)) % SPM   # ceil-div in Python
    assert r_emp == r_expected, (
        f"as-heard rotation {r_emp} != ceil(win_start/tps) % spm = {r_expected} "
        f"(ws={ws}, tps={tps}) — the deposit phase does not match the reported playhead\n"
        f"  src: {src}\n  dst: {dst}"
    )

    # (A) as-heard (redundant with the test above, kept so this pin stands alone).
    assert we >= t0, f"HEARD win_end {we} should be >= playhead {t0}"


@pytest.mark.hardware
def test_grid_phase_stays_loop_aligned(board):
    """The contrast / no-regression pin: a default GRID grab keeps win_start on a loop
    downbeat (win_start % P == 0) and deposits with rotation 0 (dst[0]==src[0]). Proves the
    shipped behaviour is untouched and the new telemetry agrees with it."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        res = board.capture_span(TRACK, 2, DST, phase=0, full=True)  # GRID (explicit)
    finally:
        board.transport(start=False)

    assert res["status"] == 0x01, f"GRID grab should succeed, got {hex(res['status'])}"
    ws, tps = res["win_start"], res["tps"]
    assert tps and tps > 0, f"tps not reported: {res}"
    P = SPM * tps
    assert ws % P == 0, f"GRID win_start {ws} should be loop-aligned (multiple of P={P})"

    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(SPM)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(2 * SPM)]
    assert len(set(src)) >= 8 and src[0] != src[1], f"source line not read back: {src}"
    assert dst[0] == src[0], f"GRID downbeat phase: dst[0]={dst[0]} should == src[0]={src[0]}"
    for s in range(2 * SPM):
        assert dst[s] == src[s % SPM], f"GRID fidelity: dst[{s}]={dst[s]} != src[{s%SPM}]={src[s%SPM]}"


# the slot-track path (CMD_CAPTURE_TO_SLOT_TRACK / SEQ_CORE_CaptureSpanToSlotTrack) is what
# the Capture-page commit actually calls; it threads `phase` to the same SEQ_CORE_CaptureSpan
# as above through a 7th wire byte. capture_span pins the engine phase logic; this pins that
# the slot path's byte-packing + threading reach it (the engine then runs the same HEARD code).
SLOT_BANK = 0
SLOT_PATTERN = 63        # disposable slot (matches test_capture_recorder_to_slot)
VERIFY_GROUP = 1
VERIFY_TRACK = 4         # slot track 0 -> group-1 track 0 = global track 4


@pytest.mark.hardware
def test_as_heard_slot_track_threads_phase(board):
    """The UI commit path: capture HEARD into a slot via CaptureSpanToSlotTrack (the 7th
    phase wire byte + LOOP fit so the window deposits 1:1). Load it back and confirm the
    deposit is a clean rotation of the source line — proving the byte-packing and the
    phase threading reach the engine and produce faithful (non-refused, non-garbled) material."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        ok = board.capture_to_slot_track(TRACK, TRACK, SLOT_BANK, SLOT_PATTERN, k=2, fit_mode=1, phase=1)
        assert ok, "HEARD slot-track capture (phase=1) should commit"
    finally:
        board.transport(start=False)

    assert board.pattern_load(group=VERIFY_GROUP, bank=SLOT_BANK, pattern=SLOT_PATTERN)
    time.sleep(0.15)
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(SPM)]
    slot = [board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, s) for s in range(2 * SPM)]
    assert len(set(src)) == SPM, f"source line must be fully distinct: {src}"
    assert any(slot), "slot deposit has no notes (capture/tile/save did not run)"
    # LOOP fit tiles the W=2*spm window 1:1, so the deposit is the source rotated by the
    # grab phase — a clean rotation proves the HEARD path ran end-to-end through the slot.
    assert _find_rotation(src, slot, SPM) is not None, (
        f"slot deposit is not a clean rotation of the source line (phase path garbled)\n"
        f"  src:  {src}\n  slot: {slot}"
    )
