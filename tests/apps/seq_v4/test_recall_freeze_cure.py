"""Recall-freeze cure — the drift-gated writeback pin (design §9 2026-06-22).

Wall 1 of the 2026-06-21 flat-map: "travel with generators -> return via phrase
recall" froze the audible clock up to ~1.3 s, because recall's pre-writeback
serialized a ~290 ms SD SAVE for every group the generator had merely *dirtied by
wandering*. The cure gates that writeback on `phrase_drift` (MY deliberate edits)
instead of `seq_pattern_dirty` (which the generator's ambient auto-mutate also
sets): a group carrying only wander has drift clear, so recall skips its SAVE and
loads the pristine phrase -> no freeze. Un-captured wander is abandoned by design
(you'd have CAPTUREd the wander you wanted). Lives at two callsites:
  - phrase recall: SEQ_PATTERN_SnapshotRead -> SEQ_PATTERN_WritebackAllDrifted
  - running switch: SEQ_PATTERN_Handler   -> SEQ_PATTERN_WritebackIfDrifted

Before this file the cure rested on the by-ear GO plus code inspection alone:
existing tests pin drift-leak hygiene (test_capture_drift_leak) and the
deliberate-edit writeback on SWITCH (test_fearless_switching), but NOT the cure's
own invariant on RECALL. A refactor reverting WritebackAllDrifted -> the old
WritebackAllDirty would still pass every other test in the suite. This is that pin.

The invariant, stated once: *the number of writebacks a phrase recall fires equals
the number of DRIFTED groups (popcount of the drift mask) — never the number of
dirty ones.* Two tests instantiate its ends:
  - wander only     -> drift mask 0    -> ZERO writebacks   (the cure; the catch)
  - deliberate edit -> group drift bit -> exactly one write  (nothing lost)

The switch-path drift-gate (WritebackIfDrifted, seq_pattern.c:1516) shares the same
primitive and rationale; pinning it needs the transport RUNNING at the moment of
the switch (the stopped branch uses WritebackIfDirty on purpose — no live clock to
freeze), which is timing-sensitive to measure. Deferred; the recall pin plus the
existing fearless deliberate-edit switch pin bracket it.

Destructive: overwrites scratch slot (0, 60) and phrase 0 on the SD card. Never
touches the AUTOTEST A1-A3 baselines — group 0's working slot is parked on scratch,
so even a REGRESSED spurious writeback lands on scratch, not a baseline.
"""

import time

import pytest

from harness import Board, Button, CC, Page
from harness.sysex import (
    DIAL_DEPTH,
    DIAL_RANGE_MAX,
    DIAL_RANGE_MIN,
    DIAL_RATE,
    RESET_DEFAULT,
)


TRACK = 0
GROUP0_BIT = 0x01
INSTR = 0
GP1 = Button.GP(1)

SCRATCH_BANK = 0
SCRATCH_SLOT = 60  # group 0's parked working slot; never a baseline (0/1/2)
PHRASE = 0

LEN_SHORT = 15  # 16 steps = one measure -> fast track wraps -> fast wander
LEN_CAP = 8     # length baked into the captured phrase (positive control)
LEN_EDIT = 12   # the deliberate jam edit (differs from LEN_CAP)

ENGAGE_MS = 0.75
SETTLE = 0.10
WANDER_TIMEOUT = 25.0  # generous: at full reroll the first track wrap already mutates


def _park_group0_on_scratch(board: Board) -> None:
    """Point group 0's working slot at a scratch slot so any writeback (incl. a
    regressed spurious one) lands on scratch, never a baseline. A raw load repoints
    seq_pattern[0] and clears the dirty bit by construction (slot == live)."""
    assert board.pattern_save(TRACK, SCRATCH_BANK, SCRATCH_SLOT), "scratch build should commit"
    assert board.pattern_load(TRACK, SCRATCH_BANK, SCRATCH_SLOT), "park load should commit"
    mask, _ = board.dirty_query()
    assert not (mask & GROUP0_BIT), f"park should leave group 0 clean, mask={mask:#04x}"


def _engage_wandering_generator(board: Board) -> None:
    """Drum-mode pitch generator on TRACK, engaged and dialed to full reroll so a
    single track wrap visibly mutates the loop. track_drum_init MUST precede ENGAGE
    or GP1 refuses ('needs drum-mode track') and no slot is allocated."""
    board.track_drum_init(TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(INSTR)
    board.press(GP1)
    time.sleep(ENGAGE_MS + SETTLE)
    board.generator_dial_set(TRACK, INSTR, DIAL_RANGE_MIN, 36)  # C-1
    board.generator_dial_set(TRACK, INSTR, DIAL_RANGE_MAX, 84)  # C-5
    board.generator_dial_set(TRACK, INSTR, DIAL_RATE, 127)
    board.generator_dial_set(TRACK, INSTR, DIAL_DEPTH, 127)


def _wander_until_loop_changes(board: Board, baseline_loop: bytes) -> None:
    """Run the real engine (transport start) until the auto-mutate path visibly
    rerolls the generator loop, then stop. That reroll is what dirties the group
    WITHOUT drifting it (the SEQ_PATTERN_DirtySetTrack automutate gate)."""
    board.transport(start=True)
    try:
        deadline = time.monotonic() + WANDER_TIMEOUT
        while time.monotonic() < deadline:
            snap = board.generator_query(TRACK, INSTR)
            if snap is not None and snap.loop != baseline_loop:
                return
            time.sleep(0.25)
        raise AssertionError(
            "generator never mutated -> engine not advancing, or FREEZE engaged?"
        )
    finally:
        board.transport(start=False)


def _recall_writeback_delta(board: Board) -> tuple[int, int]:
    """Recall phrase 0; return (writeback_delta, drift_popcount_before_recall).
    The cure's invariant is that these two are equal."""
    drift = board.phrase_drift_mask()
    _, c0 = board.dirty_query()
    assert board.phrase_recall(PHRASE), "phrase 0 was captured -> recall must succeed"
    _, c1 = board.dirty_query()
    return (c1 - c0), bin(drift & 0x0f).count("1")


@pytest.mark.hardware
def test_recall_skips_writeback_for_wander_only(board):
    """THE cure pin. A generator wandering during playback dirties group 0 WITHOUT
    drifting it; recalling the phrase must fire ZERO writebacks (no ~290 ms SAVE ->
    no clock freeze) and restore the pristine captured loop (wander abandoned)."""
    board.reset(RESET_DEFAULT)
    _park_group0_on_scratch(board)
    _engage_wandering_generator(board)
    board.cc_set(TRACK, CC.LENGTH, LEN_SHORT)  # deliberate, but cleared by the capture below

    baseline = board.generator_query(TRACK, INSTR)
    assert baseline is not None, "engaged generator must expose a slot"

    assert board.phrase_capture(PHRASE), "capture should commit the pristine organism"
    assert board.phrase_drift_mask() == 0, "capture clears drift -> the committed reference"

    _wander_until_loop_changes(board, baseline.loop)

    # Precondition: wander DIRTIED group 0 but did NOT DRIFT it — the whole point.
    mask, _ = board.dirty_query()
    assert mask & GROUP0_BIT, "generator wander should have dirtied group 0"
    assert board.phrase_drift_mask() == 0, (
        "ambient wander must NOT set phrase_drift (the DirtySetTrack automutate gate)"
    )

    # The cure: recall fires ZERO writebacks despite the dirty group.
    delta, drift_pop = _recall_writeback_delta(board)
    assert drift_pop == 0
    assert delta == 0, (
        f"RECALL-FREEZE REGRESSION: a wander-only organism wrote back {delta} "
        f"group(s) on recall (expected 0). WritebackAllDrifted reverted to "
        f"WritebackAllDirty? Each spurious writeback is a ~290 ms clock freeze."
    )

    # Wander abandoned: the recalled loop is the pristine captured baseline.
    after = board.generator_query(TRACK, INSTR)
    assert after is not None
    assert after.loop == baseline.loop, (
        "recall should restore the captured loop (un-captured wander abandoned)"
    )


@pytest.mark.hardware
def test_recall_writes_back_deliberate_edit(board):
    """Positive control: a DELIBERATE edit (drift set) DOES write back on recall
    (FEARLESS 'never lose work') and the live organism returns to the pristine
    phrase. Proves the writeback counter moves when it should -> the wander test's
    'delta == 0' is a live signal, not a dead counter."""
    board.reset(RESET_DEFAULT)
    _park_group0_on_scratch(board)

    board.cc_set(TRACK, CC.LENGTH, LEN_CAP)
    assert board.phrase_capture(PHRASE), "capture should commit"
    assert board.phrase_drift_mask() == 0, "capture clears drift"
    assert board.cc_get(TRACK, CC.LENGTH) == LEN_CAP

    board.cc_set(TRACK, CC.LENGTH, LEN_EDIT)  # the deliberate jam
    assert board.phrase_drift_mask() & GROUP0_BIT, "a hands-on CC edit must drift group 0"
    assert board.cc_get(TRACK, CC.LENGTH) == LEN_EDIT

    delta, drift_pop = _recall_writeback_delta(board)
    assert drift_pop == 1
    assert delta == 1, (
        f"a drifted group must write back exactly once on recall (nothing lost); "
        f"got {delta} writeback(s) for {drift_pop} drifted group(s)"
    )
    assert board.cc_get(TRACK, CC.LENGTH) == LEN_CAP, (
        "recall must restore the pristine captured phrase over the live edit"
    )
