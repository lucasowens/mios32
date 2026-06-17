"""POSTURE-MORPH (Loop A) — per-group posture interpolation between live and a
target phrase's same-group slice.

Design §10 "Phrase morphing": glide ONE pattern group's posture (ext CCs
0x80..0x9f) from its LIVE state (morph pos 0 = true pass-through, the §2
"every dial sweeps 0->max incl. pass-through at 0") toward a target phrase's
same-group slice (pos PHRASE_MORPH_MAX). live = A + pos/MAX*(B-A), per CC,
applied per-measure. Grid/notes and the other three groups stay untouched.

A (the morph=0 endpoint) is snapshotted from the focused group at ARM time, so
pos 0 returns to it bit-exactly (reversible). B is read once from the phrase's
group-slice on disk (SEQ_FILE_B_PhraseReadCCs — no per-step SD).

These pins assert on cc_get, which reads the SOURCE CC struct (SEQ_CC_Get) — so
they prove the lerp MATH and the arm/set/tick STATE MACHINE deterministically.
That the morphed posture is actually re-rendered + audible (the SEQ_CC_Set
render-arming asymmetry) is the by-ear GO's job.

The pins:
  - pass-through at 0 == the live posture, full-throw == the phrase's posture
    (exact endpoints), a clean midpoint, and reversibility (0 returns to A);
  - the morph touches ONLY the focused group — the other three are untouched;
  - while RUNNING the position lands per-measure (the ref_step==0 boundary
    driver), not inline; while STOPPED it applies inline (auditioning);
  - arm of an un-captured phrase refuses cleanly (no live change);
  - the QUERY round-trip, and reset disarms the morph.

The morph reads the phrase's V4 ext block, which only freshly-created sessions
reserve room for — the `genv4` fixture (shared convention with test_phrases)
load-or-creates session GENV4 and restores AUTOTEST afterwards.

Destructive: overwrites GENV4 scratch slots and its MBSEQ_PH.V4. Never touches
the A1-A3 baselines.
"""

import time

import pytest

from harness import Board, Button, CC
from harness.sysex import PHRASE_MORPH_MAX, RESET_DEFAULT


pytestmark = pytest.mark.timeout(120)

GENV4_SESSION = "GENV4"
AUTOTEST_SESSION = "AUTOTEST"

SCRATCH_BANK = 0
SCRATCH_A = 61
SCRATCH_B = 62

GRIP = CC.TENSION_GRIP  # 0x9a — a clean 0..127 ext CC, carried by the morph

# Group 0 (focused) markers: distinct live(A) vs phrase(B) on two of its tracks.
PHRASE_B = 5
GRIP_A_T0, GRIP_B_T0 = 20, 100
GRIP_A_T1, GRIP_B_T1 = 28, 92
# midpoint (pos = MAX/2): A + (B-A)/2  ->  60 and 60
MID = PHRASE_MORPH_MAX // 2
GRIP_MID_T0 = GRIP_A_T0 + (GRIP_B_T0 - GRIP_A_T0) * MID // PHRASE_MORPH_MAX  # 60
GRIP_MID_T1 = GRIP_A_T1 + (GRIP_B_T1 - GRIP_A_T1) * MID // PHRASE_MORPH_MAX  # 60

# A track in group 1 (NOT focused) — must stay untouched throughout.
OTHER_TRACK = 4
GRIP_OTHER = 64

ALL_GROUPS_DIRTY = 0x0f
SETTLE = 0.10
PLAY_ACROSS_WRAP_S = 2.5  # > one bar at default tempo -> guarantees a ref_step==0 boundary


@pytest.fixture
def genv4(board: Board):
    """Run on a V4-sized session; restore AUTOTEST on teardown (mirrors test_phrases)."""
    if board.session_name_get() != GENV4_SESSION:
        try:
            board.session_load(GENV4_SESSION, timeout=20.0)
        except (TimeoutError, RuntimeError):
            board.session_create(GENV4_SESSION)
    yield board
    board.session_load(AUTOTEST_SESSION, timeout=20.0)


def _park(board: Board) -> None:
    """Group 0's working slot points at SCRATCH_A with a clean dirty bit (the
    Stage A/B parking convention)."""
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_B), "slot B build should commit"
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A), "slot A build should commit"
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A), "park load should commit"
    mask, _ = board.dirty_query()
    assert not (mask & 0x01), f"park should leave group 0 clean, mask={mask:#04x}"


def _clean(board: Board) -> None:
    for g in range(4):
        board.dirty_set(g, False)


def _build_phrase_b_and_live_a(board: Board) -> None:
    """Capture phrase B with the B-markers on group 0, then set live to the
    A-markers and stamp the group-1 isolation marker."""
    board.ui_track_set(0)  # focus group 0 (the morph latches this at arm time)
    # phrase B's group-0 posture
    board.cc_set(0, GRIP, GRIP_B_T0)
    board.cc_set(1, GRIP, GRIP_B_T1)
    assert board.phrase_capture(PHRASE_B), "capture B should commit"
    # live A (the morph=0 endpoint) + an untouched group-1 marker
    board.cc_set(0, GRIP, GRIP_A_T0)
    board.cc_set(1, GRIP, GRIP_A_T1)
    board.cc_set(OTHER_TRACK, GRIP, GRIP_OTHER)


@pytest.mark.hardware
def test_morph_endpoints_midpoint_reversible_stopped(genv4):
    """THE Loop A pin (transport stopped -> inline apply). pos 0 = the live
    posture exactly (pass-through), pos MAX = the phrase's posture exactly, a
    clean linear midpoint, and pos 0 again returns to A bit-exactly. Asserts on
    two tracks of the focused group so the per-track fan is exercised."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_phrase_b_and_live_a(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm toward a captured phrase should commit"
    assert board.phrase_morph_query() == (0, PHRASE_B), "arm: pos 0, target = B"

    # pos 0 == live A (true pass-through)
    assert board.phrase_morph_set(0) == 0
    assert board.cc_get(0, GRIP) == GRIP_A_T0, "pos 0 = live posture (track 0)"
    assert board.cc_get(1, GRIP) == GRIP_A_T1, "pos 0 = live posture (track 1)"

    # pos MAX == phrase B's posture (exact endpoint)
    assert board.phrase_morph_set(PHRASE_MORPH_MAX) == PHRASE_MORPH_MAX
    assert board.cc_get(0, GRIP) == GRIP_B_T0, "pos MAX = phrase posture (track 0)"
    assert board.cc_get(1, GRIP) == GRIP_B_T1, "pos MAX = phrase posture (track 1)"

    # clean linear midpoint (±1 for integer rounding)
    assert board.phrase_morph_set(MID) == MID
    assert abs(board.cc_get(0, GRIP) - GRIP_MID_T0) <= 1, "midpoint lerp (track 0)"
    assert abs(board.cc_get(1, GRIP) - GRIP_MID_T1) <= 1, "midpoint lerp (track 1)"

    # reversible: pos 0 returns to the arm-time A, bit-exactly
    assert board.phrase_morph_set(0) == 0
    assert board.cc_get(0, GRIP) == GRIP_A_T0, "reversible: back to A exactly (track 0)"
    assert board.cc_get(1, GRIP) == GRIP_A_T1, "reversible: back to A exactly (track 1)"

    _clean(board)


@pytest.mark.hardware
def test_morph_leaves_other_groups_untouched(genv4):
    """The morph touches ONLY the focused group's four tracks — a marker on a
    group-1 track is unchanged across a full-throw morph of group 0."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_phrase_b_and_live_a(board)

    assert board.cc_get(OTHER_TRACK, GRIP) == GRIP_OTHER, "preamble: group-1 marker set"

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"
    assert board.phrase_morph_set(PHRASE_MORPH_MAX) == PHRASE_MORPH_MAX

    assert board.cc_get(0, GRIP) == GRIP_B_T0, "focused group did morph"
    assert board.cc_get(OTHER_TRACK, GRIP) == GRIP_OTHER, (
        "a non-focused group's posture must be untouched by the per-group morph"
    )

    _clean(board)


@pytest.mark.hardware
def test_morph_applies_per_measure_when_running(genv4):
    """While RUNNING, a position change is deferred to the ref_step==0 measure
    boundary (the SEQ_PATTERN_PhraseMorphTick driver) rather than applied inline.
    Arm at 0, start, set full-throw, then play across a bar wrap: the boundary
    driver lands the phrase posture. (The stopped/inline path is covered above.)"""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_phrase_b_and_live_a(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"
    assert board.phrase_morph_set(0) == 0
    assert board.cc_get(0, GRIP) == GRIP_A_T0, "armed at pos 0 = live A"

    board.press(Button.PLAY)
    time.sleep(SETTLE)
    assert board.phrase_morph_set(PHRASE_MORPH_MAX) == PHRASE_MORPH_MAX  # deferred while running
    time.sleep(PLAY_ACROSS_WRAP_S)  # a ref_step==0 boundary fires the morph tick
    board.press(Button.STOP)
    time.sleep(SETTLE)

    assert board.cc_get(0, GRIP) == GRIP_B_T0, (
        "the per-measure boundary driver should have landed the phrase posture"
    )

    _clean(board)


@pytest.mark.hardware
def test_morph_arm_refuses_uncaptured(board: Board):
    """Arming toward a phrase that was never captured refuses cleanly (returns
    False, no live change, no crash). RESET_DEFAULT clears occupancy, so this
    touches no SD and leaves the baselines alone."""
    board.reset(RESET_DEFAULT)
    assert not board.phrase_present(9), "a freshly reset session has no captured phrases"
    grip_before = board.cc_get(0, GRIP)
    assert not board.phrase_morph_arm(9), "arm of an uncaptured phrase must refuse"
    assert board.phrase_morph_query() == (0, -1), "a refused arm leaves the morph disarmed"
    assert board.cc_get(0, GRIP) == grip_before, "a refused arm must not change live state"
    # SET while disarmed is refused (the client raises on the refuse status)
    with pytest.raises(RuntimeError):
        board.phrase_morph_set(8)


@pytest.mark.hardware
def test_morph_query_roundtrip_and_reset_disarms(genv4):
    """QUERY reflects arm + set; RESET_STATE disarms the morph (the gesture-reset
    hardening — a stray armed morph must not survive a reset, like the phantom-pull
    guard)."""
    board = genv4
    board.reset(RESET_DEFAULT)
    assert board.phrase_morph_query() == (0, -1), "reset session starts disarmed"

    _park(board)
    board.ui_track_set(0)
    board.cc_set(0, GRIP, GRIP_B_T0)
    assert board.phrase_capture(0), "capture should commit"

    assert board.phrase_morph_arm(0), "arm should commit"
    assert board.phrase_morph_query() == (0, 0), "armed: pos 0, target 0"
    assert board.phrase_morph_set(7) == 7, "set should hold the position"
    assert board.phrase_morph_query() == (7, 0), "query reflects the set position"

    board.reset(RESET_DEFAULT)
    assert board.phrase_morph_query() == (0, -1), "RESET_STATE must disarm the morph"

    _clean(board)


# A second phrase to recall while a morph is armed (the "arrival" gesture).
RECALL_SLOT = 3


@pytest.mark.hardware
def test_morph_releases_on_recall(genv4):
    """Recall is "the arrival" (design §10) — it replaces the whole organism, so an
    armed morph is RELEASED, since its arm-time A is now stale. Otherwise the next
    nudge would lerp off the stale A and break pos-0==live. (Reviewer-found gap.)"""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    board.ui_track_set(0)

    # phrase B (the morph target) and a distinct phrase to recall
    board.cc_set(0, GRIP, GRIP_B_T0)
    assert board.phrase_capture(PHRASE_B), "capture B should commit"
    board.cc_set(0, GRIP, GRIP_A_T0)
    assert board.phrase_capture(RECALL_SLOT), "capture the recall target should commit"

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"
    assert board.phrase_morph_set(MID) == MID, "ride the morph partway"
    assert board.phrase_morph_query()[1] == PHRASE_B, "armed toward B before the recall"

    # recall the other phrase: the arrival -> the morph releases
    assert board.phrase_recall(RECALL_SLOT), "recall should commit"
    assert board.phrase_morph_query() == (0, -1), "recall (arrival) must release the morph"
    assert board.cc_get(0, GRIP) == GRIP_A_T0, "recall restored its own posture (not a stale lerp)"

    _clean(board)


@pytest.mark.hardware
def test_morph_releases_on_session_load(genv4):
    """A session switch replaces the whole rig, so an armed morph (holding the OLD
    session's A/B/group) must be released — else a stray nudge would clobber the
    new set's posture from stale snapshots. (SEQ_PATTERN_ProbePhrasesOnLoad.)"""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    board.ui_track_set(0)
    board.cc_set(0, GRIP, GRIP_B_T0)
    assert board.phrase_capture(0), "capture should commit"

    assert board.phrase_morph_arm(0), "arm should commit"
    assert board.phrase_morph_query()[1] == 0, "armed before the reload"

    board.session_load(GENV4_SESSION, timeout=20.0)  # the whole rig changes under us
    assert board.phrase_morph_query() == (0, -1), "a session load must release the armed morph"


def _arm_group0_morph(board: Board) -> None:
    """Capture phrase B, set live to A on group 0, arm a morph toward B (focused
    group 0). Leaves the morph armed at pos 0 — the precondition for the
    release-on-out-of-band-CC-replace pins below."""
    board.ui_track_set(0)
    board.cc_set(0, GRIP, GRIP_B_T0)
    assert board.phrase_capture(PHRASE_B), "capture B should commit"
    board.cc_set(0, GRIP, GRIP_A_T0)  # live A
    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"
    assert board.phrase_morph_query()[1] == PHRASE_B, "armed before the disturbance"


@pytest.mark.hardware
def test_morph_releases_on_pattern_switch(genv4):
    """A pattern switch into the morph's focused group replaces its live CCs ->
    the arm-time A is stale, so the morph is released (SEQ_PATTERN_Load)."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _arm_group0_morph(board)
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B), "switch group 0 should commit"
    assert board.phrase_morph_query() == (0, -1), "a pattern switch into the morph's group releases it"
    _clean(board)


@pytest.mark.hardware
def test_morph_releases_on_pull(genv4):
    """A RECOMBINE pull into a track of the morph's focused group replaces that
    track's live CCs -> the morph is released (SEQ_CORE_LoadTrackFromSlot)."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _arm_group0_morph(board)
    assert board.track_load(0, SCRATCH_BANK, SCRATCH_B, 0), "pull into a group-0 track should commit"
    assert board.phrase_morph_query() == (0, -1), "a pull into the morph's group releases it"
    _clean(board)


@pytest.mark.hardware
def test_morph_releases_on_undo(genv4):
    """UNDO restores a track's CCs out-of-band -> if a morph is armed on that
    track's group its arm-time A is stale, so the morph is released
    (SEQ_CORE_TrackUndoRestore). Arm AFTER the pull so the undo victim is real."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    # a pull arms the track-undo with track 0's prior state (and would cancel any
    # morph — none armed yet); then arm a NEW morph on group 0.
    assert board.track_load(0, SCRATCH_BANK, SCRATCH_B, 0), "pull to arm the track undo"
    _arm_group0_morph(board)
    assert board.track_undo() is not None, "undo should restore the pulled track"
    assert board.phrase_morph_query() == (0, -1), "an UNDO into the morph's group releases it"
    _clean(board)
