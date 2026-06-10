"""Tension Workbench Stage B — GRAVITY cockpit page control routing.

Smoke-pins that the cockpit page's encoders/buttons actually reach the field
engine on hardware: GP1 → global GRAVITY, GP3 → per-track GRIP, GP8 → RESOLVE
(instant snap to the detent in Stage B; Stage C makes it a bar-quantized ramp).
LCD layout and by-ear feel are validated by hand — the engine math itself is
pinned by test_tension.py.
"""

import pytest

from harness import Button, CC, Encoder, Page


@pytest.mark.hardware
def test_gravity_page_gp1_sets_gravity(board):
    """GP1 encoder drives the global GRAVITY dial (signed, clamped)."""
    board.page_set(Page.GRAVITY)
    board.tension_set(0)
    board.turn(Encoder.GP(1), 5)   # turn() chunks into ±3 (firmware per-call clamp)
    assert board.tension_get() == 5, "GP1 +5 should set GRAVITY = +5"
    board.turn(Encoder.GP(1), -9)
    assert board.tension_get() == -4, "GP1 -9 should set GRAVITY = -4"
    board.tension_set(0)


@pytest.mark.hardware
def test_gravity_page_gp3_sets_grip(board):
    """GP3 encoder drives per-track GRIP (SEQ_CC_TENSION_GRIP on visible track)."""
    board.page_set(Page.GRAVITY)
    board.ui_track_set(0)
    board.cc_set(0, CC.TENSION_GRIP, 0)
    board.turn(Encoder.GP(3), 20)
    assert board.cc_get(0, CC.TENSION_GRIP) == 20, "GP3 +20 should set GRIP = 20"
    board.cc_set(0, CC.TENSION_GRIP, 0)


@pytest.mark.hardware
def test_gravity_page_gp8_resolves_to_detent(board):
    """GP8 button (RESOLVE) snaps GRAVITY back to the detent (Stage B: instant)."""
    board.page_set(Page.GRAVITY)
    board.tension_set(-40)
    assert board.tension_get() == -40
    board.press(Button.GP(8))
    assert board.tension_get() == 0, "RESOLVE should land GRAVITY on the detent (0)"
