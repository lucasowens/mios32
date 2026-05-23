"""End-to-end smoke tests: prove the harness can reach the firmware."""

import time

import pytest

from harness import Button


@pytest.mark.hardware
def test_ping(board):
    """SysEx PING → PONG round trip with the firmware build id."""
    payload = board.ping(timeout=2.0)
    assert payload == b"SEQv4", f"unexpected build id: {payload!r}"


@pytest.mark.hardware
def test_lcd_snapshot_dimensions(board):
    """LCD snapshot returns a 2x80 buffer."""
    snap = board.lcd_snapshot()
    assert snap.lines == 2
    assert snap.columns == 80
    assert len(snap.raw) == 160


@pytest.mark.hardware
def test_lcd_snapshot_is_printable(board):
    """LCD content is mostly printable ASCII so assertions can match on `.text`."""
    snap = board.lcd_snapshot()
    # Allow some non-printable bytes (custom glyphs / cursor markers) but the
    # majority should be printable ASCII for any normal page.
    printable = sum(1 for b in snap.raw if 0x20 <= b < 0x7F)
    assert printable >= len(snap.raw) * 0.5, (
        f"LCD looks mostly non-printable ({printable}/{len(snap.raw)} printable): "
        f"line0={snap.line(0)!r}"
    )


@pytest.mark.hardware
def test_button_play_dispatches(board):
    """PLAY button is configured in midiphy_rh and BUTTON dispatches cleanly."""
    pin = board.button(Button.PLAY, depressed=False)
    assert pin != 0 and pin != 0xFFFF
    # Release immediately so we don't leave transport in an odd state.
    board.button(Button.PLAY, depressed=True)


@pytest.mark.hardware
def test_menu_held_changes_lcd(board):
    """While MENU is held, the LCD shows the menu navigation overlay.

    BUTTON_BEH_MENU is 0 (set-mode) in the midiphy hwcfg, so MENU_PRESSED is
    only true while the physical button is depressed. We snapshot mid-hold,
    then release and confirm the screen returns to its prior state.
    """
    before = board.lcd_snapshot().text

    board.button(Button.MENU, depressed=False)
    time.sleep(0.1)  # let SEQ_TASK_UI run a render pass
    try:
        held = board.lcd_snapshot().text
    finally:
        board.button(Button.MENU, depressed=True)

    time.sleep(0.1)
    after = board.lcd_snapshot().text

    assert held != before, (
        "LCD didn't change while MENU was held — handler may not have fired.\n"
        f"before:\n{before}\nheld:\n{held}"
    )
    assert after == before, (
        "LCD didn't return to the prior state after releasing MENU.\n"
        f"before:\n{before}\nafter:\n{after}"
    )
