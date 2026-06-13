"""FEARLESS SWITCHING Stage C — CHECKPOINT / REVERT (the blessed anchor).

Design §9 (2026-06-11) + plan 2026-06-12 §1/§2: one-deep, organism-grain.
CHECKPOINT blesses all four groups' live state (incl. generator state) into
the internal anchor carrier MBSEQ_AN.V4 (a fifth, non-navigable "bank",
lazy-created on first CHECKPOINT). REVERT restores all four and sets every
group dirty (live now diverges from the working slot — the next switch writes
the reverted state back, the save-model inversion working normally). The
anchor reuses the bank record serializer wholesale, so gen state rides along
(V4 ext tag) and the organism comes back ALIVE.

The pins:
  - checkpoint -> jam (edit + drift the loop) -> revert: content + the
    sculpted organism come back byte-identical and ENGAGED, and REVERT sets
    every group dirty;
  - CHECKPOINT only reads live state -> it must NOT change the dirty mask;
  - REVERT before any CHECKPOINT (or in a never-checkpointed session) refuses
    cleanly — returns False, no crash, no live change;
  - the anchor is a separate file: a working-slot writeback (the Stage A
    auto-writeback) targets the user bank, never MBSEQ_AN.V4, so a REVERT
    after a writeback still restores the checkpointed state.

The gen-faithfulness pins need V4-sized bank records, which only freshly
created sessions have — the `genv4` fixture load-or-creates session GENV4 and
restores AUTOTEST afterwards (mirrors test_genstate_v4). The refuse pin needs
a session with NO anchor — the `noanchor` fixture uses a dedicated NOANC
session that the suite never CHECKPOINTs into, so it stays permanently
anchor-free across runs.

Destructive: overwrites GENV4 (0, 61) / (0, 62) — the established scratch
slots — and creates GENV4/NOANC anchor files. Never touches the A1-A3
baselines.
"""

import time

import pytest

from harness import Board, Button, CC, Page
from harness.sysex import DIAL_DEPTH, DIAL_RATE, RESET_DEFAULT


# The genv4 / noanchor fixtures may CREATE a session on first use — an async SD
# format worth tens of seconds. Override the suite's 10s default for this module.
pytestmark = pytest.mark.timeout(120)

GENV4_SESSION = "GENV4"
NOANC_SESSION = "NOANC"
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

LEN_CHECKPOINT = 7   # CC marker baked in before CHECKPOINT
LEN_JAM = 12         # the jam edit that REVERT must throw away

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


@pytest.fixture
def noanchor(board: Board):
    """A dedicated session the suite never CHECKPOINTs into, so it has no
    MBSEQ_AN.V4 — the deterministic no-anchor state for the refuse pin."""
    if board.session_name_get() != NOANC_SESSION:
        try:
            board.session_load(NOANC_SESSION, timeout=20.0)
        except (TimeoutError, RuntimeError):
            board.session_create(NOANC_SESSION)
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
    """The byte-identical persistence contract (shared with Stage B)."""
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
def test_checkpoint_revert_restores_sculpted_organism(genv4):
    """THE Stage C bundle pin. Engage + sculpt + a CC marker, CHECKPOINT, then
    jam (change the marker and drift the loop). REVERT brings the checkpointed
    state back: content restored, the organism byte-identical and ENGAGED, and
    mutation resumes. CHECKPOINT must not change the dirty mask; REVERT must set
    every group dirty."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    _engage(board)
    _sculpt(board)
    board.cc_set(0, CC.LENGTH, LEN_CHECKPOINT)
    g0 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g0 is not None and g0.engaged
    assert g0.loop != g0.anchor, "sculpt preamble: loop should have drifted off the anchor"

    # CHECKPOINT reads live state only — it must not touch the dirty mask.
    mask_before, _ = board.dirty_query()
    assert board.checkpoint(), "checkpoint should commit"
    assert board.anchor_present(), "anchor should exist after checkpoint"
    mask_after, _ = board.dirty_query()
    assert mask_after == mask_before, (
        f"CHECKPOINT must not change dirty (before={mask_before:#04x}, "
        f"after={mask_after:#04x})"
    )

    # Jam: change the marker and drift the loop away from the checkpoint.
    board.cc_set(0, CC.LENGTH, LEN_JAM)
    board.generator_tick_force(TRACK, INSTR)
    assert board.cc_get(0, CC.LENGTH) == LEN_JAM, "the jam edit should be live"
    g_jammed = board.generator_query(TRACK, INSTR)
    assert g_jammed.loop != g0.loop, "the jam should have drifted the loop"

    # REVERT: the organism comes back to the checkpoint.
    assert board.revert(), "revert should commit"

    assert board.cc_get(0, CC.LENGTH) == LEN_CHECKPOINT, (
        "content should be restored to the checkpointed value"
    )
    g1 = board.generator_query(TRACK, INSTR, with_anchor=True)
    assert g1 is not None, "the organism should come back"
    assert g1.engaged, "REVERT resumes the organism ENGAGED (comes back alive)"
    _assert_same_slot(g1, g0)

    # REVERT sets every group dirty (live != working slot now).
    mask_reverted, _ = board.dirty_query()
    assert (mask_reverted & ALL_GROUPS_DIRTY) == ALL_GROUPS_DIRTY, (
        f"REVERT should set every group dirty, mask={mask_reverted:#04x}"
    )

    # Alive = it still mutates (rate 77 / depth 55 over 64 steps: a no-op cycle
    # is astronomically unlikely).
    board.generator_tick_force(TRACK, INSTR)
    g2 = board.generator_query(TRACK, INSTR)
    assert g2.loop != g1.loop, "mutation should resume after the revert"

    for g in range(4):
        board.dirty_set(g, False)  # politeness: don't leave debris for teardown


@pytest.mark.hardware
def test_revert_without_anchor_refuses(noanchor):
    """REVERT in a session that was never CHECKPOINTed refuses cleanly: no
    anchor present, revert() returns False (not an exception, no crash)."""
    board = noanchor
    board.reset(RESET_DEFAULT)
    assert not board.anchor_present(), (
        "the NOANC session must have no anchor (the suite never checkpoints it)"
    )
    assert not board.revert(), "REVERT without an anchor must refuse (return False)"


@pytest.mark.hardware
def test_anchor_survives_working_slot_writeback(genv4):
    """The anchor is a separate file: the Stage A auto-writeback commits a dirty
    group into its USER-bank working slot, never into MBSEQ_AN.V4. So a switch
    away-and-back (which writes the jam into the working slot) leaves the anchor
    intact — a later REVERT still restores the checkpointed state."""
    board = genv4
    board.reset(RESET_DEFAULT)
    _park(board)

    board.cc_set(0, CC.LENGTH, LEN_CHECKPOINT)
    assert board.checkpoint(), "checkpoint should commit"

    # Jam, then switch away and back — the dirty group's jam is written back
    # into its working slot (SCRATCH_A) by the auto-writeback.
    board.cc_set(0, CC.LENGTH, LEN_JAM)
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B), "switch to B should commit"
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_A), "return to A should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_JAM, (
        "the auto-writeback should have committed the jam into the working slot"
    )

    # REVERT: the anchor was untouched by the working-slot writeback.
    assert board.revert(), "revert should commit"
    assert board.cc_get(0, CC.LENGTH) == LEN_CHECKPOINT, (
        "REVERT should restore the checkpointed state — the working-slot "
        "writeback must not have touched the anchor file"
    )

    for g in range(4):
        board.dirty_set(g, False)
