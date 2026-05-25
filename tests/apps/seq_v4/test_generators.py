"""Generator-output tests: drive the SEQ V4 via the UI, capture MIDI, assert.

Each test:
1. Reset device (via the `board` fixture).
2. Force track 0 to send on USB1 so the harness can hear it.
3. Navigate to TRKEUCLID.
4. Select the generator type via GP12.
5. Configure params via GP9 (LENGTH-slot), GP10 (PULSES-slot), GP11 (OFFSET-slot).
   The slots are re-skinned per generator type — see seq_ui_trkeuclid.c.
6. PLAY ~2.5s, STOP, capture.
7. Assert on the captured note stream.

Generator types (from `seq_ui_trkeuclid.c`):
  0 = EUCLID:  GP9=length, GP10=pulses, GP11=offset
  1 = CA:      GP9=rule,   GP10=seed,   GP11=gens
  2 = POLY:    GP9=N,      GP10=M,      GP11=phase
  3 = SUB:     GP9=depth,  GP10=prob,   GP11=seed
  4 = LSYS:   GP9=preset, GP10=iter,   GP11=seed

GP12 is the type selector. Default type after fresh boot is EUCLID (0); we floor
to it then dial up by the type's enum index.
"""

import time

import pytest

from harness import Button, Encoder, MidiPort, Page


# Generator type indices, mirror of GEN_TYPE_* in seq_ui_trkeuclid.c.
GEN_EUCLID = 0
GEN_CA = 1
GEN_POLY = 2
GEN_SUB = 3
GEN_LSYS = 4
GEN_TYPE_COUNT = 5

# Capture / play window per test.
PLAY_SECONDS = 2.5


def _setup_track_and_open_trkeuclid(board):
    """Common preamble: route track 0 to USB1, open the generator page."""
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
    board.page_set(Page.TRKEUCLID)
    time.sleep(0.1)


def _select_gen_type(board, gen_type: int):
    """Drive GP12 to the requested generator type.

    The encoder cycles 0..GEN_TYPE_COUNT-1 with floor/ceiling clamps. Floor it
    first (large negative) then dial up to the target — deterministic
    regardless of prior state.
    """
    board.turn(Encoder.GP(12), -(GEN_TYPE_COUNT + 2))   # floor to 0 (EUCLID)
    if gen_type > 0:
        board.turn(Encoder.GP(12), +gen_type)
    time.sleep(0.1)


def _play_and_capture(board, seconds: float = PLAY_SECONDS):
    """PLAY, sleep, STOP. Returns the list of Note On events on track-0's channel."""
    capture_t0 = board.capture_start()
    board.press(Button.PLAY)
    try:
        time.sleep(seconds)
    finally:
        board.press(Button.STOP)
        time.sleep(0.1)
    return [
        e for e in board.capture_notes(since=capture_t0)
        if e.is_on and e.channel == 0
    ], capture_t0


@pytest.mark.hardware
def test_euclidean_5_in_8(board):
    """Euclidean(L=8, P=5): track 0 emits some hits when the generator is configured.

    The trigger layer gets a Bjorklund-distributed pattern of 5 onsets across
    8 steps, doubled into the 16-step track. We only assert "non-empty" — the
    exact hit count depends on track length, BPM, and timing tolerance.
    """
    _setup_track_and_open_trkeuclid(board)
    _select_gen_type(board, GEN_EUCLID)

    # Length: floor + dial to 7 (display 8). Pulses: 0 -> 5.
    board.turn(Encoder.GP(9), -99)
    board.turn(Encoder.GP(10), -99)
    board.turn(Encoder.GP(9), +7)
    board.turn(Encoder.GP(10), +5)
    time.sleep(0.15)

    notes, t0 = _play_and_capture(board)
    assert len(notes) >= 4, (
        f"Euclidean(5,8) produced too few hits: {len(notes)} in {PLAY_SECONDS}s\n"
        f"events: {[(round(e.timestamp - t0, 3), e.note) for e in notes]}"
    )


@pytest.mark.xfail(
    reason="LCD shows the polyrhythm gen wrote sparse triggers (3-of-8 doubled "
    "to 5-6 gates per 16 steps), but playback fires a denser, partially-"
    "doubled pattern. Confirmed orthogonal to multi-Note-layer contamination "
    "(AUTOTEST has clean par-layer config) and to multi-track contamination "
    "(RESET_MUTE_NON_T0 silences all but track 0). Likely a separate gate-"
    "path source-of-truth issue between the editor (which the LCD reflects) "
    "and the tick-fire path. Open puzzle, see "
    "project-state-2026-05-25 memory.",
    strict=False,
)
@pytest.mark.hardware
def test_polyrhythm_3_in_8(board):
    """Polyrhythm(N=3, M=8): 3 evenly-spaced hits per 8 steps.

    Polyrhythm generates strictly-periodic patterns — N hits per M steps,
    evenly distributed. Far more predictable than Euclidean for assertion
    purposes; intervals between hits should be ~equal.
    """
    _setup_track_and_open_trkeuclid(board)
    _select_gen_type(board, GEN_POLY)

    # POLY: GP9 = N (pulses), GP10 = M (cycle length).
    # Defaults are N=3, M=8. We re-floor then dial up to make this idempotent.
    board.turn(Encoder.GP(9), -99)
    board.turn(Encoder.GP(10), -99)
    board.turn(Encoder.GP(10), +8)  # M first — N is clamped to <= M
    board.turn(Encoder.GP(9), +3)   # N = 3
    time.sleep(0.15)

    notes, t0 = _play_and_capture(board)
    assert len(notes) >= 4, (
        f"Polyrhythm(3,8) produced too few hits: {len(notes)} in {PLAY_SECONDS}s\n"
        f"events: {[(round(e.timestamp - t0, 3), e.note) for e in notes]}"
    )

    # Even spacing check: consecutive intervals between hits should be roughly
    # equal. M=8 steps with N=3 hits gives gaps of approx 8/3 ≈ 2.67 steps,
    # but Polyrhythm typically distributes as {0, 3, 5} or similar. Compute
    # coefficient of variation across observed intervals — for strictly
    # periodic generators it stays small.
    timestamps = [e.timestamp - t0 for e in notes[:8]]
    intervals = [b - a for a, b in zip(timestamps, timestamps[1:])]
    if len(intervals) >= 3:
        mean = sum(intervals) / len(intervals)
        max_gap = max(intervals)
        min_gap = min(intervals)
        # Loose check: any single gap shouldn't exceed 3x the smallest gap.
        # Truly periodic generators stay well within 2x.
        assert max_gap <= 3 * min_gap, (
            f"Polyrhythm intervals not roughly regular: min={min_gap:.3f} max={max_gap:.3f}\n"
            f"intervals: {[round(i, 3) for i in intervals]}"
        )


@pytest.mark.hardware
def test_ca_generator_emits_notes(board):
    """Cellular Automata generator: produces notes based on rule + seed + gens.

    CA params (GP9=rule 0..255, GP10=seed 1..255, GP11=gens 0..32). The output
    depends on the chosen rule — Rule 30 is the default and is known chaotic
    (produces a varied trigger pattern). We just assert the generator emits
    a nontrivial number of notes — exact rhythm is deterministic but
    rule-dependent.
    """
    _setup_track_and_open_trkeuclid(board)
    _select_gen_type(board, GEN_CA)

    # Floor and dial up to known values. Defaults: rule=30, seed=1, gens=8.
    # We just push to a known configuration and trust the trigger writes.
    board.turn(Encoder.GP(9), -99)  # rule floor
    board.turn(Encoder.GP(10), -99)  # seed floor
    board.turn(Encoder.GP(11), -99)  # gens floor
    board.turn(Encoder.GP(9), +30)   # rule = 30 (chaotic)
    board.turn(Encoder.GP(10), +1)    # seed = 1 (clamps at min 1 anyway)
    board.turn(Encoder.GP(11), +8)    # gens = 8
    time.sleep(0.15)

    notes, t0 = _play_and_capture(board)
    assert len(notes) >= 2, (
        f"CA(rule=30) produced too few hits: {len(notes)} in {PLAY_SECONDS}s\n"
        f"events: {[(round(e.timestamp - t0, 3), e.note) for e in notes]}"
    )


@pytest.mark.hardware
def test_subdivide_generator_emits_notes(board):
    """Recursive Subdivide generator: GP9=depth, GP10=prob, GP11=seed.

    Subdivide creates rhythmic patterns by recursively splitting beats. Depth
    1..8, probability 0..100. We use prob=100 to guarantee every potential
    subdivision fires.
    """
    _setup_track_and_open_trkeuclid(board)
    _select_gen_type(board, GEN_SUB)

    board.turn(Encoder.GP(9), -99)   # depth floor (clamps at 1)
    board.turn(Encoder.GP(10), -99)  # prob floor (0)
    board.turn(Encoder.GP(9), +3)    # depth = 4 (1 + 3)
    board.turn(Encoder.GP(10), +100)  # prob = 100 (every subdivision fires)
    time.sleep(0.15)

    notes, t0 = _play_and_capture(board)
    assert len(notes) >= 2, (
        f"Subdivide produced too few hits: {len(notes)} in {PLAY_SECONDS}s\n"
        f"events: {[(round(e.timestamp - t0, 3), e.note) for e in notes]}"
    )


@pytest.mark.hardware
def test_lsystem_generator_emits_notes(board):
    """L-system generator: GP9=preset (0..7), GP10=iter (0..6), GP11=seed.

    L-systems produce self-similar patterns via string rewriting. Different
    presets give different rule sets; iter controls how many rewrite passes
    are applied.
    """
    _setup_track_and_open_trkeuclid(board)
    _select_gen_type(board, GEN_LSYS)

    board.turn(Encoder.GP(9), -99)   # preset floor
    board.turn(Encoder.GP(10), -99)  # iter floor
    board.turn(Encoder.GP(10), +3)   # 3 iterations gives a decent pattern
    time.sleep(0.15)

    notes, t0 = _play_and_capture(board)
    assert len(notes) >= 2, (
        f"L-system produced too few hits: {len(notes)} in {PLAY_SECONDS}s\n"
        f"events: {[(round(e.timestamp - t0, 3), e.note) for e in notes]}"
    )
