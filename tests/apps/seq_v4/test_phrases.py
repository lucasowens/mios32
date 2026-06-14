"""PHRASES — the snapshot library ("a set is a path").

Design §9 (2026-06-11, "an organism is a phrase") + plan
doc/plans/2026-06-13-phrases-snapshot-library.md. A phrase is a whole-organism
committed snapshot — capture/recall GENERALIZE CHECKPOINT/REVERT from the one
anchor (MBSEQ_AN.V4) to N named slots in the MBSEQ_PH.V4 sentinel bank (phrase
N -> patterns 4N..4N+3). Faithful (par+trg+CC+generators ride the V4 ext tag),
drift-free, and outside every for(bank<NUM_BANKS) loop. Track-grain
recombination happens LIVE (the RECOMBINE pull) before capture; a phrase
snapshots the result.

The pins:
  - capture -> jam (edit + drift the loop) -> recall: content + the sculpted
    organism come back byte-identical and ENGAGED, and recall sets every group
    dirty (REVERT semantics generalized);
  - NAVIGATION: two distinct phrases hold distinct snapshots — recall A / B / A
    each restores its own state (the "set is a path", N-slot proof that
    distinguishes phrases from the one-deep anchor). Also exercises out-of-order
    capture (phrase 5 before phrase 2 -> the file is written past EOF at the
    higher slot first; only written records are read back);
  - recall of an un-captured phrase refuses cleanly — returns False, no crash,
    no live change;
  - the phrase file is independent of the user banks: a working-slot writeback
    (Stage A auto-writeback) targets the user bank, never MBSEQ_PH.V4, so a
    recall after a switch-away-and-back still restores the captured state;
  - occupancy tracks capture: phrase_present is False before, True after.

The gen-faithfulness pins need V4-sized bank records, which only freshly
created sessions have — the `genv4` fixture load-or-creates session GENV4 and
restores AUTOTEST afterwards (mirrors test_fearless_checkpoint / test_genstate_v4).

Destructive: overwrites GENV4 (0, 61) / (0, 62) — the established scratch
slots — and creates GENV4's MBSEQ_PH.V4. Never touches the A1-A3 baselines (the
phrase file is a separate sentinel bank).
"""

import time

import pytest

from harness import Board, Button, CC, Page
from harness.sysex import DIAL_DEPTH, DIAL_RATE, RESET_DEFAULT


# The genv4 fixture may CREATE a session on first use — an async SD format worth
# tens of seconds. Override the suite's 10s default for this module.
pytestmark = pytest.mark.timeout(120)

GENV4_SESSION = "GENV4"
AUTOTEST_SESSION = "AUTOTEST"  # conftest.TEST_SESSION_NAME

SCRATCH_BANK = 0
SCRATCH_A = 61  # group 0's parked working slot
SCRATCH_B = 62  # the "other" slot

TRACK = 0
INSTR = 0

GP1 = Button.GP(1)
SETTLE = 0.10
ENGAGE_MSG_MS = 750

RATE_SCULPTED = 77
DEPTH_SCULPTED = 55

LEN_CAPTURE = 7   # CC marker baked in before capture
LEN_JAM = 12      # the jam edit that recall must throw away

# distinct markers for the navigation pin
LEN_PHRASE_A = 9
LEN_PHRASE_B = 21
PHRASE_A = 2
PHRASE_B = 5      # captured AFTER A but at a HIGHER slot -> out-of-order write

ALL_GROUPS_DIRTY = 0x0f


@pytest.fixture
def genv4(board: Board):
    """Run on a V4-sized session (fresh-format records reserve room for the gen
    sub-block); restore AUTOTEST on teardown."""
    if board.session_name_get() != GENV4_SESSION:
        try:
            board.session_load(GENV4_SESSION, timeout=20.0)
        except (TimeoutError, RuntimeError):
            board.session_create(GENV4_SESSION)
    yield board
    board.session_load(AUTOTEST_SESSION, timeout=20.0)


def _park(board: Board) -> None:
    """Both scratch slots get known content from freshly reset live state, then
    a raw load points group 0's working slot at SCRATCH_A with a clean dirty
    bit (the Stage A/B parking convention)."""
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_B), "slot B build should commit"
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A), "slot A build should commit"
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A), "park load should commit"
    mask, _ = board.dirty_query()
    assert not (mask & 0x01), f"park should leave group 0 clean, mask={mask:#04x}"


def _engage(board: Board, track: int = TRACK, instr: int = INSTR) -> None:
    """ENGAGE a Turing generator on (track, instr) via the PITCHGEN page."""
    board.track_drum_init(track)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(instr)
    time.sleep(SETTLE)
    board.press(GP1)
    time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)
    g = board.generator_query(track, instr)
    assert g is not None and g.engaged, "ENGAGE preamble failed"


def _sculpt(board: Board, track: int = TRACK, instr: int = INSTR) -> None:
    """Make the slot unmistakably non-default: dials, locks, MULT codes, and a
    loop drifted off its auto-anchor."""
    board.generator_dial_set(track, instr, DIAL_RATE, RATE_SCULPTED)
    board.generator_dial_set(track, instr, DIAL_DEPTH, DEPTH_SCULPTED)
    board.generator_lock_set(track, instr, 3, True)
    board.generator_lock_set(track, instr, 17, True)
    board.generator_mult_set(track, instr, 5, 3)   # 2×
    board.generator_mult_set(track, instr, 9, 0)   # mute
    for _ in range(3):
        board.generator_tick_force(track, instr)   # drift loop off the anchor


def _assert_same_slot(g_now, g_ref) -> None:
    """The byte-identical persistence contract (shared with FEARLESS Stage B/C)."""
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
def test_phrase_capture_recall_restores_sculpted_organism(genv4):
    """THE Stage A bundle pin. Engage + sculpt + a CC marker, capture phrase 3,
    then jam (change the marker and drift the loop). Recall brings the captured
    state back: content restored, the organism byte-identical and ENGAGED, and
    mutation resumes. Capture must not change the dirty mask (it reads live state
    only); recall must set every group dirty."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    _engage(board)
    _sculpt(board)
    board.cc_set(0, CC.LENGTH, LEN_CAPTURE)
    g0 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g0 is not None and g0.engaged
    assert g0.loop != g0.anchor, "sculpt preamble: loop should have drifted off the anchor"

    # capture reads live state only — it must not touch the dirty mask.
    mask_before, _ = board.dirty_query()
    assert board.phrase_capture(3), "capture should commit"
    assert board.phrase_present(3), "phrase 3 should be present after capture"
    mask_after, _ = board.dirty_query()
    assert mask_after == mask_before, (
        f"capture must not change dirty (before={mask_before:#04x}, "
        f"after={mask_after:#04x})"
    )

    # Jam: change the marker and drift the loop away from the captured state.
    board.cc_set(0, CC.LENGTH, LEN_JAM)
    board.generator_tick_force(TRACK, INSTR)
    assert board.cc_get(0, CC.LENGTH) == LEN_JAM, "the jam edit should be live"
    g_jammed = board.generator_query(TRACK, INSTR)
    assert g_jammed.loop != g0.loop, "the jam should have drifted the loop"

    # recall: the organism comes back to the captured snapshot.
    assert board.phrase_recall(3), "recall should commit"

    assert board.cc_get(0, CC.LENGTH) == LEN_CAPTURE, (
        "content should be restored to the captured value"
    )
    g1 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g1 is not None, "the organism should come back"
    assert g1.engaged, "recall resumes the organism ENGAGED (comes back alive)"
    _assert_same_slot(g1, g0)

    # recall sets every group dirty (live != working slot now — the inversion).
    mask_recalled, _ = board.dirty_query()
    assert (mask_recalled & ALL_GROUPS_DIRTY) == ALL_GROUPS_DIRTY, (
        f"recall should set every group dirty, mask={mask_recalled:#04x}"
    )

    # Alive = it still mutates (rate 77 / depth 55 over 64 steps: a no-op cycle
    # is astronomically unlikely).
    board.generator_tick_force(TRACK, INSTR)
    g2 = board.generator_query(TRACK, INSTR)
    assert g2.loop != g1.loop, "mutation should resume after the recall"

    for g in range(4):
        board.dirty_set(g, False)  # politeness: don't leave debris for teardown


@pytest.mark.hardware
def test_phrase_navigation_distinct_slots(genv4):
    """The "set is a path" pin — N independent waypoints, not one anchor. Capture
    two phrases with distinct content (B at a HIGHER slot than A, captured second
    -> the file is written out of order), then walk A / B / A and confirm each
    restores its own snapshot. This is the capability the snapshot library adds
    over the one-deep CHECKPOINT/REVERT."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    board.cc_set(0, CC.LENGTH, LEN_PHRASE_A)
    assert board.phrase_capture(PHRASE_A), "capture A should commit"

    board.cc_set(0, CC.LENGTH, LEN_PHRASE_B)
    assert board.phrase_capture(PHRASE_B), "capture B should commit (higher slot, out of order)"

    # walk the path: A -> B -> A, each restores its own snapshot
    assert board.phrase_recall(PHRASE_A), "recall A should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_PHRASE_A, "phrase A snapshot"

    assert board.phrase_recall(PHRASE_B), "recall B should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_PHRASE_B, "phrase B snapshot (distinct from A)"

    assert board.phrase_recall(PHRASE_A), "recall A again should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_PHRASE_A, (
        "returning to A restores A's snapshot — phrases are independent slots"
    )

    for g in range(4):
        board.dirty_set(g, False)


@pytest.mark.hardware
def test_phrase_recall_preserves_live_edit_to_working_slot(genv4):
    """FEARLESS 'never lose work' for phrase recall (the chosen semantics for the
    toggle-and-edit case). Recall A, nudge it live, then recall B: the nudge is
    written back to group 0's WORKING SLOT before B overwrites live — so it's
    recoverable, not silently discarded — while phrase A stays a pristine committed
    snapshot (recall A restores the captured value, NOT the nudge). Mirrors the
    pattern-switch WritebackIfDirty-before-Load; REVERT deliberately does not."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)  # group 0 parked at SCRATCH_A, clean

    board.cc_set(0, CC.LENGTH, LEN_PHRASE_A)
    assert board.phrase_capture(PHRASE_A), "capture A should commit"
    board.cc_set(0, CC.LENGTH, LEN_PHRASE_B)
    assert board.phrase_capture(PHRASE_B), "capture B should commit"

    # recall A, then nudge live (a value distinct from both phrase markers)
    assert board.phrase_recall(PHRASE_A), "recall A should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_PHRASE_A
    board.cc_set(0, CC.LENGTH, LEN_JAM)  # the live nudge that must not be lost

    # recall B -> writeback persists the nudge into group 0's working slot first
    assert board.phrase_recall(PHRASE_B), "recall B should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_PHRASE_B, "B's snapshot is live now"

    # NEVER LOST: the nudge is recoverable from the working slot. pattern_load is a
    # RAW load (bypasses the auto-writeback), so it reads SCRATCH_A as it was left.
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A), "raw-load the working slot"
    assert board.cc_get(0, CC.LENGTH) == LEN_JAM, (
        "the nudge made on recalled phrase A must have been written back to group "
        "0's working slot (FEARLESS never-lose-work)"
    )

    # A STAYS PRISTINE: recall A restores the CAPTURED value, not the nudge.
    assert board.phrase_recall(PHRASE_A), "recall A again should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_PHRASE_A, (
        "phrase A is immutable — recall restores the committed snapshot, not the nudge"
    )

    for g in range(4):
        board.dirty_set(g, False)


@pytest.mark.hardware
def test_recall_uncaptured_phrase_refuses(board: Board):
    """Recall of a phrase that was never captured this session refuses cleanly:
    not present, phrase_recall returns False (no exception, no crash, no live
    change). RESET_DEFAULT clears the session-scoped occupancy mask, so this
    touches no SD and leaves the A1-A3 baselines untouched."""
    board.reset(RESET_DEFAULT)
    assert not board.phrase_present(9), "a freshly reset session has no captured phrases"
    len_before = board.cc_get(0, CC.LENGTH)
    assert not board.phrase_recall(9), "recall of an uncaptured phrase must refuse (return False)"
    assert board.cc_get(0, CC.LENGTH) == len_before, "a refused recall must not change live state"


@pytest.mark.hardware
def test_phrase_survives_working_slot_writeback(genv4):
    """The phrase file is a separate sentinel bank: the Stage A auto-writeback
    commits a dirty group into its USER-bank working slot, never into
    MBSEQ_PH.V4. So a switch away-and-back (which writes the jam into the working
    slot) leaves the phrase intact — a later recall still restores the captured
    state."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    board.cc_set(0, CC.LENGTH, LEN_CAPTURE)
    assert board.phrase_capture(1), "capture should commit"

    # Jam, then switch away and back — the dirty group's jam is written back
    # into its working slot (SCRATCH_A) by the auto-writeback.
    board.cc_set(0, CC.LENGTH, LEN_JAM)
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B), "switch to B should commit"
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_A), "return to A should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_JAM, (
        "the auto-writeback should have committed the jam into the working slot"
    )

    # recall: the phrase was untouched by the working-slot writeback.
    assert board.phrase_recall(1), "recall should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_CAPTURE, (
        "recall should restore the captured state — the working-slot writeback "
        "must not have touched the phrase file"
    )

    for g in range(4):
        board.dirty_set(g, False)


@pytest.mark.hardware
def test_phrase_present_tracks_capture(genv4):
    """Occupancy mask correctness: a phrase is absent until captured, present
    after."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    assert not board.phrase_present(7), "phrase 7 absent before any capture"
    assert board.phrase_capture(7), "capture should commit"
    assert board.phrase_present(7), "phrase 7 present after capture"
    assert not board.phrase_present(8), "a different, uncaptured phrase stays absent"


@pytest.mark.hardware
def test_phrase_occupancy_survives_session_reload(genv4):
    """Cross-session occupancy probe (the gap this increment closes). Before it,
    the occupancy mask was RAM-only and zeroed on every session load, so a
    reloaded set went dark and refused EVERY recall until re-captured blind.
    SEQ_PATTERN_ProbePhrasesOnLoad now re-seeds the mask from MBSEQ_PH.V4 on disk
    at load (seq_file.c SEQ_FILE_LoadAllFiles), reading each phrase's base-pattern
    header (num_tracks>0 = occupied).

    Captures 0/3/7 with gaps between them: the gap-fill in SEQ_PATTERN_SnapshotWrite
    stamps EMPTY markers (num_tracks=0) into slots 1/2/4/5/6, so after reload those
    must read back ABSENT — not as undefined f_lseek-gap garbage that false-lights.
    The phrase DATA always persisted; this proves the *knowledge of which slots are
    real* now survives a reload, and that empty slots still refuse recall."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    # capture three phrases with distinct content markers, leaving gaps between
    markers = {0: 5, 3: 11, 7: 19}
    for n, length in markers.items():
        board.cc_set(0, CC.LENGTH, length)
        assert board.phrase_capture(n), f"capture {n} should commit"
    for n in markers:
        assert board.phrase_present(n), f"phrase {n} present after capture"
    for empty in (1, 2, 4, 5, 6):
        assert not board.phrase_present(empty), f"gap slot {empty} absent before reload"

    # THE cross-session event: reload the session. cmd_session_load always re-runs
    # SEQ_FILE_LoadAllFiles -> SEQ_PATTERN_ProbePhrasesOnLoad (re-seed from disk).
    board.session_load(GENV4_SESSION, timeout=20.0)

    # occupancy survived the reload (exactly the slots the old code left dark)
    for n in markers:
        assert board.phrase_present(n), (
            f"phrase {n} must survive reload — the cross-session probe should "
            f"re-seed occupancy from disk"
        )
    # gap slots must NOT false-light (the empty-marker, not undefined gap garbage)
    for empty in (1, 2, 4, 5, 6):
        assert not board.phrase_present(empty), (
            f"gap slot {empty} must stay absent after reload (empty-marker, "
            f"not a false-positive on undefined gap content)"
        )

    # and each phrase still recalls its OWN snapshot after the reload
    for n, length in markers.items():
        assert board.phrase_recall(n), f"recall {n} should commit after reload"
        assert board.cc_get(0, CC.LENGTH) == length, (
            f"phrase {n} should restore its captured marker ({length}) after reload"
        )

    # an empty slot still refuses cleanly after the reload (no garbage recall)
    len_before = board.cc_get(0, CC.LENGTH)
    assert not board.phrase_recall(2), "an empty slot must still refuse after reload"
    assert board.cc_get(0, CC.LENGTH) == len_before, "a refused recall must not change live state"

    for g in range(4):
        board.dirty_set(g, False)
