"""Session-wide fixtures for the MIDIbox test harness."""

import pytest

from harness import Board, BoardNotFound


@pytest.fixture(scope="session")
def _board_session():
    """Open the SEQ V4 board once per session."""
    try:
        b = Board()
    except BoardNotFound as e:
        pytest.skip(f"SEQ V4 board not connected: {e}")
    yield b
    b.close()


@pytest.fixture
def board(_board_session):
    """Per-test board fixture: resets transport + page before each test."""
    try:
        _board_session.reset()
    except TimeoutError:
        # Firmware doesn't support RESET_STATE (older build); proceed anyway.
        pass
    yield _board_session
