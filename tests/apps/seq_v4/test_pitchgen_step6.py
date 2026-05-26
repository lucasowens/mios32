"""Phase G (§8 step 6): PITCHGEN polish — ROLL, LOCK, cursor, depth, contour.

Mix of two test flavors:
  - **Dispatch tests** (LCD-scrape): pin the UI surface. Right popup, right
    LCD field flips, right state transitions.
  - **Behavioral tests** (CMD_GENERATOR_QUERY): pin the actual math. LOCK
    really survives ROLL; the bitmap really clears on slot recycle; locks
    really persist across DISENGAGE→ENGAGE.

Still listen-test only:
  - mutate_loop's measure-boundary behavior (would need a measure-trigger
    testctrl command, or a play-and-wait cycle; deferred — the ROLL path
    exercises the same lock-respect code and the math is shared)
  - depth=0 freezing (same reason; ROLL ignores depth so doesn't cover it)
  - contour distribution shape (statistical; possible but defer)
"""

import time

import pytest

from harness import Board, Button, Encoder, Page


PITCHGEN_TRACK = 0
GP1 = Button.GP(1)
GP6 = Button.GP(6)
GP_ENC_6 = Encoder.GP(6)
GP_ENC_7 = Encoder.GP(7)

# Popup durations — same pattern as test_pitchgen_bounce.py: SEQ_UI_Msg
# overlays the page LCD; sleep through it before reading the LCD.
SETTLE = 0.15
ENGAGE_MSG_MS = 750
ROLL_MSG_MS = 500
LOCK_MSG_MS = 500
DISENGAGE_MSG_MS = 750
HINT_MSG_MS = 1000


def _setup_pitchgen(board: Board) -> None:
    """Reinit track 0 to drum mode, open PITCHGEN, park cursor on drum 0."""
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    # Datawheel cursor starts at 0 from RESET_STATE. PITCHGEN datawheel
    # handler clamps to LOOP_LEN-1 = 63, but ui_selected_step is global
    # so any prior test could have moved it; reset by scrolling -64.
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    time.sleep(SETTLE)


def _lcd_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _wait_msg_clear(ms: int) -> None:
    time.sleep((ms / 1000.0) + SETTLE)


def _engage(board: Board) -> None:
    board.press(GP1)
    _wait_msg_clear(ENGAGE_MSG_MS)


@pytest.mark.hardware
def test_engage_then_gp1_rolls_not_disengages(board):
    """Phase G changes GP1-while-engaged from DISENGAGE to ROLL.

    A second press of GP1 after ENGAGE should fire the 'ROLL' popup and
    leave state:ENGAGED — distinguishing this from phase F's toggle
    behavior where the second press would have flipped to disengaged.
    """
    _setup_pitchgen(board)

    _engage(board)
    assert "state:ENGAGED" in _lcd_text(board)

    board.press(GP1)
    # Read LCD while the ROLL popup is still showing — it's a different
    # message body from ENGAGED/DISENGAGED, that's the dispatch witness.
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "ROLL" in popup, (
        f"GP1-while-engaged should fire the ROLL popup; LCD reads:\n{popup}"
    )

    _wait_msg_clear(ROLL_MSG_MS)
    text = _lcd_text(board)
    assert "state:ENGAGED" in text, (
        f"after ROLL the gen should still be engaged (not disengaged):\n{text}"
    )


@pytest.mark.hardware
def test_select_held_gp1_disengages(board):
    """SELECT modifier + GP1 = explicit DISENGAGE (the escape hatch).

    Without the modifier, GP1 would have rolled instead. With SELECT
    held, the DISENGAGED popup fires and state drops to 'disengaged'
    (slot kept — not freed, distinct from BOUNCE).
    """
    _setup_pitchgen(board)

    _engage(board)
    assert "state:ENGAGED" in _lcd_text(board)

    # Hold SELECT, press GP1, release SELECT.
    board.button(Button.SELECT, depressed=False)
    board.press(GP1)
    board.button(Button.SELECT, depressed=True)
    _wait_msg_clear(DISENGAGE_MSG_MS)

    text = _lcd_text(board)
    assert "state:disengaged" in text, (
        f"SEL+GP1 while engaged should disengage (state:disengaged), "
        f"not free the slot:\n{text}"
    )


@pytest.mark.hardware
def test_lock_toggle_flips_lcd_bracket(board):
    """GP6 press toggles the cursor step's lock; LCD row 1 RHS shows
    '[ ]' when unlocked and '[L]' when locked. This is the on-screen
    witness of the per-step LOCK state.
    """
    _setup_pitchgen(board)
    _engage(board)

    text = _lcd_text(board)
    assert "[ ]" in text, (
        f"freshly engaged gen should report cursor step unlocked '[ ]':\n{text}"
    )

    board.press(GP6)
    _wait_msg_clear(LOCK_MSG_MS)
    text = _lcd_text(board)
    assert "[L]" in text, (
        f"after GP6 LOCK press, cursor step should show '[L]':\n{text}"
    )

    board.press(GP6)
    _wait_msg_clear(LOCK_MSG_MS)
    text = _lcd_text(board)
    assert "[ ]" in text, (
        f"second GP6 press should unlock; back to '[ ]':\n{text}"
    )


@pytest.mark.hardware
def test_gp6_lock_refuses_when_no_slot(board):
    """GP6 LOCK press with no allocated gen slot prints the 'ENGAGE first'
    guidance — the bare LOCK gesture must not silently no-op.
    """
    _setup_pitchgen(board)
    # NOT engaged.

    board.press(GP6)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "ENGAGE first" in popup, (
        f"LOCK press without a slot should show ENGAGE-first guidance:\n{popup}"
    )


@pytest.mark.hardware
def test_datawheel_moves_cursor_visible_in_lcd(board):
    """Turning the datawheel moves the cursor (ui_selected_step) and the
    LCD 'Stp:NN' field updates. Only meaningful when engaged — the field
    is only shown in the engaged-state hint.
    """
    _setup_pitchgen(board)
    _engage(board)

    text = _lcd_text(board)
    assert "Stp:01" in text, (
        f"after fresh setup the cursor should be at step 1:\n{text}"
    )

    # Move cursor +5 steps.
    board.encoder(Encoder.DATAWHEEL, +3)
    board.encoder(Encoder.DATAWHEEL, +2)
    time.sleep(SETTLE)
    text = _lcd_text(board)
    assert "Stp:06" in text, (
        f"after +5 datawheel, cursor should be at step 6:\n{text}"
    )


@pytest.mark.hardware
def test_depth_encoder_changes_lcd_value(board):
    """GP6 encoder turn dials mutation_depth; the 'D:NNN' field on LCD
    row 1 LHS reflects it live.
    """
    _setup_pitchgen(board)
    _engage(board)

    text = _lcd_text(board)
    assert "D:120" in text, (
        f"default depth should be 120 (techno default tuned 2026-05-26):\n{text}"
    )

    # Dial down.
    for _ in range(10):
        board.encoder(GP_ENC_6, -3)
    time.sleep(SETTLE)
    text = _lcd_text(board)
    # The exact value isn't important — what matters is it's not 120.
    assert "D:120" not in text, (
        f"after dialing GP6 down, depth should no longer be 120:\n{text}"
    )


@pytest.mark.hardware
def test_contour_encoder_cycles_shape(board):
    """GP7 encoder cycles through UNIFORM/LOW/HIGH/TRIANGLE; LCD 'Ct:'
    field flips between the 3-char shape names.

    Default is TRIANGLE (techno-tuned 2026-05-26), the top of the
    enum range. SEQ_UI_Var8_Inc clamps (no wrap), so we dial DOWN to
    walk the cycle.
    """
    _setup_pitchgen(board)
    _engage(board)

    text = _lcd_text(board)
    assert "Ct:Tri" in text, (
        f"default contour should be TRIANGLE:\n{text}"
    )

    board.encoder(GP_ENC_7, -1)
    time.sleep(SETTLE)
    text = _lcd_text(board)
    assert "Ct:Hi" in text, (
        f"after -1 GP7 turn, contour should drop to HIGH_BIAS:\n{text}"
    )

    board.encoder(GP_ENC_7, -2)
    time.sleep(SETTLE)
    text = _lcd_text(board)
    assert "Ct:Uni" in text, (
        f"after another -2, contour should reach UNIFORM:\n{text}"
    )


# ===========================================================================
# Behavioral tests — pin the actual math via CMD_GENERATOR_QUERY.
# ===========================================================================


def _scroll_cursor_to(board: Board, step: int) -> None:
    """Move ui_selected_step to `step` from wherever it is now. Brute-force:
    scroll all the way to 0 first, then up to `step`. Cheap (~22 calls)."""
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    # Now at step 0.
    remaining = step
    while remaining >= 3:
        board.encoder(Encoder.DATAWHEEL, +3)
        remaining -= 3
    if remaining > 0:
        board.encoder(Encoder.DATAWHEEL, +remaining)


def _lock_steps(board: Board, steps: list[int]) -> None:
    """Toggle locks on for the given step indices via the GP6 button.
    Assumes none of the steps are currently locked. Pops the LOCK msg each
    time — sleep through so subsequent LCD reads aren't masked."""
    for s in steps:
        _scroll_cursor_to(board, s)
        board.press(GP6)
        _wait_msg_clear(LOCK_MSG_MS)


@pytest.mark.hardware
def test_roll_preserves_locked_steps_in_loop(board):
    """ROLL must leave the loop[] entries of LOCKed steps untouched.

    Snapshot the loop after ENGAGE; lock a handful of steps; ROLL; query
    the loop again — locked positions must match the original byte-for-byte.
    """
    _setup_pitchgen(board)
    _engage(board)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None and before.engaged, "expected engaged slot"

    locked_steps = [3, 17, 42]
    _lock_steps(board, locked_steps)

    # Snapshot loop AFTER locking but BEFORE ROLL — the lock toggle itself
    # doesn't change loop[] (it only flips the bitmap), so this is identical
    # to `before.loop`, but reading it again proves that.
    after_lock = board.generator_query(PITCHGEN_TRACK, 0)
    assert after_lock is not None
    assert after_lock.loop == before.loop, "lock toggle must not change loop[]"
    for s in locked_steps:
        assert after_lock.locks[s], f"step {s} should be locked"

    # ROLL: GP1 while engaged.
    board.press(GP1)
    _wait_msg_clear(ROLL_MSG_MS)

    after_roll = board.generator_query(PITCHGEN_TRACK, 0)
    assert after_roll is not None and after_roll.engaged

    for s in locked_steps:
        assert after_roll.loop[s] == before.loop[s], (
            f"step {s} is LOCKED but its loop value changed across ROLL: "
            f"was {before.loop[s]}, now {after_roll.loop[s]}"
        )
        assert after_roll.locks[s], f"step {s} should still be locked after ROLL"


@pytest.mark.hardware
def test_roll_actually_rerolls_unlocked_steps(board):
    """Sanity that ROLL isn't a no-op: at least one unlocked step changes.

    Counter to the LOCK test above. With 64 unlocked steps and a fresh
    random reroll over 1..127, the chance every single one happens to land
    on its previous value is vanishingly small (well below any test
    tolerance), so >=1 change is a safe assertion.
    """
    _setup_pitchgen(board)
    _engage(board)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None

    board.press(GP1)   # ROLL
    _wait_msg_clear(ROLL_MSG_MS)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None

    changed = sum(1 for i in range(64) if before.loop[i] != after.loop[i])
    assert changed > 0, (
        f"ROLL should reroll unlocked steps but the loop is byte-identical "
        f"before/after — either the random source is stuck or roll_loop "
        f"silently no-op'd. before={before.loop.hex()}\nafter={after.loop.hex()}"
    )


@pytest.mark.hardware
def test_locks_survive_disengage_reengage(board):
    """DISENGAGE keeps the slot allocated; the lock bitmap must persist.

    The §11-glossary contract: DISENGAGE stops mutating but leaves the loop
    (and locks) so re-engaging picks up where you left off. A regression
    that cleared locks on DISENGAGE would silently undo the user's careful
    per-step LOCK pattern the moment they paused mutation.
    """
    _setup_pitchgen(board)
    _engage(board)

    locked = [7, 23]
    _lock_steps(board, locked)

    # SEL+GP1 = DISENGAGE (slot stays).
    board.button(Button.SELECT, depressed=False)
    board.press(GP1)
    board.button(Button.SELECT, depressed=True)
    _wait_msg_clear(DISENGAGE_MSG_MS)

    mid = board.generator_query(PITCHGEN_TRACK, 0)
    assert mid is not None, "DISENGAGE should keep the slot allocated"
    assert not mid.engaged, "after DISENGAGE the slot should report engaged=0"
    for s in locked:
        assert mid.locks[s], f"step {s} lock lost across DISENGAGE"

    # Re-engage via GP1 (now in disengaged state, GP1 = ENGAGE).
    board.press(GP1)
    _wait_msg_clear(ENGAGE_MSG_MS)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None and after.engaged
    for s in locked:
        assert after.locks[s], f"step {s} lock lost across re-ENGAGE"


@pytest.mark.hardware
def test_bounce_clears_locks_for_next_slot(board):
    """BOUNCE-in-place frees the pool slot; a subsequent ENGAGE allocates a
    fresh slot with all locks cleared.

    Regression target: a future change that forgot to memset the freed slot
    (or shared lock storage across slot lifetimes) would leak the old lock
    bitmap into the new gen — silently breaking the "fresh" contract.

    The phase F.3 ENGAGE auto-jump was withdrawn in phase H — user pick
    always wins. So after BOUNCE-in-place the cursor stays on D0, and a
    second GP1 press allocates a fresh slot at D0 (the bounced loop's
    par bytes don't gate ENGAGE; only the lock bitmap of the previous
    slot matters here). UNDO is the documented safety net for the
    par-buffer overwrite this triggers.
    """
    _setup_pitchgen(board)
    _engage(board)

    locked = [5, 13, 50]
    _lock_steps(board, locked)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None
    for s in locked:
        assert before.locks[s], f"step {s} should be locked pre-BOUNCE"

    # BOUNCE-in-place: cursor must be on the engaged drum for the in-place
    # branch (lock toggles moved the cursor; reset it).
    _scroll_cursor_to(board, 0)
    board.press(Button.GP(8))
    _wait_msg_clear(1000)  # BOUNCE_MSG_MS

    # D0's slot should be freed.
    queried = board.generator_query(PITCHGEN_TRACK, 0)
    assert queried is None, "BOUNCE-in-place should free the pool slot at D0"

    # Re-ENGAGE — cursor still on D0, so the fresh slot lands at D0
    # (post-phase-H: no auto-jump).
    board.press(GP1)
    _wait_msg_clear(ENGAGE_MSG_MS)

    fresh = board.generator_query(PITCHGEN_TRACK, 0)
    assert fresh is not None and fresh.engaged, (
        "re-ENGAGE on cursor D0 should allocate a fresh slot there"
    )
    assert not any(fresh.locks), (
        f"fresh ENGAGE after BOUNCE should produce a slot with no locks; "
        f"got locks at: {[i for i, b in enumerate(fresh.locks) if b]}"
    )
