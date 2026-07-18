"""Capture-fidelity determination pins (2026-07-18 cont. 7) — the two-family LAW.

Every capture path belongs to one of two families with opposite bake contracts:

  FLATTEN (mirror copy — CaptureToTrack/ToSlot/ToSlotTrack, chord route): the
  deposit holds render-stack output ONLY, so deterministic emission shapers
  (echo, LFO, direction, Delay/Roll/Nth lanes, ...) are PRESERVED on the dst —
  re-applying them to the un-baked bytes reproduces the heard sound.

  TAPE (emission recording — live tape + stopped re-sim): the deposit holds
  what actually SOUNDED (repeats, LFO'd pitches, traversal, delays baked into
  the notes), so the same shapers are RESET on the dst (re-apply would double).

These pins turn the user's by-ear checks into byte assertions: CC readback for
the preserve/reset split, par-lane readback for the stream-lane copy and the
drum pitch bake, refusal codes for the chord fence. Full law:
seq_cc.c above SEQ_CC_ResetGenerativeCommon; session block: DECISIONS_LOG
2026-07-18 cont. 7.
"""

import time

import pytest

from harness import Board, CC

SRC = 0
DST = 2

SEQ_CC_LENGTH = 0x4D
TRKDIR_FORWARD = 0
TRKDIR_PINGPONG = 2
TRKDIR_RANDOM_STEP = 5
EVENT_MODE_NOTE = 0
EVENT_MODE_CHORD = 1
PAR_TYPE_CC = 5
PAR_TYPE_ROLL = 9
LAY_CONST_A4 = 0x03  # par layer D (index 3) type assignment
LAY_CONST_B4 = 0x13  # par layer D constant row B (CC number for a CC-type layer)

MEASURE_TICKS = 16 * 96
SETTLE = 0.10


def _drive_measures(board: Board, measures: int) -> None:
    """Advance the (stopped) engine in clock_step chunks so the ring fills."""
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
    raise AssertionError(f"ring only reached depth {depth} (< {target}) — transport not advancing?")


def _setup_note_line(board: Board) -> None:
    """Static melodic line on SRC: track_note_init layout (0=Note 1=Vel 2=Len
    3=Roll), 16 steps, gate every step, recognizable note ramp."""
    board.track_note_init(SRC)
    board.cc_set(SRC, SEQ_CC_LENGTH, 15)
    board.cc_set(SRC, CC.DIRECTION, TRKDIR_FORWARD)
    board.ui_track_set(SRC)
    board.trg_byte_set(SRC, 0, 0xFF)
    board.trg_byte_set(SRC, 1, 0xFF)
    for step in range(16):
        board.track_par_set(SRC, 0, 0, step, 48 + step)
        board.track_par_set(SRC, 1, 0, step, 100)
        board.track_par_set(SRC, 2, 0, step, 71)
    time.sleep(SETTLE)


def _arm_shapers(board: Board, direction: int = TRKDIR_PINGPONG) -> None:
    """Arm the deterministic emission shapers the law splits on."""
    board.cc_set(SRC, CC.ECHO_REPEATS, 3)
    board.cc_set(SRC, CC.ECHO_DELAY, 10)
    board.cc_set(SRC, CC.ECHO_VELOCITY, 40)
    board.cc_set(SRC, CC.LFO_WAVEFORM, 2)
    board.cc_set(SRC, CC.LFO_AMPLITUDE, 90)
    board.cc_set(SRC, CC.DIRECTION, direction)


@pytest.mark.hardware
def test_flatten_preserves_deterministic_shapers(board):
    """FLATTEN half of the law: a stopped CaptureToTrack keeps echo/LFO/direction
    CCs AND the Roll lane assignment on the frozen copy (they were never baked
    into the mirror — the old blanket reset made every stopped freeze come out
    drier/straighter than the source), while the notes stay byte-identical."""
    board.reset()
    _setup_note_line(board)
    _arm_shapers(board, TRKDIR_PINGPONG)

    assert board.capture_to_track(SRC, DST), "capture_to_track failed"

    # deterministic shaping survives the flatten
    assert board.cc_get(DST, CC.ECHO_REPEATS) == 3, "echo repeats stripped by flatten"
    assert board.cc_get(DST, CC.ECHO_DELAY) == 10, "echo delay stripped by flatten"
    assert board.cc_get(DST, CC.ECHO_VELOCITY) == 40, "echo velocity stripped by flatten"
    assert board.cc_get(DST, CC.LFO_WAVEFORM) == 2, "LFO waveform stripped by flatten"
    assert board.cc_get(DST, CC.LFO_AMPLITUDE) == 90, "LFO amplitude stripped by flatten"
    assert board.cc_get(DST, CC.DIRECTION) == TRKDIR_PINGPONG, "deterministic direction reset by flatten"
    assert board.cc_get(DST, LAY_CONST_A4) == PAR_TYPE_ROLL, "Roll lane assignment stripped by flatten"

    # and the content is still the lossless mirror copy
    for s in range(16):
        assert board.track_par_get(DST, 0, 0, s) == 48 + s, f"note mismatch at step {s}"


@pytest.mark.hardware
def test_flatten_resets_random_direction(board):
    """Generation stays reset in BOTH flavors: a Random_Step source flattens to
    Forward (live RNG draws can't be 'preserved deterministically')."""
    board.reset()
    _setup_note_line(board)
    board.cc_set(SRC, CC.DIRECTION, TRKDIR_RANDOM_STEP)

    assert board.capture_to_track(SRC, DST), "capture_to_track failed"
    assert board.cc_get(DST, CC.DIRECTION) == TRKDIR_FORWARD, (
        "Random_Step must reset to Forward on the frozen copy (generation axis)"
    )


@pytest.mark.hardware
@pytest.mark.timeout(40)
def test_tape_resets_deterministic_shapers(board):
    """TAPE half of the law (the contrast pin): the SAME armed shapers reset on a
    while-playing grab — echo repeats / LFO'd pitches / traversal are already in
    the recorded notes, so a kept dial would double-apply."""
    board.reset()
    _setup_note_line(board)
    _arm_shapers(board, TRKDIR_FORWARD)  # forward: keep the taped content simple
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        status = board.capture_span(SRC, 1, DST)
        assert status == 0x01, f"while-playing grab should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    assert board.cc_get(DST, CC.ECHO_REPEATS) == 0, "tape dst must reset echo (baked as notes)"
    assert board.cc_get(DST, CC.LFO_WAVEFORM) == 0, "tape dst must reset LFO (baked into pitches)"
    assert board.cc_get(DST, CC.DIRECTION) == TRKDIR_FORWARD, "tape dst plays forward (traversal baked)"


@pytest.mark.hardware
@pytest.mark.timeout(40)
def test_tape_copies_cc_lane(board):
    """Stream lanes reach tape deposits: the tee hears NOTE events only, so a
    painted CC lane used to arrive memset-0 (curve lost + spurious CC-0). Now
    SEQ_CORE_CaptureCopyStreamLanes copies it from the source mirror."""
    board.reset()
    _setup_note_line(board)
    board.cc_set(SRC, LAY_CONST_A4, PAR_TYPE_CC)  # layer D: Roll -> CC type
    board.cc_set(SRC, LAY_CONST_B4, 74)           # a real CC number so the lane is live
    for s in range(16):
        board.track_par_set(SRC, 3, 0, s, 20 + s)  # painted curve
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        status = board.capture_span(SRC, 1, DST)
        assert status == 0x01, f"while-playing grab should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    assert board.cc_get(DST, LAY_CONST_A4) == PAR_TYPE_CC, "CC lane assignment must survive (step-data carrier)"
    got = [board.track_par_get(DST, 3, 0, s) for s in range(16)]
    assert got == [20 + s for s in range(16)], (
        f"tape deposit must carry the painted CC curve (GRID rotation 0): {got}"
    )


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_resim_copies_cc_lane(board):
    """Same stream-lane contract on the STOPPED re-sim path (the sink hears note
    events only too)."""
    board.reset()
    _setup_note_line(board)
    board.cc_set(SRC, LAY_CONST_A4, PAR_TYPE_CC)
    board.cc_set(SRC, LAY_CONST_B4, 74)
    for s in range(16):
        board.track_par_set(SRC, 3, 0, s, 20 + s)
    _drive_measures(board, 4)  # fill the ring while stopped

    status = board.capture_span(SRC, 1, DST)
    assert status == 0x01, f"stopped re-sim grab should succeed, got {hex(status)}"

    got = [board.track_par_get(DST, 3, 0, s) for s in range(16)]
    assert got == [20 + s for s in range(16)], (
        f"re-sim deposit must carry the painted CC curve: {got}"
    )


# --- drum pitch bake ---------------------------------------------------------
# A real playable kit (track_drum_kit_init: Note+Length lanes, per-drum gates).
# Distinct gates per drum + a static Semi transpose = a deterministic "drum
# pitch shift" whose capture can be asserted byte-for-byte.
DRUM_GATES = {  # drum -> (trg byte0, trg byte1) = steps 0..7, 8..15
    0: (0x11, 0x11),  # kick: 0,4,8,12
    2: (0x04, 0x04),  # hat:  2,10
    5: (0x20, 0x20),  # tom:  5,13
}
SEMI = 2
SRC_LEN_PAR = 71  # length lane value -> gate 3/4 step; round-trips exactly at tps=24


def _gated_steps(b0: int, b1: int):
    return [s for s in range(16) if ((b0 | (b1 << 8)) >> s) & 1]


def _setup_kit(board: Board) -> None:
    board.track_drum_kit_init(SRC)
    board.ui_track_set(SRC)
    for drum, (b0, b1) in DRUM_GATES.items():
        board.trg_byte_set(SRC, 0, b0, 0, drum)
        board.trg_byte_set(SRC, 1, b1, 0, drum)
        for s in _gated_steps(b0, b1):
            board.track_par_set(SRC, 1, drum, s, SRC_LEN_PAR)  # length lane
    board.cc_set(SRC, CC.TRANSPOSE_SEMI, SEMI)
    time.sleep(SETTLE)


@pytest.mark.hardware
@pytest.mark.timeout(40)
def test_drum_tape_bakes_pitch_into_note_lane(board):
    """THE drum determination pin: a kit with a Note par lane captures its HEARD
    pitch while playing. The taped notes (config 36+d, Semi'd at emission) land
    in the dst kit's Note lane per (step, instrument); the dst's Semi travel is
    UNDONE (the lane carries the pitch — a kept Semi would double-transpose);
    gates land per drum (the per-instrument write-back); the taped gate length
    round-trips into the Length lane."""
    board.reset()
    _setup_kit(board)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        status = board.capture_span(SRC, 1, DST)
        assert status == 0x01, f"drum tape grab should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    # the lane carries the pitch; the static transpose must NOT travel with it
    assert board.cc_get(DST, CC.TRANSPOSE_SEMI) == 0, (
        "dst Semi must be zeroed when the Note lane carries the baked pitch (double-apply guard)"
    )
    assert board.cc_get(SRC, CC.TRANSPOSE_SEMI) == SEMI, "capture must not disturb the source"

    for drum, (b0, b1) in DRUM_GATES.items():
        # per-drum gates survived (the 2026-07-18 per-instrument write-back)
        got_b, _ = board.trg_byte_get(DST, 0, drum, 0, 2)
        assert got_b[0] == b0 and got_b[1] == b1, (
            f"drum {drum} gates: got {got_b[0]:#04x},{got_b[1]:#04x} want {b0:#04x},{b1:#04x}"
        )
        for s in _gated_steps(b0, b1):
            note = board.track_drum_par_get(DST, drum, s)
            assert note == 36 + drum + SEMI, (
                f"drum {drum} step {s}: note lane {note}, want heard pitch {36 + drum + SEMI}"
            )
            lenv = board.track_par_get(DST, 1, drum, s)
            assert lenv == SRC_LEN_PAR, (
                f"drum {drum} step {s}: length lane {lenv}, want round-tripped {SRC_LEN_PAR}"
            )
    # an ungated (step, drum) stays empty — no smear across instruments
    assert board.track_drum_par_get(DST, 0, 1) == 0, "ungated step should stay empty"
    assert board.track_drum_par_get(DST, 3, 0) == 0, "unused drum should stay empty"


@pytest.mark.hardware
@pytest.mark.timeout(40)
def test_drum_tape_attribution_octave_fold(board):
    """Regression pin for the 2026-07-18 hardware A/B find (user's T5 kit):
    a kit transposed BELOW note 0 emits the OCTAVE-FOLDED pitch live
    (SEQ_CORE_TrimNote), but the grab's expected-note table hard-clamped to 0 —
    no exact match, so that drum's hits nearest-merged onto a neighbor (the
    '11 became part of the 9 drum' merge). With the fold in the table, every
    drum keeps its own row and the folded pitch lands in the Note lane."""
    board.reset()
    _setup_kit(board)
    board.cc_set(SRC, CC.TRANSPOSE_SEMI, 15)   # -1 (sign nibble)
    board.cc_set(SRC, CC.TRANSPOSE_OCT, 13)    # -3 -> shift -37: drum0 36-37 = -1 -> folds to 11
    time.sleep(SETTLE)
    try:
        board.transport(start=True)
        _wait_ring_depth(board, 3)
        status = board.capture_span(SRC, 1, DST)
        assert status == 0x01, f"folded-kit tape grab should succeed, got {hex(status)}"
    finally:
        board.transport(start=False)

    expect = {0: 11, 2: (36 + 2) - 37, 5: (36 + 5) - 37}  # drum0 folds -1 -> 11; others in range
    for drum, (b0, b1) in DRUM_GATES.items():
        got_b, _ = board.trg_byte_get(DST, 0, drum, 0, 2)
        assert got_b[0] == b0 and got_b[1] == b1, (
            f"folded kit: drum {drum} gates merged/moved: got {got_b[0]:#04x},{got_b[1]:#04x} "
            f"want {b0:#04x},{b1:#04x} (the old clamp merged the folded drum onto a neighbor)"
        )
        for s in _gated_steps(b0, b1):
            note = board.track_drum_par_get(DST, drum, s)
            assert note == expect[drum], (
                f"folded kit: drum {drum} step {s}: note lane {note}, want {expect[drum]}"
            )


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_drum_resim_per_instrument(board):
    """STOPPED grabs of a kit used to collapse every drum onto instrument 0
    (the re-sim sink was melodic-mono by construction — the user's Capture-page
    grab with transport off hit exactly this). The sink now maps events per
    instrument via the same expected-note table as the tape: gates per drum,
    pitch in the Note lane (Semi undone), measured length in the Length lane."""
    board.reset()
    _setup_kit(board)
    _drive_measures(board, 4)

    status = board.capture_span(SRC, 1, DST)
    assert status == 0x01, f"stopped drum re-sim grab should succeed, got {hex(status)}"

    assert board.cc_get(DST, CC.TRANSPOSE_SEMI) == 0, "dst Semi must be zeroed (Note lane carries pitch)"
    for drum, (b0, b1) in DRUM_GATES.items():
        got_b, _ = board.trg_byte_get(DST, 0, drum, 0, 2)
        assert got_b[0] == b0 and got_b[1] == b1, (
            f"re-sim drum {drum} gates: got {got_b[0]:#04x},{got_b[1]:#04x} want {b0:#04x},{b1:#04x} "
            "(instrument-0 collapse?)"
        )
        for s in _gated_steps(b0, b1):
            note = board.track_drum_par_get(DST, drum, s)
            assert note == 36 + drum + SEMI, (
                f"re-sim drum {drum} step {s}: note lane {note}, want {36 + drum + SEMI}"
            )
            lenv = board.track_par_get(DST, 1, drum, s)
            assert lenv == SRC_LEN_PAR, (
                f"re-sim drum {drum} step {s}: length lane {lenv}, want {SRC_LEN_PAR}"
            )
    # nothing smeared onto unused rows
    assert board.track_drum_par_get(DST, 1, 0) == 0, "unused drum row should stay empty"


@pytest.mark.hardware
def test_chord_source_ram_grab_refused(board):
    """Chord fence: chord steps store INDICES — the note-stream materialize can't
    round-trip them, so the RAM grab refuses -13 (0x1D) instead of depositing
    gates over an all-zero chord lane. The pattern-capture verb keeps the
    faithful chord-index route (test_capture_recorder_to_slot.py)."""
    board.reset()
    board.track_note_init(SRC)
    board.cc_set(SRC, CC.EVENT_MODE, EVENT_MODE_CHORD)
    board.ui_track_set(SRC)

    status = board.capture_span(SRC, 1, DST)
    assert status == 0x1D, f"chord source must refuse -13 (0x1D), got {hex(status)}"
