"""FEARLESS SWITCHING Stage B — gen-state ext tag V4 ("the organism comes
back alive").

Design §9 (2026-06-11) + plan 2026-06-12: gen state is SLOT CONTENT. A
pattern record's V4 ext block carries up to SEQ_GENERATOR_PERSIST_SLOTS (4)
generator entries per track (dials, engaged flag, loop/locks/anchor/mult);
every content-replacing load clears the track's generators and re-seeds them
from the incoming record — resume ENGAGED, no re-anchor, no undo
re-snapshot. The corollaries pinned here:

  - a gen-less slot KILLS the track's generators on load (the semantic
    change: pre-Stage-B, an engaged generator survived pattern switches and
    chewed on whatever loaded under it);
  - the pull (TrackRead) carries gen state, and track-undo restores the
    victim's generators (else the pulled organism rewrites the restored
    notes at the next wrap);
  - the capture-to-slot trample preserves the dst group's LIVE generators
    while round-tripping the slot's own;
  - existing V3-sized sessions (AUTOTEST) degrade: the writeback falls back
    to a V3 ext block — payload intact, organism not persisted, no crash.

The V4 round-trip pins need V4-sized bank records, which only freshly
created sessions have — the `genv4` fixture load-or-creates session GENV4
and restores AUTOTEST afterwards so the rest of the suite keeps its
baselines. Sessions on the device SD are disposable test scratch (standing
note); GENV4 content is rebuilt by every test that uses it.

Destructive: overwrites GENV4 (0, 61) / (0, 62) and AUTOTEST (0, 61) /
(0, 62) — the established scratch slots, never the A1-A3 baselines.
"""

import time

import pytest

from harness import Board, Button, CC, Page
from harness.sysex import DIAL_DEPTH, DIAL_RATE, RESET_DEFAULT


# The genv4 fixture may CREATE the session on first use — an async SD format
# worth tens of seconds. Override the suite's 10s default for this module.
pytestmark = pytest.mark.timeout(120)

GENV4_SESSION = "GENV4"
AUTOTEST_SESSION = "AUTOTEST"  # conftest.TEST_SESSION_NAME

SCRATCH_BANK = 0
SCRATCH_A = 61  # group 0's parked working slot in these tests
SCRATCH_B = 62  # the "other" slot; gen-less by construction in _park

TRACK = 0       # the organism's track (group 0)
PULL_DST = 6    # pull destination (group 1) — mirrors Stage A's pull pin
INSTR = 0

GP1 = Button.GP(1)
SETTLE = 0.10
ENGAGE_MSG_MS = 750

# Non-default dial values so the round trip can't pass on defaults.
RATE_SCULPTED = 77
DEPTH_SCULPTED = 55

LEN_SLOT_A = 7
LEN_EDIT = 12


@pytest.fixture
def genv4(board: Board):
    """Run the test on a V4-sized session (fresh-format bank records reserve
    room for the gen sub-block); restore AUTOTEST on teardown so the rest of
    the suite sees its baselines."""
    if board.session_name_get() != GENV4_SESSION:
        try:
            board.session_load(GENV4_SESSION, timeout=20.0)
        except (TimeoutError, RuntimeError):
            board.session_create(GENV4_SESSION)
    yield board
    board.session_load(AUTOTEST_SESSION, timeout=20.0)


def _park(board: Board) -> None:
    """Stage A's parking convention: both scratch slots get known (gen-less)
    content from the freshly reset live state, then a raw load points group
    0's working slot at SCRATCH_A with a clean dirty bit."""
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_B), "slot B build should commit"
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A), "slot A build should commit"
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A), "park load should commit"
    mask, _ = board.dirty_query()
    assert not (mask & 0x01), f"park should leave group 0 clean, mask={mask:#04x}"


def _engage(board: Board, track: int = TRACK, instr: int = INSTR) -> None:
    """ENGAGE a Turing generator on (track, instr) via the PITCHGEN page —
    the same recipe as the pitchgen lifecycle tests."""
    board.track_drum_init(track)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(instr)
    time.sleep(SETTLE)
    board.press(GP1)
    time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)
    g = board.generator_query(track, instr)
    assert g is not None and g.engaged, "ENGAGE preamble failed"


def _sculpt(board: Board, track: int = TRACK, instr: int = INSTR) -> None:
    """Make the slot unmistakably non-default: dials, locks, MULT codes, and
    a loop that has drifted away from its auto-anchor."""
    board.generator_dial_set(track, instr, DIAL_RATE, RATE_SCULPTED)
    board.generator_dial_set(track, instr, DIAL_DEPTH, DEPTH_SCULPTED)
    board.generator_lock_set(track, instr, 3, True)
    board.generator_lock_set(track, instr, 17, True)
    board.generator_mult_set(track, instr, 5, 3)   # 2×
    board.generator_mult_set(track, instr, 9, 0)   # mute
    for _ in range(3):
        board.generator_tick_force(track, instr)   # drift loop off the anchor


def _assert_same_slot(g_now, g_ref) -> None:
    """The byte-identical persistence contract."""
    assert g_now.loop == g_ref.loop, "loop should round-trip byte-identical"
    assert g_now.locks == g_ref.locks, "locks should round-trip"
    assert g_now.mult == g_ref.mult, "MULT codes should round-trip"
    assert g_now.anchor == g_ref.anchor, "anchor should round-trip byte-identical"
    assert g_now.anchor_valid == g_ref.anchor_valid
    assert g_now.mutation_rate == g_ref.mutation_rate, "rate dial should round-trip"
    assert g_now.mutation_depth == g_ref.mutation_depth, "depth dial should round-trip"
    assert g_now.range_min == g_ref.range_min
    assert g_now.range_max == g_ref.range_max
    assert g_now.contour_shape == g_ref.contour_shape


@pytest.mark.hardware
def test_sculpted_organism_survives_round_trip_alive(genv4):
    """THE Stage B pin — engage, sculpt, switch away, come back: the slot is
    byte-identical (loop, locks, anchor, MULT, dials), still ENGAGED, and
    mutation resumes. Switching away also pins the kill half: the gen-less
    slot B clears the track's generators."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    _engage(board)
    _sculpt(board)
    g0 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g0 is not None and g0.engaged
    assert g0.loop != g0.anchor, "sculpt preamble: loop should have drifted off the anchor"

    # Switch away: writeback persists the organism into slot A; slot B has no
    # gen entries, so the track must come up generator-less (the semantic
    # change — pre-Stage-B the generator survived and chewed on slot B).
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B), "switch to B should commit"
    assert board.generator_query(TRACK, INSTR) is None, (
        "a gen-less slot must kill the track's generators on load"
    )

    # Come back: the organism resumes, byte-identical and engaged.
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_A), "return to A should commit"
    g1 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g1 is not None, "the organism should come back"
    assert g1.engaged, "gen state resumes ENGAGED (blessed fork: comes back alive)"
    _assert_same_slot(g1, g0)

    # Alive = it still mutates (rate 77 / depth 55 over 64 steps: the odds of
    # a no-op mutate cycle are astronomically small).
    board.generator_tick_force(TRACK, INSTR)
    g2 = board.generator_query(TRACK, INSTR)
    assert g2.loop != g1.loop, "mutation should resume after the round trip"


@pytest.mark.hardware
def test_pull_carries_generator_and_undo_restores_victim(genv4):
    """The pull inherits the organism (posture pull): TrackRead re-seeds the
    destination track's generator from the section's V4 entry. Track-undo
    puts the victim's (gen-less) state back — without the gen carry, the
    pulled organism would keep rewriting the restored notes."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    _engage(board)
    _sculpt(board)
    g0 = board.generator_query(TRACK, INSTR, with_anchor=True)

    # Aimed save carries the organism into the slot (variation-library save).
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A), "save should commit"

    assert board.generator_query(PULL_DST, INSTR) is None, "pull dst should start gen-less"
    assert board.track_load(
        dst_track=PULL_DST, bank=SCRATCH_BANK, pattern=SCRATCH_A, slot_track=TRACK
    ), "pull should commit"
    g_pulled = board.generator_query(PULL_DST, INSTR, with_anchor=True)
    assert g_pulled is not None, "the pull should bring the generator back alive"
    assert g_pulled.engaged
    _assert_same_slot(g_pulled, g0)

    # One gesture back: the victim had no generators, so undo must kill the
    # pulled one along with restoring the notes.
    assert board.track_undo() == PULL_DST
    assert board.generator_query(PULL_DST, INSTR) is None, (
        "track-undo must restore the victim's (gen-less) generator state"
    )

    board.dirty_set(1, False)  # politeness: see Stage A's pull pin


@pytest.mark.hardware
def test_gen_less_pull_clears_dst_generators(genv4):
    """Pulling a section that has no gen entry kills the destination track's
    generator — slot content wins, same rule as the group switch."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    _engage(board)
    assert board.generator_query(TRACK, INSTR) is not None

    # SCRATCH_B was parked gen-less; pull its section 1 over the organism.
    assert board.track_load(
        dst_track=TRACK, bank=SCRATCH_BANK, pattern=SCRATCH_B, slot_track=1
    ), "pull should commit"
    assert board.generator_query(TRACK, INSTR) is None, (
        "a gen-less section must clear the dst track's generators"
    )

    board.dirty_set(0, False)


@pytest.mark.hardware
def test_capture_trample_preserves_live_generators(genv4):
    """CaptureToSlotTrack stages the dst slot in the dst group's live RAM
    (read..write) — the slot's generators ride the pool through that window
    so the write-back round-trips them, then the LIVE organisms come back.
    The captured section itself is a freeze: persisted gen-less."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    _engage(board)
    _sculpt(board)
    g0 = board.generator_query(TRACK, INSTR, with_anchor=True)

    # Push track 0's frozen output into slot B section 2 (dst group 0 = the
    # group whose live organism must survive the trample).
    assert board.capture_to_slot_track(TRACK, 2, SCRATCH_BANK, SCRATCH_B), (
        "push should commit"
    )

    g1 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g1 is not None and g1.engaged, "live organism must survive the trample"
    _assert_same_slot(g1, g0)

    # The frozen copy is generator-less: pull the captured section elsewhere
    # and verify nothing comes alive.
    assert board.track_load(
        dst_track=PULL_DST, bank=SCRATCH_BANK, pattern=SCRATCH_B, slot_track=2
    ), "pull of the captured section should commit"
    assert board.generator_query(PULL_DST, INSTR) is None, (
        "a capture is a freeze — it must not persist a generator"
    )

    board.dirty_set(0, False)
    board.dirty_set(1, False)


@pytest.mark.hardware
def test_persist_cap_keeps_first_four_instruments(genv4):
    """A drum track can engage up to 16 generators but only
    SEQ_GENERATOR_PERSIST_SLOTS (4) persist — instrument-ascending, the rest
    dropped with a debug note. Round-trip and count the survivors."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    board.track_drum_init(TRACK)
    board.page_set(Page.PITCHGEN)
    for instr in range(5):
        board.ui_instrument_set(instr)
        time.sleep(SETTLE)
        board.press(GP1)
        time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)
        assert board.generator_query(TRACK, instr) is not None, f"engage {instr} failed"

    # Round trip through the working slot (writeback at switch-away).
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B)
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_A)

    for instr in range(4):
        assert board.generator_query(TRACK, instr) is not None, (
            f"instrument {instr} is inside the persist cap and should survive"
        )
    assert board.generator_query(TRACK, 4) is None, (
        "instrument 4 is beyond the persist cap and should be dropped"
    )


@pytest.mark.hardware
def test_v3_session_degrades_without_losing_payload(board):
    """AUTOTEST's bank records are V3-sized (zero slack for the gen
    sub-block): the writeback degrades to a V3 ext block — the organism is
    NOT persisted (documented degrade), but the V3 payload (CCs etc.) still
    round-trips and nothing crashes. Runs on the suite's standing AUTOTEST
    session; the genv4 fixture isn't used."""
    assert board.session_name_get() == AUTOTEST_SESSION, (
        "degrade pin must run on the V3-sized AUTOTEST session"
    )
    board.reset(RESET_DEFAULT)

    # Stage A's parking convention, with a CC marker baked into slot A.
    board.cc_set(0, CC.LENGTH, LEN_SLOT_A)
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_B)
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A)
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A)

    _engage(board)
    board.cc_set(0, CC.LENGTH, LEN_EDIT)  # the jam edit riding along

    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B), "switch to B should commit"
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_A), "return to A should commit"

    assert board.generator_query(TRACK, INSTR) is None, (
        "V3-sized record can't carry the organism — degrade drops it"
    )
    assert board.cc_get(0, CC.LENGTH) == LEN_EDIT, (
        "the V3 payload (jam edit) must still survive the round trip"
    )
