"""Retroactive CAPTURE — PRECISE GATE LENGTH (2026-06-20).

The first capture cut wrote a fixed ~3/4 gate (length value 71) for every grabbed
note. Now both capture paths reproduce the ACTUAL articulation. The engine emits
every note-off as a running-status (velocity-0) package on the same cable, so:
  - WHILE PLAYING the live-tape tap back-fills each note-on's gate (off.tick - on.tick);
  - STOPPED the re-sim sink pairs each emitted off with its still-open on.
SEQ_CORE_CaptureGateToParLen inverts the playback formula (gate = tps*len/96) back to a
stored length value.

These pins need a melodic track WITH a length layer — track_drum_init gives only a single
Note layer — so they use the new track_note_init (Note/Vel/Length/Roll). The source line
has a DISTINCT per-step gate gradient (16..91, every value < 96 so each note's gate is
under one step: no glide, no cross-bar tie => the clean precise-gate case). A capture that
regressed to the old fixed 71 would collapse the gradient and fail. tps is 96 ticks/step
here (the harness measure math), so the round-trip is near-exact; a small tolerance covers
integer rounding.
"""

import time

import pytest

from harness import Board

TRACK = 0
DST = 2
NOTE_LAYER = 0
VEL_LAYER = 1
LEN_LAYER = 2

SEQ_CC_LENGTH = 0x4D
SEQ_CC_DIRECTION = 0x48
TRKDIR_FORWARD = 0
MEASURE_TICKS = 16 * 96  # steps_per_measure(16) * ticks_per_16th(96) => tps = 96


def _gate_gradient() -> list[int]:
    """16 distinct per-step length values, 16..91 — all < 96 so each note's gate is under
    one step (no glide, no cross-bar tie => the clean precise-gate case)."""
    return [16 + s * 5 for s in range(16)]


def _setup_gated_line(board: Board) -> list[int]:
    """A fixed forward melodic line (no generator => every bar identical) on a Note track
    with Note/Velocity/Length layers, each step a distinct gate length. Returns the per-
    step source length values for the round-trip comparison."""
    lengths = _gate_gradient()
    board.track_note_init(TRACK)                      # Note/Vel/Length/Roll layers
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)            # 16 steps = one whole measure
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)                         # ring follows the visible track
    board.trg_byte_set(TRACK, 0, 0xFF)               # gate steps 0..7
    board.trg_byte_set(TRACK, 1, 0xFF)               # gate steps 8..15
    for s in range(16):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, 48 + s)   # distinct ascending notes
        board.track_par_set(TRACK, VEL_LAYER, 0, s, 100)       # audible (vel 0 => no note)
        board.track_par_set(TRACK, LEN_LAYER, 0, s, lengths[s])
    return lengths


def _drive_measures(board: Board, measures: int) -> None:
    """Advance the STOPPED engine `measures` measures so the ring fills (re-sim path)."""
    remaining = measures * MEASURE_TICKS
    while remaining > 0:
        chunk = min(remaining, 16000)
        board.clock_step(chunk)
        remaining -= chunk


def _wait_ring_depth(board: Board, target: int, timeout: float = 25.0) -> int:
    deadline = time.monotonic() + timeout
    depth = 0
    while time.monotonic() < deadline:
        depth = board.capture_ring_query()["depth"]
        if depth >= target:
            return depth
        time.sleep(0.25)
    raise AssertionError(f"ring only reached depth {depth} (< {target}) — transport stalled?")


def _assert_gates(dst_len: list[int], lengths: list[int], allow_rotation: bool = False) -> None:
    """The captured length layer must reproduce the source gradient, not the old fixed
    default. Guards: periodic (both captured bars identical), distinct (the old hardcode
    collapsed every step to one value), not-all-71 (the specific old default), and per-step
    ~match to the gradient (fidelity + non-vacuous — a failed source setup would make dst
    the default, not the gradient).

    allow_rotation: the re-sim reproduces what actually played at each measure boundary, so
    its step phase follows the track's alignment to the global measure (transport-start
    aligns it; a clock_step drive from an unreset step counter does not). Precise gate is
    about the VALUES, so the re-sim check accepts any cyclic rotation of the gradient;
    absolute phase is pinned by the transport-aligned while-playing test."""
    n = len(lengths)
    bar = dst_len[:n]
    assert dst_len[n:2 * n] == bar, f"the two captured bars differ (non-periodic): {dst_len}"
    assert len(set(bar)) >= 8, f"captured gates not distinct — regressed to a default? {bar}"
    assert not all(v == 71 for v in bar), "gates regressed to the fixed default 71"
    candidates = ([lengths[r:] + lengths[:r] for r in range(n)] if allow_rotation else [lengths])
    assert any(all(abs(a - b) <= 3 for a, b in zip(bar, rot)) for rot in candidates), (
        f"captured gates don't match the source gradient"
        f"{' (any rotation)' if allow_rotation else ''}:\n  bar:{bar}\n  src:{lengths}")


@pytest.mark.hardware
def test_capture_precise_gate_resim(board):
    """STOPPED grab (re-sim): the sink pairs each emitted note-off with its open note-on
    and writes the measured length. dst reproduces the source's per-step gate gradient
    (K=2 => the gradient twice), not the old fixed 71."""
    board.reset()
    lengths = _setup_gated_line(board)
    _drive_measures(board, 4)                            # fill the ring while stopped
    assert board.capture_span(TRACK, 2, DST) == 0x01, "stopped capture should re-sim & succeed"
    dst_len = [board.track_par_get(DST, LEN_LAYER, 0, s) for s in range(32)]
    _assert_gates(dst_len, lengths, allow_rotation=True)  # gate values; phase follows alignment


@pytest.mark.hardware
def test_capture_precise_gate_while_playing(board):
    """PLAYING grab (live tape): the tap back-fills each note-on's gate from its running-
    status note-off. dst reproduces the source's per-step gate gradient, not the old
    fixed 71."""
    board.reset()
    lengths = _setup_gated_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        assert board.capture_span(TRACK, 2, DST) == 0x01, "while-playing capture should tape & succeed"
    finally:
        board.transport(start=False)
    dst_len = [board.track_par_get(DST, LEN_LAYER, 0, s) for s in range(32)]
    _assert_gates(dst_len, lengths)  # transport-aligned => exact phase pinned here


# --- GLIDE / tie capture ----------------------------------------------------------------
# A glide note doesn't emit a clean note-off: the off is deferred (scheduled at 0xffffffff)
# and only rescheduled when the tie breaks at the NEXT note. So a glide is captured as the
# gate spanning to that next note (~one full step => length 96 = tie). The window's LAST
# note ties past the window, so its off never drains in-window — the re-sim must mark a
# note still sustaining at window end as a glide instead of the default gate.
GLIDE_TRG_LAYER = 3  # ASG_GLIDE -> trg layer 3 on a default Note track
GLIDE_PAR_VALUE = 95  # stored 95 -> len 96 = tie


def _setup_glide_line(board: Board) -> None:
    """A forward line with a GLIDE TRIGGER on every step (the legato/portamento case),
    normal length values otherwise — so the captured length must come back as glide on
    every step, including the window's last (the tail-tie the re-sim must not drop)."""
    board.track_note_init(TRACK)
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)
    board.trg_byte_set(TRACK, 0, 0xFF)                              # gate steps 0..7
    board.trg_byte_set(TRACK, 1, 0xFF)                             # gate steps 8..15
    board.trg_byte_set(TRACK, 0, 0xFF, trg_layer=GLIDE_TRG_LAYER)   # glide steps 0..7
    board.trg_byte_set(TRACK, 1, 0xFF, trg_layer=GLIDE_TRG_LAYER)   # glide steps 8..15
    for s in range(16):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, 48 + s)
        board.track_par_set(TRACK, VEL_LAYER, 0, s, 100)
        board.track_par_set(TRACK, LEN_LAYER, 0, s, 40)            # glide comes from the trigger


def _assert_all_glide(dst_len: list[int]) -> None:
    # uniform glide value => rotation-invariant, so no phase concern. The pre-fix bug left
    # the last step at the default (71); this guards that and the not-a-glide collapse.
    assert all(v == GLIDE_PAR_VALUE for v in dst_len), (
        f"every captured step should be a glide ({GLIDE_PAR_VALUE}=tie), incl. the window's "
        f"last (the tail-tie); got {dst_len}")


@pytest.mark.hardware
def test_capture_glide_resim(board):
    """STOPPED grab (re-sim): a glide-triggered line captures as a tie on every step,
    including the last — whose tie reaches past the window (its off is deferred), so the
    re-sim marks the still-open note as a glide rather than the default gate."""
    board.reset()
    _setup_glide_line(board)
    _drive_measures(board, 4)
    assert board.capture_span(TRACK, 2, DST) == 0x01, "stopped capture should re-sim & succeed"
    dst_len = [board.track_par_get(DST, LEN_LAYER, 0, s) for s in range(32)]
    _assert_all_glide(dst_len)


@pytest.mark.hardware
def test_capture_glide_while_playing(board):
    """PLAYING grab (live tape): the same glide line captures as a tie on every completed-
    bar step (each glide's off ties into the next bar, recorded before the grab)."""
    board.reset()
    _setup_glide_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        assert board.capture_span(TRACK, 2, DST) == 0x01, "while-playing capture should tape & succeed"
    finally:
        board.transport(start=False)
    dst_len = [board.track_par_get(DST, LEN_LAYER, 0, s) for s in range(32)]
    _assert_all_glide(dst_len)


# --- MULTI-STEP notes (length chained across steps) --------------------------------------
# A hand-drawn note longer than one step is a CHAIN: the player maxes a step's length (Gld)
# and carries the note + length onto the following steps, ending with a partial-length tail.
# A captured >1-step note must reproduce that chain, not collapse to a single Gld start step
# (which would just tie to the next note and lose the real duration). Source here: note 60
# carried across steps 0-3 (gate on step 0 only), lengths Gld/Gld/Gld/47 -> sounds ~3.5
# steps; the capture must come back as note 60 on steps 0-3, length 95/95/95/~46, gate only
# on step 0.
GLD = 95


def _setup_multistep_line(board: Board) -> None:
    board.track_note_init(TRACK)
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(TRACK)
    board.trg_byte_set(TRACK, 0, 0x01)   # gate only step 0 (the note start)
    board.trg_byte_set(TRACK, 1, 0x00)
    notes = [60, 60, 60, 60] + [0] * 12          # note carried across the covered steps
    lens = [GLD, GLD, GLD, 47] + [40] * 12        # Gld chain + a ~half-step tail at step 3
    for s in range(16):
        board.track_par_set(TRACK, NOTE_LAYER, 0, s, notes[s])
        board.track_par_set(TRACK, VEL_LAYER, 0, s, 100)
        board.track_par_set(TRACK, LEN_LAYER, 0, s, lens[s])


@pytest.mark.hardware
def test_capture_multistep_note_while_playing(board):
    """PLAYING grab (live tape): a >1-step note (Gld chain + tail) must be reproduced as
    the multi-step length chain, not collapsed to a single Gld step. The captured note
    spans steps 0-3 with the carried Gld run and a fractional tail."""
    board.reset()
    _setup_multistep_line(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 4)
        assert board.capture_span(TRACK, 1, DST) == 0x01, "while-playing capture should tape & succeed"
    finally:
        board.transport(start=False)
    note = [board.track_par_get(DST, NOTE_LAYER, 0, s) for s in range(16)]
    leng = [board.track_par_get(DST, LEN_LAYER, 0, s) for s in range(16)]
    vel = [board.track_par_get(DST, VEL_LAYER, 0, s) for s in range(16)]
    lb, _o = board.trg_byte_get(DST, 0, 0, 0, 2)
    gate = [(lb[b // 8] >> (b % 8)) & 1 for b in range(16)]
    # the note is carried across steps 0-3
    assert note[0] == 60 and note[1] == 60 and note[2] == 60 and note[3] == 60, (
        f"multi-step note not carried across its steps: {note}")
    # the velocity is carried across the covered steps too (not left at 0)
    assert vel[0] == 100 and vel[1] == 100 and vel[2] == 100 and vel[3] == 100, (
        f"velocity must be carried across the covered steps, not 0: {vel}")
    # gate only on the start step (carried steps are ungated ties, not re-triggers)
    assert gate[0] == 1 and gate[1] == 0 and gate[2] == 0 and gate[3] == 0, (
        f"only the start step should be gated: {gate}")
    # full steps tie at Gld; the final step is the fractional tail (< Gld) that ends it
    assert leng[0] == GLD and leng[1] == GLD and leng[2] == GLD, (
        f"carried steps must be Gld(95): {leng}")
    assert 40 <= leng[3] <= 52, f"tail step should be ~47 (the half-step remainder): {leng}"
    # the duration must NOT have collapsed to a single step (the pre-fix bug)
    assert not (leng[1] == 0 and leng[2] == 0), (
        f"multi-step note collapsed to a single start step (carried steps empty): {leng}")
