"""Retroactive CAPTURE span GESTURE — UTILITY-held, end-to-end button-driven.

The engine (ring + re-sim + materialize) is pinned in test_capture_span.py. THIS
file pins the physical GESTURE wiring (the north-star "play, then keep" surface):

    hold UTILITY -> tap a select-row button (destination track)
                 -> tap GP-n (grab the last n bars + COMMIT)

Source = the ring's recording track; K = the GP index (GP1=1 bar .. GP16=16); dst
= the select-row pick. Transport STOPPED (first cut). UTILITY / select-row / GP
are all injectable through CMD_BUTTON, so the gesture is driven exactly as a hand
would. Ring filled via CMD_CLOCK_STEP (advances the stopped engine so the ring
records), same as test_capture_span.py.

Headline guarantee: the gesture invokes the engine with the SAME (src, K) and the
dst YOU PICKED — i.e. UTILITY+select[d]+GP(k) lands exactly what
CMD_CAPTURE_SPAN(src, k, d) would, in track d and nowhere else.
"""

import time

import pytest

from harness import Board, Button, CC, Page
from harness.sysex import DIAL_DEPTH, DIAL_RANGE_MAX, DIAL_RANGE_MIN, DIAL_RATE

TRACK = 0            # the generative source; the ring records the visible track
DST_A = 5            # gesture destination (select-row 6, 1-based) — group 1
DST_B = 6            # engine reference / second gesture destination — group 1
BYSTANDER = 7        # must stay empty (the gesture must not spray here) — group 1
K = 2                # grab the last 2 bars (GP2) for the headline pin
MEASURE_TICKS = 16 * 96  # steps_per_measure(16) * ticks_per_16th(96)
GP1 = Button.GP(1)
SETTLE = 0.10

SEQ_CC_LENGTH = 0x4d
SEQ_CC_DIRECTION = 0x48
TRKDIR_RANDOM_STEP = 5
EVENT_MODE_NOTE = 0  # melodic (the first-cut capture target; drum has no note par layer)


@pytest.fixture(autouse=True)
def _release_utility_after(board):
    """Never leave UTILITY held: a gesture aborted mid-hold (an assertion between
    the press and the release) would otherwise poison every following test — they
    would see the CAPTURE overlay and have their select-row/GP presses swallowed.
    UTILITY is wired on the midiphy panel, so the release dispatches; swallow any
    error just in case."""
    yield
    try:
        board.button(Button.UTILITY, depressed=True)
    except Exception:
        pass


def _drive_measures(board: Board, measures: int) -> None:
    """Advance the (stopped) engine `measures` musical measures so the ring fills,
    in clock_step chunks (the verb's tick payload is 14-bit)."""
    remaining = measures * MEASURE_TICKS
    while remaining > 0:
        chunk = min(remaining, 16000)
        board.clock_step(chunk)
        remaining -= chunk


def _setup_wander_track(board: Board) -> None:
    """A wide, fully-active pitch generator on a MELODIC (Note) track with
    random-step traversal — real generative material for the ring to capture."""
    board.track_drum_init(TRACK)
    board.cc_set(TRACK, CC.EVENT_MODE, EVENT_MODE_NOTE)  # -> Note: a note par layer exists
    board.cc_set(TRACK, SEQ_CC_LENGTH, 15)               # 16 steps = one whole measure
    board.ui_track_set(TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    board.press(GP1)                                     # ENGAGE the generator
    time.sleep(0.85)
    board.generator_dial_set(TRACK, 0, DIAL_RANGE_MIN, 36)
    board.generator_dial_set(TRACK, 0, DIAL_RANGE_MAX, 84)
    board.generator_dial_set(TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(TRACK, 0, DIAL_DEPTH, 64)
    board.cc_set(TRACK, SEQ_CC_DIRECTION, TRKDIR_RANDOM_STEP)
    # Gate every step so the re-sim actually emits notes (the generator drives
    # PITCH; gates are a separate trigger layer). trg layer 0 = gate, 2 bytes = 16.
    board.trg_byte_set(TRACK, 0, 0xff)
    board.trg_byte_set(TRACK, 1, 0xff)
    time.sleep(SETTLE)


def _capture_gesture(board: Board, dst_track: int, k: int) -> None:
    """Drive the UTILITY-held CAPTURE span gesture via injected buttons. dst_track
    is 0-based; select-row buttons are 1-based (DIRECT_TRACK); GP(k) grabs the last
    k bars and commits on its press."""
    board.button(Button.UTILITY, depressed=False)        # UTILITY down (arm)
    try:
        board.press(Button.DIRECT_TRACK(dst_track + 1))  # select-row: destination track
        board.press(Button.GP(k))                        # GP-k: grab last k bars -> commit
    finally:
        board.button(Button.UTILITY, depressed=True)     # always release the hold


@pytest.mark.hardware
def test_capture_gesture_lands_in_picked_track_matching_engine(board):
    """The gesture grabs the last K bars of the ring's track into the select-row
    track — byte-identical to the engine verb with the same (src, K) — and writes
    ONLY that track. Validates src (the ring track), dst (the select-row pick) and
    K (the GP index) are all wired correctly."""
    board.reset()
    _setup_wander_track(board)
    _drive_measures(board, 4)

    q = board.capture_ring_query()
    assert q["track"] == TRACK and q["depth"] >= K + 1, (
        f"ring should record T{TRACK} with >= {K + 1} bars before the grab, got {q}"
    )

    # engine reference: same src (the ring's track) + same K, into DST_B. The
    # capture is non-destructive (restores the ring byte-identical), so the gesture
    # below re-drives the same emitted stream.
    assert board.capture_span(TRACK, K, DST_B) == 0x01, "engine reference capture failed"
    ref = [board.track_par_get(DST_B, 0, 0, s) for s in range(K * 16)]
    assert any(ref), "engine reference produced no notes"

    # snapshot a bystander track BEFORE the gesture. Its baseline may be non-empty
    # (RESET_STATE deliberately does NOT clear pattern data, and this test never
    # inits group 1) — so prove the gesture leaves it UNCHANGED rather than
    # asserting it's all-zero, which would false-fail on leftover/boot data.
    bystander_before = [board.track_par_get(BYSTANDER, 0, 0, s) for s in range(K * 16)]

    # gesture: UTILITY + select-row[DST_A] + GP(K).
    _capture_gesture(board, DST_A, K)
    got = [board.track_par_get(DST_A, 0, 0, s) for s in range(K * 16)]

    assert got == ref, (
        "gesture capture differs from the engine verb (src/K mis-wired):\n"
        f"  engine T{DST_B}: {ref}\n  gesture T{DST_A}: {got}"
    )

    # the gesture wrote ONLY the picked track — the bystander is untouched.
    bystander_after = [board.track_par_get(BYSTANDER, 0, 0, s) for s in range(K * 16)]
    assert bystander_after == bystander_before, (
        f"gesture sprayed into T{BYSTANDER}: {bystander_before} -> {bystander_after}"
    )


@pytest.mark.hardware
def test_capture_gesture_gp_index_sets_bar_count(board):
    """GP-n picks K = n bars: GP1 -> 1 bar, GP3 -> 3 bars. The dst track length
    follows K (engine sets length = K*16 - 1). Pins the GP-index -> K mapping
    across more than one value."""
    board.reset()
    _setup_wander_track(board)
    _drive_measures(board, 5)  # depth >= 4 so K=3 is valid (max grabbable = depth-1)

    _capture_gesture(board, DST_A, 1)
    len1 = board.cc_get(DST_A, SEQ_CC_LENGTH)
    assert len1 == 15, f"GP1 should grab 1 bar -> dst length 15 (16 steps), got {len1}"

    _capture_gesture(board, DST_B, 3)
    len3 = board.cc_get(DST_B, SEQ_CC_LENGTH)
    assert len3 == 47, f"GP3 should grab 3 bars -> dst length 47 (48 steps), got {len3}"


@pytest.mark.hardware
def test_capture_select_row_button_state_survives_utility_straddle(board):
    """Regression (mirror of test_capture_gesture.py's PATTERN straddle pin): a
    select-row press whose press/release straddles the UTILITY-hold boundary must
    not leave button_state with a stuck bit and corrupt the track-select radio
    afterward (symptom: presses toggle instead of switching)."""
    board.reset()
    board.track_drum_init(0)
    time.sleep(SETTLE)

    # establish track-select view + confirm the select row switches tracks
    board.press(Button.DIRECT_TRACK(4))          # select track 3
    time.sleep(SETTLE)
    if board.ui_track_get() != 3:
        pytest.skip("select row not in track-select mode on this hardware config")

    # straddle: hold a select button across the UTILITY press/release
    board.button(Button.DIRECT_TRACK(6), depressed=False)  # DT6 (track 5) DOWN, no UTILITY
    board.button(Button.UTILITY, depressed=False)          # UTILITY DOWN (DT6 still held)
    board.button(Button.DIRECT_TRACK(6), depressed=True)   # DT6 UP during the hold (the straddle)
    board.button(Button.UTILITY, depressed=True)           # UTILITY UP
    time.sleep(SETTLE)

    # track-select must still radio cleanly to a single track.
    board.press(Button.DIRECT_TRACK(8))          # select track 7
    time.sleep(SETTLE)
    got = board.ui_track_get()
    assert got == 7, (
        f"track-select corrupted after a select press straddling the UTILITY "
        f"hold (button_state stuck): selecting track 7 landed on {got}"
    )
