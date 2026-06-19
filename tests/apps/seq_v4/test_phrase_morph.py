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


# ============================================================================
# Loop B pins — main CC whitelist (groove, transpose) + velocity + gate
# ============================================================================

# Par layer indices for a default normal track: 0=Note, 1=Velocity, 2=Length, 3=Prob
VEL_LAYER = 1
NOTE_LAYER = 0
# Velocity markers: phrases A and B differ on all steps of track 0
VEL_A = 64   # arm-time velocity (will be snapshotted as A)
VEL_B = 100  # target phrase velocity
VEL_MID = VEL_A + (VEL_B - VEL_A) * MID // PHRASE_MORPH_MAX  # expected midpoint

# Note markers: A and B differ in pitch on all steps of track 0 (discrete swap,
# NOT lerped — at any pos each step is exactly A's pitch or B's pitch).
NOTE_A = 48  # C-3
NOTE_B = 55  # G-3

GROOVE_A = 40
GROOVE_B = 80
GROOVE_MID = GROOVE_A + (GROOVE_B - GROOVE_A) * MID // PHRASE_MORPH_MAX

TRANSPOSE_A = 2   # 2 semitones up
TRANSPOSE_B = 10  # 10 semitones up

# Snap CCs (discrete — reversible snap: B at full throw, arm-time A below it).
GROOVE_STYLE_A = 0
GROOVE_STYLE_B = 2
TRANSPOSE_OCT_A = 0
TRANSPOSE_OCT_B = 3

# A simple 4-step gate pattern: A has steps 0,1 on; B has steps 2,3 on.
# After a max-throw morph, the live gate must match B exactly; pulled back to 0
# it must restore A exactly (the gate crossfade is reversible).
# We test only the first byte (steps 0..7) of track 0.
GATE_A_BYTE = 0b00000011  # steps 0,1 on
GATE_B_BYTE = 0b00001100  # steps 2,3 on


def _build_loop_b_phrases(board: Board) -> dict:
    """Build phrase B with B-markers, leave live state at A-markers.
    Returns the actually-stored snap-CC values (read back, in case the firmware
    clamps an out-of-range value) so callers assert against the true endpoints."""
    board.ui_track_set(0)  # focus group 0

    # --- set B state on track 0 ---
    board.cc_set(0, CC.GROOVE_VALUE, GROOVE_B)
    board.cc_set(0, CC.TRANSPOSE_SEMI, TRANSPOSE_B)
    board.cc_set(0, CC.GROOVE_STYLE, GROOVE_STYLE_B)
    board.cc_set(0, CC.TRANSPOSE_OCT, TRANSPOSE_OCT_B)
    for step in range(4):
        board.track_par_set(0, VEL_LAYER, 0, step, VEL_B)
        board.track_par_set(0, NOTE_LAYER, 0, step, NOTE_B)
    board.trg_byte_set(0, 0, GATE_B_BYTE, trg_layer=0)  # first gate byte
    style_b = board.cc_get(0, CC.GROOVE_STYLE)
    oct_b = board.cc_get(0, CC.TRANSPOSE_OCT)

    assert board.phrase_capture(PHRASE_B), "Loop B: capture B should commit"

    # --- restore A state on track 0 ---
    board.cc_set(0, CC.GROOVE_VALUE, GROOVE_A)
    board.cc_set(0, CC.TRANSPOSE_SEMI, TRANSPOSE_A)
    board.cc_set(0, CC.GROOVE_STYLE, GROOVE_STYLE_A)
    board.cc_set(0, CC.TRANSPOSE_OCT, TRANSPOSE_OCT_A)
    for step in range(4):
        board.track_par_set(0, VEL_LAYER, 0, step, VEL_A)
        board.track_par_set(0, NOTE_LAYER, 0, step, NOTE_A)
    board.trg_byte_set(0, 0, GATE_A_BYTE, trg_layer=0)
    style_a = board.cc_get(0, CC.GROOVE_STYLE)
    oct_a = board.cc_get(0, CC.TRANSPOSE_OCT)

    return {"style_a": style_a, "style_b": style_b, "oct_a": oct_a, "oct_b": oct_b}


@pytest.mark.hardware
def test_morph_loopb_groove_transpose_stopped(genv4):
    """Loop B pin: groove_value and transpose_semi lerp linearly between A and B;
    pos 0 == A exactly, pos MAX == B exactly, midpoint ±1 tolerance (integer rounding).
    Asserts on the SOURCE CC struct (SEQ_CC_Get), not the output mirror."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_loop_b_phrases(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"

    # pos 0 = A values exactly
    board.phrase_morph_set(0)
    assert board.cc_get(0, CC.GROOVE_VALUE)   == GROOVE_A,    "pos 0 groove == A"
    assert board.cc_get(0, CC.TRANSPOSE_SEMI) == TRANSPOSE_A, "pos 0 transpose == A"

    # pos MAX = B values exactly
    board.phrase_morph_set(PHRASE_MORPH_MAX)
    assert board.cc_get(0, CC.GROOVE_VALUE)   == GROOVE_B,    "pos MAX groove == B"
    assert board.cc_get(0, CC.TRANSPOSE_SEMI) == TRANSPOSE_B, "pos MAX transpose == B"

    # midpoint ±1
    board.phrase_morph_set(MID)
    assert abs(board.cc_get(0, CC.GROOVE_VALUE)   - GROOVE_MID) <= 1, "midpoint groove ±1"

    # reversible: back to A
    board.phrase_morph_set(0)
    assert board.cc_get(0, CC.GROOVE_VALUE)   == GROOVE_A,    "reversible: groove back to A"
    assert board.cc_get(0, CC.TRANSPOSE_SEMI) == TRANSPOSE_A, "reversible: transpose back to A"

    _clean(board)


@pytest.mark.hardware
def test_morph_loopb_velocity_endpoints_stopped(genv4):
    """Loop B pin: velocity steps lerp between A and B — pos 0 = A, pos MAX = B,
    midpoint ±1, and reversible (MAX then back to 0 restores A).  Verified via the
    output mirror (track_par_get) after the inline apply forces a render."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_loop_b_phrases(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"

    # pos 0 = A velocity on all 4 test steps
    board.phrase_morph_set(0)
    for step in range(4):
        v = board.track_par_get(0, VEL_LAYER, 0, step)
        assert v == VEL_A, f"pos 0 velocity step {step} should == A ({VEL_A}), got {v}"

    # pos MAX = B velocity on all 4 test steps
    board.phrase_morph_set(PHRASE_MORPH_MAX)
    for step in range(4):
        v = board.track_par_get(0, VEL_LAYER, 0, step)
        assert v == VEL_B, f"pos MAX velocity step {step} should == B ({VEL_B}), got {v}"

    # midpoint ±1 (integer rounding)
    board.phrase_morph_set(MID)
    for step in range(4):
        v = board.track_par_get(0, VEL_LAYER, 0, step)
        assert abs(v - VEL_MID) <= 1, f"midpoint velocity step {step} ~{VEL_MID}, got {v}"

    # reversible: MAX already happened above; back to 0 restores A
    board.phrase_morph_set(0)
    for step in range(4):
        v = board.track_par_get(0, VEL_LAYER, 0, step)
        assert v == VEL_A, f"reversible: velocity step {step} should be A ({VEL_A}), got {v}"

    _clean(board)


@pytest.mark.hardware
def test_morph_loopb_gate_max_matches_b(genv4):
    """Loop B pin: at pos MAX the gate must be exactly B's gate.
    The frozen-threshold crossfade is deterministic at full throw — every step's
    threshold (0..MAX-1) is < MAX, so every differing step shows B.  Verified via
    the source trg buffer (layer_bytes from trg_byte_get)."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_loop_b_phrases(board)

    # confirm live gate = A before arming
    layer_bytes, _ = board.trg_byte_get(0, trg_layer=0, step8_start=0, step8_count=1)
    assert layer_bytes[0] == GATE_A_BYTE, (
        f"preamble: live gate should be A ({GATE_A_BYTE:#010b}), got {layer_bytes[0]:#010b}"
    )

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"
    board.phrase_morph_set(PHRASE_MORPH_MAX)  # full throw -> all differing steps flip

    layer_bytes, _ = board.trg_byte_get(0, trg_layer=0, step8_start=0, step8_count=1)
    assert layer_bytes[0] == GATE_B_BYTE, (
        f"pos MAX gate should equal B ({GATE_B_BYTE:#010b}), got {layer_bytes[0]:#010b}"
    )

    _clean(board)


@pytest.mark.hardware
def test_morph_loopb_gate_zero_unchanged(genv4):
    """Loop B pin: a fresh arm left at pos 0 leaves the gate at the live A state
    (every threshold < 0 is impossible, so no step shows B)."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_loop_b_phrases(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"
    board.phrase_morph_set(0)  # force an explicit apply at pos=0

    layer_bytes, _ = board.trg_byte_get(0, trg_layer=0, step8_start=0, step8_count=1)
    assert layer_bytes[0] == GATE_A_BYTE, (
        f"pos 0 gate must stay at A ({GATE_A_BYTE:#010b}), got {layer_bytes[0]:#010b}"
    )

    _clean(board)


@pytest.mark.hardware
def test_morph_loopb_gate_reversible(genv4):
    """Loop B pin: the gate crossfade is REVERSIBLE, not a one-way ratchet.
    After a full-throw morph (gate == B), pulling pos back to 0 restores A exactly
    — the frozen-threshold model makes the gate deterministic and reversible.
    Verified via the source trg buffer."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_loop_b_phrases(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"

    # full throw -> B
    board.phrase_morph_set(PHRASE_MORPH_MAX)
    layer_bytes, _ = board.trg_byte_get(0, trg_layer=0, step8_start=0, step8_count=1)
    assert layer_bytes[0] == GATE_B_BYTE, (
        f"pos MAX gate should equal B ({GATE_B_BYTE:#010b}), got {layer_bytes[0]:#010b}"
    )

    # pull back to 0 -> A restored exactly (NOT stuck at B)
    board.phrase_morph_set(0)
    layer_bytes, _ = board.trg_byte_get(0, trg_layer=0, step8_start=0, step8_count=1)
    assert layer_bytes[0] == GATE_A_BYTE, (
        f"reversible: pos 0 after MAX must restore A ({GATE_A_BYTE:#010b}), "
        f"got {layer_bytes[0]:#010b}"
    )

    _clean(board)


@pytest.mark.hardware
def test_morph_loopb_snap_cc_reversible(genv4):
    """Loop B pin: groove_style and transpose_oct snap to B at full throw and
    restore arm-time A below it (reversible discrete snap).  Asserts on the SOURCE
    CC struct (SEQ_CC_Get) against the actually-stored endpoints."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    vals = _build_loop_b_phrases(board)
    # the snap is only meaningful if A and B actually differ after any clamping
    assert vals["style_a"] != vals["style_b"], "test setup: groove_style A/B must differ"
    assert vals["oct_a"] != vals["oct_b"], "test setup: transpose_oct A/B must differ"

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"

    # below MAX (incl. midpoint) = A; the snap only fires at full throw
    board.phrase_morph_set(MID)
    assert board.cc_get(0, CC.GROOVE_STYLE)  == vals["style_a"], "mid: groove_style stays A"
    assert board.cc_get(0, CC.TRANSPOSE_OCT) == vals["oct_a"],   "mid: transpose_oct stays A"

    # full throw = B
    board.phrase_morph_set(PHRASE_MORPH_MAX)
    assert board.cc_get(0, CC.GROOVE_STYLE)  == vals["style_b"], "MAX: groove_style == B"
    assert board.cc_get(0, CC.TRANSPOSE_OCT) == vals["oct_b"],   "MAX: transpose_oct == B"

    # reversible: pull back below MAX restores A
    board.phrase_morph_set(0)
    assert board.cc_get(0, CC.GROOVE_STYLE)  == vals["style_a"], "reversible: groove_style back to A"
    assert board.cc_get(0, CC.TRANSPOSE_OCT) == vals["oct_a"],   "reversible: transpose_oct back to A"

    _clean(board)


def _build_note_only_phrase(board: Board) -> None:
    """Phrase B differs from live A ONLY in note pitch (transpose/groove/gate left
    at reset default and identical A==B), so the note layer read back through the
    output mirror (track_par_get == SEQ_PAR_Get) is not contaminated by a transpose
    or groove morph — the only thing that moves across the sweep is the note swap."""
    board.ui_track_set(0)
    for step in range(4):
        board.track_par_set(0, NOTE_LAYER, 0, step, NOTE_B)
    assert board.phrase_capture(PHRASE_B), "note phrase capture should commit"
    for step in range(4):
        board.track_par_set(0, NOTE_LAYER, 0, step, NOTE_A)


@pytest.mark.hardware
def test_morph_loopb_note_swap_reversible(genv4):
    """Loop B Phase 1 pin: per-step NOTE swap.  pos 0 = A pitches, pos MAX = B
    pitches, reversible (MAX->0 restores A).  At an intermediate pos every step is
    EITHER A's pitch or B's pitch — a discrete swap, never an interpolated value
    (that's the whole point: no off-scale glide).

    track_par_get reads the output mirror (transpose applied), so this phrase keeps
    transpose constant A==B and asserts the swap against the OBSERVED endpoints —
    robust to any fixed transpose/force-scale offset."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)
    _build_note_only_phrase(board)

    assert board.phrase_morph_arm(PHRASE_B), "arm should commit"

    # observed endpoints (transpose is constant A==B; only the note swap moves)
    board.phrase_morph_set(0)
    a_vals = [board.track_par_get(0, NOTE_LAYER, 0, s) for s in range(4)]
    board.phrase_morph_set(PHRASE_MORPH_MAX)
    b_vals = [board.track_par_get(0, NOTE_LAYER, 0, s) for s in range(4)]
    for s in range(4):
        assert b_vals[s] != a_vals[s], (
            f"pos MAX note step {s} must differ from A ({a_vals[s]}), got {b_vals[s]}"
        )

    # midpoint: each step is discretely A or B (never an interpolated pitch)
    board.phrase_morph_set(MID)
    for s in range(4):
        v = board.track_par_get(0, NOTE_LAYER, 0, s)
        assert v in (a_vals[s], b_vals[s]), (
            f"midpoint note step {s} must be discrete A({a_vals[s]}) or B({b_vals[s]}), got {v}"
        )

    # reversible: back to 0 restores A exactly
    board.phrase_morph_set(0)
    for s in range(4):
        v = board.track_par_get(0, NOTE_LAYER, 0, s)
        assert v == a_vals[s], f"reversible: note step {s} back to A ({a_vals[s]}), got {v}"

    _clean(board)
