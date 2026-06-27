"""Retroactive CAPTURE — the WHILE-PLAYING live tape (2026-06-20).

Stopped capture RE-SIMULATES the generative frame (test_capture_span.py). While
PLAYING the notes are sounding now, so CAPTURE just records the emitted stream — a
passive tee off the MIDI-out drain fills a per-bar live tape; a grab quantizes the
last K completed bars into a static dst. Strictly more faithful (keeps emission
coin-flips / live keys / real timing) and no engine borrow. The gesture is identical;
SEQ_CORE_CaptureSpan dispatches PLAYING->tape, STOPPED->re-sim.

These pins need the engine to ACTUALLY run, so they use CMD_TRANSPORT (the real
play-button path) — clock_step only advances ticks with IsRunning() false, which the
tape's tap deliberately ignores. They run in real time (a bar ~1-2s), so they poll the
ring depth rather than sleep a fixed duration.

A FIXED forward line (no generator) is the source: it lets us compare the captured
copy NOTE-FOR-NOTE to what plays, which pins both tape fidelity AND the step phase
(step 0 must be the downbeat accent — a one-step rotation would be obvious). The
generative "sounds like what I just played" remains the by-ear GO.
"""

import time

import pytest

from harness import Board, CC

TRACK = 0
DST = 2
NOTE_LAYER = 0
EVENT_MODE_NOTE = 0

SEQ_CC_LENGTH = 0x4d
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0


def _setup_forward_line(board: Board) -> None:
    """A fixed, recognizable forward line on a melodic (Note) track: step 0 is a high
    accent (72), steps 1..15 ascend from 49. Gates on every step so it sounds. No
    generator + forward traversal => every played bar is identical, so the captured
    copy can be compared element-for-element (the unambiguous phase check)."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)  # -> Note: a note par-layer exists
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)               # 16 steps = one whole measure
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)                            # ring follows the visible track
    board.trg_byte_set(TRACK, 0, 0xFF)                   # gate steps 0..7
    board.trg_byte_set(TRACK, 1, 0xFF)                   # gate steps 8..15
    board.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, 16):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 48 + step)


def _setup_forward_line_len(board: Board, n_steps: int) -> None:
    """A fixed forward line of `n_steps` steps (Approach A: any length, not just a whole
    measure). Same recognizable shape as _setup_forward_line — step 0 accent (72), the
    rest an ascending ramp — gated on every step, no generator, forward traversal, so the
    captured copy is comparable element-for-element."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)
    board.cc_set(TRACK, SEQ_CC_LENGTH, n_steps - 1)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)
    for b in range((n_steps + 7) // 8):                  # gate every step
        board.trg_byte_set(TRACK, b, 0xFF)
    board.track_par_set(TRACK, NOTE_LAYER, 0, 0, 72)     # downbeat accent
    for step in range(1, n_steps):
        board.track_par_set(TRACK, NOTE_LAYER, 0, step, 36 + (step % 48))


def _wait_ring_depth(board: Board, target: int, timeout: float = 25.0) -> int:
    """Poll the CAPTURE ring until it has buffered >= `target` bars (the transport is
    genuinely running and bars are completing). Returns the depth reached; raises on
    timeout. Starting the transport resets the ring, so depth growth also proves the
    engine actually advanced past the start reset."""
    deadline = time.monotonic() + timeout
    depth = 0
    while time.monotonic() < deadline:
        depth = board.capture_ring_query()["depth"]
        if depth >= target:
            return depth
        time.sleep(0.25)
    raise AssertionError(f"ring only reached depth {depth} (< {target}) — transport not advancing?")


@pytest.mark.hardware
def test_transport_starts_and_tape_fills(board):
    """CMD_TRANSPORT genuinely runs the engine: after start, the ring (which the
    start RESETS) refills as real bars complete, and the recorded track is the visible
    one. Stop returns to a quiet, known state."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        depth = _wait_ring_depth(board, 4)
        assert depth >= 4
        q = board.capture_ring_query()
        assert q["track"] == TRACK, f"tape should record visible track {TRACK}, got {q['track']}"
    finally:
        board.transport(start=False)


@pytest.mark.hardware
def test_capture_while_playing_reproduces_line(board):
    """The headline pin: grab the last K bars WHILE PLAYING (no STOP), and the static
    dst reproduces the source line note-for-note on the correct step phase. Captures
    the recorded performance directly — the old re-sim path refused (-3) while running."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)

        # grab WHILE running — the dispatcher routes to the live tape (re-sim returns
        # -3 here; tape must succeed).
        status = board.capture_span(TRACK, 2, DST)
        assert status == 0x01, f"while-playing capture should succeed via the tape, got {hex(status)}"
    finally:
        board.transport(start=False)

    # the source is static, so each recorded bar == the source line; dst (K=2 = 32
    # steps) is that line twice. Compare element-for-element: fidelity + phase.
    # track_par_get is (track, layer, instr, step) — read the NOTE layer across steps.
    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(16)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(32)]
    # guard against a vacuous pass: the source must actually be the multi-note line
    # (else dst==src could be 0==0). The line is a downbeat accent + an ascending ramp.
    assert len(set(src)) >= 8 and src[0] != src[1], f"source line not read back: {src}"
    assert any(dst), "captured dst has no notes"
    assert dst[0] == src[0], (
        f"downbeat phase wrong: dst step 0 = {dst[0]}, source downbeat = {src[0]} "
        "(a one-step rotation would show here)"
    )
    # element-for-element over both captured bars: fidelity + phase + periodicity.
    # (transpose is 0 here, and force-to-scale is idempotent, so dst-mirror == src-mirror.)
    for s in range(32):
        assert dst[s] == src[s % 16], (
            f"tape fidelity: dst step {s} = {dst[s]}, expected source step {s % 16} = {src[s % 16]}\n"
            f"  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
def test_capture_while_playing_nonaligned_loop(board):
    """Approach A (2026-06-26): a NON-(whole-measure) track — here 24 steps = 1.5 global
    measures — is grabbable WHILE PLAYING via the tape's tick-period slice (P = spm*tps),
    even though its loop never lines up with a global-measure boundary. Grab k=1 (one
    24-step loop) and the static dst reproduces the forward line note-for-note on the
    correct phase. The stopped re-sim still refuses this length (see the pin below)."""
    board.reset()
    _setup_forward_line_len(board, 24)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)                       # > 1 full 24-step loop has played
        q = board.capture_ring_query()
        assert q["max_k"] >= 1, f"non-aligned track should expose >=1 grabbable loop while playing, got {q['max_k']}"
        status = board.capture_span(TRACK, 1, DST)       # one 24-step loop
        assert status == 0x01, f"while-playing non-aligned capture should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(24)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(24)]
    assert len(set(src)) >= 8 and src[0] != src[1], f"source line not read back: {src}"
    assert any(dst), "captured dst has no notes"
    for s in range(24):
        assert dst[s] == src[s], (
            f"non-aligned tape fidelity: dst step {s} = {dst[s]}, expected src {s} = {src[s]}\n"
            f"  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
def test_capture_while_playing_twobar_phase(board):
    """WHILE-PLAYING phase for a 2-bar (32-step) forward line: the tape just buckets the
    real emitted ticks (no re-sim drive), so the downbeat must land right. Grab k=1 (one
    32-step loop) and compare note-for-note. This isolates the tape-window phase for n=2
    from the stopped re-sim drive (which has a separate multi-bar phase issue)."""
    board.reset()
    _setup_forward_line_len(board, 32)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 5)                       # >= 2 full 32-step loops (n=2)
        status = board.capture_span(TRACK, 1, DST)       # one 2-bar loop
        assert status == 0x01, f"2-bar while-playing capture should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    src = [board.track_par_get(TRACK, NOTE_LAYER, 0, s) for s in range(32)]
    dst = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(32)]
    assert len(set(src)) >= 8 and src[0] != src[1], f"source line not read back: {src}"
    assert any(dst), "captured dst has no notes"
    assert dst[0] == src[0], (
        f"2-bar tape downbeat phase wrong: dst[0]={dst[0]} src[0]={src[0]}\n  src: {src}\n  dst: {dst}"
    )
    for s in range(32):
        assert dst[s] == src[s], (
            f"2-bar tape fidelity: dst[{s}]={dst[s]}, expected src[{s}]={src[s]}\n  src: {src}\n  dst: {dst}"
        )


@pytest.mark.hardware
def test_capture_nonaligned_stopped_refused(board):
    """The flip side of the pin above: while STOPPED, a non-(whole-measure) track refuses
    (0x18 = 0x10|8) — the re-sim drive phase-aligns to the global measure, so reproducing a
    non-aligned loop is the deferred A2 kernel. The LCD steers the user to 'play to grab'."""
    board.reset()
    _setup_forward_line_len(board, 24)
    # advance the (stopped) engine so the ring has history, then a stopped grab
    board.clock_step(16000)
    board.clock_step(16000)
    status = board.capture_span(TRACK, 1, DST)
    assert status == 0x18, f"stopped non-aligned should refuse 0x18 (play to grab), got {hex(status)}"


@pytest.mark.hardware
def test_capture_while_playing_refuses_wrong_track(board):
    """Refusals route through the dispatcher's tape path too: capturing a track the
    tape is not recording is refused (0x14 = 0x10|4) while playing."""
    board.reset()
    _setup_forward_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        status = board.capture_span(TRACK + 1, 1, DST)  # tape records TRACK, not TRACK+1
        assert status == 0x14, f"expected wrong-track refusal 0x14 while playing, got {hex(status)}"
    finally:
        board.transport(start=False)
