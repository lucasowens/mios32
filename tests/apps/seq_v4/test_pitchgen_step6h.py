"""Phase H (§8 step 6 follow-on): PITCHGEN gestures — ANCHOR / SNAP / MULT.

Mix of two test flavors mirroring test_pitchgen_step6.py:
  - **Dispatch tests** (LCD-scrape): pin the UI surface. Right popups,
    right LCD field flips for the new gestures (GP3 ANCHOR, GP4 SNAP,
    GP7 MULT).
  - **Behavioral tests** (CMD_GENERATOR_QUERY): pin the actual semantics.
    Auto-anchor at ENGAGE; SNAP restores loop[] to anchored bytes; ANCHOR
    refreshes the captured identity; MULT cycle advances per-step code.

Still listen-test only (matches step 6 carveouts):
  - MULT's *effect* on the measure-boundary mutate path (would need a
    measure-trigger testctrl command; ROLL ignores MULT by design so it
    can't be used as a proxy here).
"""

import time

import pytest

from harness import Board, Button, Encoder, Page


PITCHGEN_TRACK = 0
GP1 = Button.GP(1)
GP3 = Button.GP(3)
GP4 = Button.GP(4)
GP7 = Button.GP(7)

SETTLE = 0.15
ENGAGE_MSG_MS = 750
ROLL_MSG_MS = 500
ANCHOR_MSG_MS = 750
SNAP_MSG_MS = 750
MULT_MSG_MS = 600
NO_ANCHOR_MSG_MS = 1500
NOT_ENGAGED_MSG_MS = 1000


def _setup_pitchgen(board: Board) -> None:
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    # Reset datawheel cursor — global ui_selected_step may have drifted.
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    time.sleep(SETTLE)


def _lcd_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _popup_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _wait(ms: int) -> None:
    time.sleep((ms / 1000.0) + SETTLE)


def _engage(board: Board) -> None:
    board.press(GP1)
    _wait(ENGAGE_MSG_MS)


def _scroll_cursor_to(board: Board, step: int) -> None:
    """Park ui_selected_step at `step` by brute-force scrolling from 0."""
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    remaining = step
    while remaining >= 3:
        board.encoder(Encoder.DATAWHEEL, +3)
        remaining -= 3
    if remaining > 0:
        board.encoder(Encoder.DATAWHEEL, +remaining)


# =============================================================================
# Dispatch tests — LCD popup + LCD field
# =============================================================================

@pytest.mark.hardware
def test_gp3_anchor_pops_anchored(board):
    """GP3 button press while engaged pops 'ANCHORED' popup."""
    _setup_pitchgen(board)
    _engage(board)

    board.press(GP3)
    time.sleep(SETTLE)
    text = _popup_text(board)
    assert "ANCHORED" in text, f"expected ANCHORED popup, got:\n{text}"


@pytest.mark.hardware
def test_gp3_anchor_without_slot_says_engage_first(board):
    """GP3 with no allocated slot pops the ENGAGE-first guidance."""
    _setup_pitchgen(board)

    board.press(GP3)
    time.sleep(SETTLE)
    text = _popup_text(board)
    assert "ENGAGE first to ANCHOR" in text, (
        f"expected ENGAGE-first guidance, got:\n{text}"
    )


@pytest.mark.hardware
def test_gp4_snap_pops_snapped(board):
    """GP4 button press while engaged (auto-anchored) pops 'SNAPPED'."""
    _setup_pitchgen(board)
    _engage(board)

    board.press(GP4)
    time.sleep(SETTLE)
    text = _popup_text(board)
    assert "SNAPPED" in text, f"expected SNAPPED popup, got:\n{text}"


@pytest.mark.hardware
def test_gp7_mult_cycle_lcd_shows_label(board):
    """GP7 cycles cursor-step MULT 0→1→2→3→0; LCD row 1 RHS shows M:label.

    Default after ENGAGE is code 2 → 'M:1x'. First press advances to code 3
    → 'M:2x'. Default cursor is step 0 (post _setup_pitchgen).
    """
    _setup_pitchgen(board)
    _engage(board)

    text = _lcd_text(board)
    assert "M:1x" in text, f"default MULT at cursor should be 1x:\n{text}"

    board.press(GP7)
    _wait(MULT_MSG_MS)
    text = _lcd_text(board)
    assert "M:2x" in text, f"after 1 GP7 press, expected M:2x:\n{text}"

    board.press(GP7)
    _wait(MULT_MSG_MS)
    text = _lcd_text(board)
    assert "M:0x" in text, f"after 2 GP7 presses, expected M:0x (mute):\n{text}"

    board.press(GP7)
    _wait(MULT_MSG_MS)
    text = _lcd_text(board)
    assert "M:0.5x" in text, f"after 3 GP7 presses, expected M:0.5x:\n{text}"

    board.press(GP7)
    _wait(MULT_MSG_MS)
    text = _lcd_text(board)
    assert "M:1x" in text, (
        f"after 4 GP7 presses (one full cycle), expected M:1x again:\n{text}"
    )


# =============================================================================
# Behavioral tests — generator_query
# =============================================================================

@pytest.mark.hardware
def test_engage_auto_anchors_and_defaults_mult(board):
    """ENGAGE must auto-snapshot the seeded loop into anchor[] and set the
    per-step MULT codes to MULT_DEFAULT (=2) for all 64 steps.

    Regression target: a future refactor that forgot the auto-anchor would
    leave anchor_valid=0, breaking SNAP-from-fresh-engage. A refactor that
    forgot the explicit mult[] init would leave the memset-zeroed mute code
    (=0) on every step, silently freezing the loop's mutation.
    """
    _setup_pitchgen(board)
    _engage(board)

    g = board.generator_query(PITCHGEN_TRACK, 0)
    assert g is not None and g.engaged
    assert g.anchor_valid, "ENGAGE must auto-anchor (anchor_valid=true)"
    assert all(code == 2 for code in g.mult), (
        f"every step's MULT should default to 2 (1x); got: {g.mult}"
    )


@pytest.mark.hardware
def test_snap_restores_loop_to_anchor(board):
    """After ROLL changes the loop, SNAP must restore it byte-for-byte to
    the auto-anchored snapshot from ENGAGE.
    """
    _setup_pitchgen(board)
    _engage(board)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None

    # ROLL to change the loop.
    board.press(GP1)
    _wait(ROLL_MSG_MS)
    mid = board.generator_query(PITCHGEN_TRACK, 0)
    assert mid is not None
    assert mid.loop != before.loop, (
        "sanity: ROLL should change loop (else the test can't observe SNAP)"
    )

    # SNAP via GP4.
    board.press(GP4)
    _wait(SNAP_MSG_MS)
    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None
    assert after.loop == before.loop, (
        f"SNAP must restore loop[] to the anchored bytes.\n"
        f"  pre-roll: {before.loop.hex()}\n"
        f"  post-roll: {mid.loop.hex()}\n"
        f"  post-snap: {after.loop.hex()}"
    )
    assert after.engaged, "SNAP must not disengage"


@pytest.mark.hardware
def test_anchor_refresh_captures_new_identity(board):
    """ANCHOR after ROLL re-snapshots the *current* loop as the new identity.
    A subsequent SNAP returns to the new anchor, NOT to the original ENGAGE
    seed.
    """
    _setup_pitchgen(board)
    _engage(board)

    original = board.generator_query(PITCHGEN_TRACK, 0)
    assert original is not None

    # Roll to a different loop content.
    board.press(GP1)
    _wait(ROLL_MSG_MS)
    rolled = board.generator_query(PITCHGEN_TRACK, 0)
    assert rolled is not None
    assert rolled.loop != original.loop

    # ANCHOR the rolled loop as new identity.
    board.press(GP3)
    _wait(ANCHOR_MSG_MS)

    # Roll again to drift away.
    board.press(GP1)
    _wait(ROLL_MSG_MS)
    drifted = board.generator_query(PITCHGEN_TRACK, 0)
    assert drifted is not None
    assert drifted.loop != rolled.loop, "sanity: second ROLL should differ"

    # SNAP — should restore to the *rolled* anchor, not the original.
    board.press(GP4)
    _wait(SNAP_MSG_MS)
    snapped = board.generator_query(PITCHGEN_TRACK, 0)
    assert snapped is not None
    assert snapped.loop == rolled.loop, (
        f"SNAP after ANCHOR-refresh must restore to the refreshed anchor.\n"
        f"  rolled (new anchor): {rolled.loop.hex()}\n"
        f"  drifted:             {drifted.loop.hex()}\n"
        f"  post-snap:           {snapped.loop.hex()}"
    )
    assert snapped.loop != original.loop, (
        "SNAP must NOT silently return to the original ENGAGE seed once the "
        "user has re-anchored"
    )


@pytest.mark.hardware
def test_mult_cycle_only_touches_cursor_step(board):
    """GP7 advances the MULT code only at the cursor step; other steps stay
    at MULT_DEFAULT (=2). Proves the packed-nibble addressing is correct.
    """
    _setup_pitchgen(board)
    _engage(board)

    # Move cursor to step 17 (odd step — exercises the high-nibble path) and
    # cycle MULT three times: 2 → 3 → 0 → 1.
    _scroll_cursor_to(board, 17)
    for _ in range(3):
        board.press(GP7)
        _wait(MULT_MSG_MS)

    g = board.generator_query(PITCHGEN_TRACK, 0)
    assert g is not None
    assert g.mult[17] == 1, (
        f"step 17 MULT should be 1 (=0.5x) after 3 cycles from default 2; "
        f"got {g.mult[17]}"
    )
    other_codes = [(i, c) for i, c in enumerate(g.mult) if i != 17 and c != 2]
    assert not other_codes, (
        f"only step 17 should differ from default; other non-default codes: "
        f"{other_codes}"
    )


@pytest.mark.hardware
def test_snap_without_slot_pops_engage_first(board):
    """GP4 with no allocated slot pops ENGAGE-first guidance (not 'SNAPPED').
    """
    _setup_pitchgen(board)

    board.press(GP4)
    time.sleep(SETTLE)
    text = _popup_text(board)
    assert "ENGAGE first to SNAP" in text, (
        f"expected ENGAGE-first guidance, got:\n{text}"
    )
