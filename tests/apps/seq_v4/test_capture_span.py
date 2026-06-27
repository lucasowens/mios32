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
    """Driving the stopped engine fills the ring; depth caps at 17 (the live bar +
    16 grabbable prior, discard-oldest) and the ring records the visible track."""
    board.reset()
    board.ui_track_set(TRACK)
    _drive_measures(board, 20)  # > 17 -> capped
    q = board.capture_ring_query()
    assert q["depth"] == 17, f"ring depth should cap at 17, got {q['depth']}"
    assert q["track"] == TRACK, f"ring should record visible track {TRACK}, got {q['track']}"


@pytest.mark.hardware
def test_capture_ring_reaches_16_bars_back(board):
    """Ring-17 extends the frame-back from 15 to a full 16 bars. This pins the RING
    extension itself: at K=16 the capture now PASSES the ring/history guard and
    reaches the dst par-buffer guard, whereas the old depth-16 ring returned -6
    (0x16, 'not enough history') at K=16.

    An actual 16-bar SUCCESS needs a true 1-voice melodic track (par_instr=1):
    256 steps * 1 instr = 256 <= SEQ_PAR_MAX_BYTES(1024). This HIL track is a
    16-instrument drum layout (track_drum_init; the harness has no melodic init —
    the parked 'true Note-track init' follow-on), so 256 * 16 = 4096 overflows the
    par buffer and the grab refuses -9 (0x19). That refusal is the proof: it got
    PAST the ring guard. The success path is validated by ear on a melodic track."""
    board.reset()
    _setup_wander_track(board)
    _drive_measures(board, 18)  # >= 17 -> depth caps at 17 -> frame-back valid through K=16
    q = board.capture_ring_query()
    assert q["depth"] == 17, f"ring should cap at 17 before a full grab, got {q['depth']}"

    # 0x19 = dst par overflow: the grab passed the ring/history guard (the old
    # depth-16 ring returned 0x16 at K=16) and only stopped at this drum-layout
    # track's par-buffer cap. 0x16 here would mean the ring still caps below 16.
    status = board.capture_span(TRACK, 16, DST)
    assert status == 0x19, (
        f"expected par-cap refusal 0x19 at K=16 (ring-17 reached, par capped); got "
        f"{hex(status)} — 0x16 would mean the ring still caps below 16 bars"
    )


@pytest.mark.hardware
def test_capture_max_k_matches_refusal_boundary(board):
    """CaptureMaxK (the par/trg-aware grabbable max the UI thermometer lights, now
    exposed in the ring query) must equal the real success/refuse boundary: a grab
    at max_k succeeds, a grab at max_k+1 refuses with a dst buffer overflow. Guards
    against the UI cap drifting from the engine's own guards. On this 16-instrument
    drum-layout track the par buffer caps max_k well below the ring depth — exactly
    the over-promise the par-aware thermometer fixes (raw depth would light to 16)."""
    board.reset()
    _setup_wander_track(board)
    _drive_measures(board, 18)  # deep ring -> the binding cap is the par buffer, not the ring
    q = board.capture_ring_query()
    assert q["depth"] == 17, f"ring should be full (17) before the boundary check, got {q['depth']}"
    mk = q["max_k"]
    assert 1 <= mk < 16, (
        f"a 16-instrument drum-layout track must cap max_k below the ring depth "
        f"(the over-promise case); got max_k={mk}"
    )

    # at the cap -> succeeds; one past the cap -> dst buffer overflow (par 0x19 / trg 0x1c).
    assert board.capture_span(TRACK, mk, DST) == 0x01, f"grab at max_k={mk} should succeed"
    over = board.capture_span(TRACK, mk + 1, DST)
    assert over in (0x19, 0x1c), (
        f"grab at max_k+1={mk + 1} should refuse with a buffer overflow (0x19/0x1c); "
        f"got {hex(over)} — UI cap (max_k) has drifted from the engine guard"
    )


@pytest.mark.hardware
def test_capture_stopped_refuses_non_measure_multiple(board):
    """STOPPED re-sim grabs N-whole-global-measure tracks but refuses a length that is NOT
    an integer multiple of the global measure (status 0x18 = 0x10|8): a non-multiple
    longer-than-a-bar length (24 steps = 1.5 measures), an odd length (11 steps), and a
    SUB-measure length (8 steps). The re-sim drive phase-aligns to the global measure, so
    reproducing a non-aligned loop is the deferred A2 kernel — the LCD steers to 'play to
    grab' (the WHILE-PLAYING tape path handles any length; see test_capture_while_playing)."""
    board.reset()
    board.ui_track_set(TRACK)
    for length, steps in ((23, 24), (10, 11), (7, 8)):
        board.cc_set(TRACK, SEQ_CC_LENGTH, length)
        _drive_measures(board, 3)
        status = board.capture_span(TRACK, 1, DST)
        assert status == 0x18, (
            f"expected measure-multiple refusal 0x18 for {steps} steps, got {hex(status)}"
        )


@pytest.mark.hardware
def test_capture_multimeasure_stopped_refused(board):
    """STOPPED re-sim is ONE global measure only. A multi-measure track (32-step = 2 bars,
    64-step = 4 bars) refuses while stopped (0x18 = "play to grab"): the re-sim drive phase-
    aligns to the global measure and can't yet reproduce a multi-bar loop's own phase (a
    hardware trace showed a sub-measure rotation — the deferred A2 kernel). Multi-measure
    capture is via the WHILE-PLAYING tape instead (test_capture_while_playing_twobar_phase
    proves it note-for-note)."""
    board.reset()
    board.ui_track_set(TRACK)                          # ring follows the visible track
    for steps in (32, 64):
        board.cc_set(TRACK, SEQ_CC_LENGTH, steps - 1)
        _drive_measures(board, 4)                      # a little history (the -8 gate is pre-history)
        status = board.capture_span(TRACK, 1, DST)
        assert status == 0x18, (
            f"stopped {steps}-step ({steps // 16}-bar) grab should refuse 0x18 (play to grab), "
            f"got {hex(status)}"
        )


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
    traversal seed are unchanged after a capture. (The robotize seed — also written
    during the drive, now sourced from the frame — is restored by the whole-track
    memcpy in SEQ_CORE_CaptureSpanSnapshot/Restore, so it's covered here without a
    dedicated accessor.)"""
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
