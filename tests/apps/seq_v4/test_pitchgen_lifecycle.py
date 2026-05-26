"""Phase H slot-lifetime coverage — anchor + MULT survive the lifecycle
gestures (DISENGAGE → re-ENGAGE, etc.) and are cleared on slot recycle
(BOUNCE-in-place, relocate). Plus one end-to-end techno-bass workflow
test that composes ENGAGE / MULT / mutate / ROLL / SNAP / BOUNCE in one
story to catch integration regressions.

Sibling to test_pitchgen_step6h.py (which pins the dispatch + initial
behavioral contracts) and test_pitchgen_mutate_path.py (which pins
the mutate-path math via CMD_GENERATOR_TICK_FORCE). The cuts are:

  - Step 6H (initial): "the gestures work and stamp the right state."
  - Mutate path:      "the rate-gated mutate path respects every dial."
  - Lifecycle (here): "state survives the lifecycle and is reset cleanly."
  - End-to-end:       "all of the above compose into a live workflow."
"""

import time

import pytest

from harness import Board, Button, Encoder, Page
from harness.sysex import (
    DIAL_DEPTH,
    DIAL_RATE,
)


PITCHGEN_TRACK = 0
GP1 = Button.GP(1)
GP3 = Button.GP(3)
GP4 = Button.GP(4)
GP6 = Button.GP(6)
GP7 = Button.GP(7)
GP8 = Button.GP(8)

SETTLE = 0.10
ENGAGE_MSG_MS = 750
DISENGAGE_MSG_MS = 750
ROLL_MSG_MS = 500
ANCHOR_MSG_MS = 750
SNAP_MSG_MS = 750
MULT_MSG_MS = 600
BOUNCE_MSG_MS = 1000

MULT_MUTE = 0
MULT_DOUBLE = 3


def _setup_pitchgen(board: Board) -> None:
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    time.sleep(SETTLE)


def _wait(ms: int) -> None:
    time.sleep((ms / 1000.0) + SETTLE)


def _engage(board: Board) -> None:
    board.press(GP1)
    _wait(ENGAGE_MSG_MS)


def _disengage(board: Board) -> None:
    board.button(Button.SELECT, depressed=False)
    board.press(GP1)
    board.button(Button.SELECT, depressed=True)
    _wait(DISENGAGE_MSG_MS)


def _scroll_cursor_to(board: Board, step: int) -> None:
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    remaining = step
    while remaining >= 3:
        board.encoder(Encoder.DATAWHEEL, +3)
        remaining -= 3
    if remaining > 0:
        board.encoder(Encoder.DATAWHEEL, +remaining)


# =============================================================================
# Slot-lifetime: state survives DISENGAGE → re-ENGAGE
# =============================================================================

@pytest.mark.hardware
def test_anchor_survives_disengage_reengage(board):
    """The anchored loop must survive a DISENGAGE → re-ENGAGE round-trip.
    Re-ENGAGE on an existing slot does NOT re-anchor (per phase-H design:
    auto-anchor only on first ENGAGE seed; subsequent ENGAGE flips back
    on without touching the captured identity). Otherwise SNAP would
    silently rebind to whatever the loop happened to be at re-engage time.
    """
    _setup_pitchgen(board)
    _engage(board)

    g_initial = board.generator_query(PITCHGEN_TRACK, 0)
    assert g_initial is not None
    assert g_initial.anchor_valid

    # ROLL to drift the loop away from the anchor.
    board.press(GP1)
    _wait(ROLL_MSG_MS)
    g_rolled = board.generator_query(PITCHGEN_TRACK, 0)
    assert g_rolled is not None
    assert g_rolled.loop != g_initial.loop, "ROLL should change loop"

    # DISENGAGE → re-ENGAGE.
    _disengage(board)
    mid = board.generator_query(PITCHGEN_TRACK, 0)
    assert mid is not None and not mid.engaged
    assert mid.anchor_valid, "anchor_valid lost across DISENGAGE"

    board.press(GP1)
    _wait(ENGAGE_MSG_MS)
    reengaged = board.generator_query(PITCHGEN_TRACK, 0)
    assert reengaged is not None and reengaged.engaged
    assert reengaged.anchor_valid

    # SNAP should still return to the ORIGINAL anchor, not the rolled
    # loop that was 'current' at re-engage time.
    board.press(GP4)
    _wait(SNAP_MSG_MS)
    snapped = board.generator_query(PITCHGEN_TRACK, 0)
    assert snapped is not None
    assert snapped.loop == g_initial.loop, (
        f"SNAP after DISENGAGE/re-ENGAGE must return to the ORIGINAL "
        f"auto-anchor, not the rolled state at re-engage.\n"
        f"  initial (anchor): {g_initial.loop.hex()}\n"
        f"  rolled:           {g_rolled.loop.hex()}\n"
        f"  post-snap:        {snapped.loop.hex()}"
    )


@pytest.mark.hardware
def test_mult_survives_disengage_reengage(board):
    """Per-step MULT codes must survive DISENGAGE → re-ENGAGE. The slot
    stays allocated (per §11 glossary: "DISENGAGE stops mutating but
    leaves the loop and locks so re-engaging picks up where you left
    off"); the mult[] array lives in the same slot and should follow
    the same rule.
    """
    _setup_pitchgen(board)
    _engage(board)

    # Stamp a known MULT pattern via the cycle gesture.
    pattern = [(4, 3), (12, 0), (29, 1)]   # (step, target_code)
    for step, target in pattern:
        _scroll_cursor_to(board, step)
        # Current is MULT_DEFAULT=2; cycle (target - 2) mod 4 times.
        cycles = (target - 2) % 4
        for _ in range(cycles):
            board.press(GP7)
            _wait(MULT_MSG_MS)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None
    for step, target in pattern:
        assert before.mult[step] == target, (
            f"setup failed: step {step} mult={before.mult[step]} ≠ {target}"
        )

    _disengage(board)
    board.press(GP1)
    _wait(ENGAGE_MSG_MS)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None
    for step, target in pattern:
        assert after.mult[step] == target, (
            f"MULT lost across DISENGAGE/re-ENGAGE at step {step}: "
            f"was {target}, now {after.mult[step]}"
        )


# =============================================================================
# Slot recycle: state cleared on BOUNCE-in-place
# =============================================================================

@pytest.mark.hardware
def test_bounce_clears_anchor_and_mult_for_next_slot(board):
    """BOUNCE-in-place frees the pool slot; a subsequent ENGAGE allocates
    a fresh slot. The new slot must have anchor_valid set (auto-anchor
    fires on first-ENGAGE seed), but the OLD slot's mult pattern must
    NOT leak in — every step should be back at MULT_DEFAULT=2.

    Regression target: a future change that forgets memset on slot free
    would leak the previous mult/anchor into the new gen.
    """
    _setup_pitchgen(board)
    _engage(board)

    # Stamp a distinctive MULT pattern via the harness's direct set.
    board.generator_mult_set(PITCHGEN_TRACK, 0, 7, MULT_MUTE)
    board.generator_mult_set(PITCHGEN_TRACK, 0, 21, MULT_DOUBLE)

    pre_bounce = board.generator_query(PITCHGEN_TRACK, 0)
    assert pre_bounce is not None
    assert pre_bounce.mult[7] == MULT_MUTE
    assert pre_bounce.mult[21] == MULT_DOUBLE

    # BOUNCE-in-place. Cursor must be on the engaged drum.
    _scroll_cursor_to(board, 0)
    board.press(GP8)
    _wait(BOUNCE_MSG_MS)

    bounced = board.generator_query(PITCHGEN_TRACK, 0)
    assert bounced is None, "BOUNCE-in-place must free the slot"

    # Re-ENGAGE — cursor still on D0 (no more F.3 auto-jump as of phase H).
    board.press(GP1)
    _wait(ENGAGE_MSG_MS)

    fresh = board.generator_query(PITCHGEN_TRACK, 0)
    assert fresh is not None and fresh.engaged
    assert fresh.anchor_valid, "fresh ENGAGE must auto-anchor"
    assert all(c == 2 for c in fresh.mult), (
        f"BOUNCE must clear MULT for the recycled slot — every step "
        f"should be MULT_DEFAULT (2); leaked codes: "
        f"{[(i, c) for i, c in enumerate(fresh.mult) if c != 2]}"
    )


# =============================================================================
# End-to-end techno-bass workflow — composes the whole phase H story
# =============================================================================

@pytest.mark.hardware
def test_techno_bass_workflow_end_to_end(board):
    """One scripted live-use story exercising the phase H gesture set
    end-to-end:

       1. ENGAGE on D0 (default bass-narrow range, auto-anchor)
       2. Stamp MULT=0 on a couple of "anchor steps" + MULT=3 on a
          "wildcard" step → soft hold-with-accent pattern
       3. Force several mutates → MULT=0 steps unchanged, others drift,
          MULT=3 steps drift the most
       4. ROLL → all unlocked steps reroll (ROLL ignores MULT)
       5. SNAP → loop returns to the auto-anchor bytes; MULT pattern
          preserved (MULT lives on top of the anchor identity)
       6. BOUNCE-in-place → slot freed, source par holds the snapped loop

    Catches integration bugs that the unit tests miss — e.g., SNAP that
    accidentally resets MULT, or BOUNCE that fails to persist the
    snapped loop into source.
    """
    _setup_pitchgen(board)
    _engage(board)

    # 1. confirm initial state
    s0 = board.generator_query(PITCHGEN_TRACK, 0)
    assert s0 is not None
    assert s0.engaged and s0.anchor_valid
    assert all(c == 2 for c in s0.mult), "fresh ENGAGE: every step MULT=2"

    # 2. stamp MULT pattern (steps 0 + 16 + 32 + 48 frozen, 7 + 23 + 39 + 55 doubled)
    frozen_steps = [0, 16, 32, 48]
    wild_steps = [7, 23, 39, 55]
    for s in frozen_steps:
        board.generator_mult_set(PITCHGEN_TRACK, 0, s, MULT_MUTE)
    for s in wild_steps:
        board.generator_mult_set(PITCHGEN_TRACK, 0, s, MULT_DOUBLE)

    # 3. force-mutate. rate=127 so the threshold sweeps as documented.
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_DEPTH, 127)
    for _ in range(6):
        board.generator_tick_force(PITCHGEN_TRACK, 0)

    s1 = board.generator_query(PITCHGEN_TRACK, 0)
    assert s1 is not None
    for fs in frozen_steps:
        assert s1.loop[fs] == s0.loop[fs], (
            f"MULT=0 step {fs} moved across force-mutates: "
            f"{s0.loop[fs]} → {s1.loop[fs]}"
        )

    # 4. ROLL — should reroll all unlocked steps, MULT-agnostic.
    board.press(GP1)
    _wait(ROLL_MSG_MS)
    s2 = board.generator_query(PITCHGEN_TRACK, 0)
    assert s2 is not None
    # Frozen steps (MULT=0) were preserved by mutate, but ROLL ignores MULT
    # — they reroll too. Confirm at least one MULT=0 step changed (proving
    # ROLL bypasses MULT) and the loop overall is now different from s1.
    rolled_frozen = sum(1 for fs in frozen_steps if s2.loop[fs] != s1.loop[fs])
    assert rolled_frozen > 0, (
        f"ROLL must ignore MULT — at least one frozen step should reroll. "
        f"frozen_step values pre={[s1.loop[fs] for fs in frozen_steps]} "
        f"post={[s2.loop[fs] for fs in frozen_steps]}"
    )
    assert s2.loop != s1.loop, "ROLL must change the loop overall"

    # 5. SNAP — should restore loop to s0 (the auto-anchor); MULT preserved.
    board.press(GP4)
    _wait(SNAP_MSG_MS)
    s3 = board.generator_query(PITCHGEN_TRACK, 0)
    assert s3 is not None
    assert s3.loop == s0.loop, (
        f"SNAP must restore loop to auto-anchor bytes.\n"
        f"  anchor (s0): {s0.loop.hex()}\n"
        f"  post-snap:   {s3.loop.hex()}"
    )
    # MULT pattern must survive SNAP — it lives on top of the loop, not
    # part of the identity.
    for fs in frozen_steps:
        assert s3.mult[fs] == MULT_MUTE, f"SNAP wiped MULT at step {fs}"
    for ws in wild_steps:
        assert s3.mult[ws] == MULT_DOUBLE, f"SNAP wiped MULT at step {ws}"

    # 6. BOUNCE-in-place — slot freed; source par holds the snapped bytes.
    _scroll_cursor_to(board, 0)
    board.press(GP8)
    _wait(BOUNCE_MSG_MS)
    bounced = board.generator_query(PITCHGEN_TRACK, 0)
    assert bounced is None, "BOUNCE-in-place must free the slot"

    # Sample a few steps from the par-layer and confirm they match the
    # snapped loop's bytes (BOUNCE freezes the source as last-written).
    for step in (0, 7, 16, 32):
        v = board.track_drum_par_get(PITCHGEN_TRACK, 0, step)
        assert v == s3.loop[step], (
            f"step {step} par-layer should hold the bounced loop byte; "
            f"loop={s3.loop[step]} but par={v}"
        )
