"""Session-wide fixtures for the MIDIbox test harness."""

import pytest

from harness import Board, BoardNotFound

# Name of the dedicated test session on the device's SD card. When set, the
# session fixture switches the device to this session for the test run and
# restores the user's previous session on teardown. Leave as None until the
# session has been built on the device (see doc/plans/2026-05-25-test-session-rig.md).
TEST_SESSION_NAME: str | None = "AUTOTEST"


@pytest.fixture(scope="session")
def _board_session():
    """Open the SEQ V4 board once per session; optionally switch to TEST_SESSION_NAME."""
    try:
        b = Board()
    except BoardNotFound as e:
        pytest.skip(f"SEQ V4 board not connected: {e}")

    prior_session: str | None = None
    if TEST_SESSION_NAME:
        try:
            prior_session = b.session_name_get()
        except (TimeoutError, RuntimeError):
            # Firmware predates SESSION_NAME_GET — disable the swap entirely so
            # we don't leave the device on the wrong session with no way back.
            prior_session = None
        else:
            if prior_session != TEST_SESSION_NAME:
                try:
                    b.session_load(TEST_SESSION_NAME)
                except (TimeoutError, RuntimeError) as e:
                    b.close()
                    pytest.skip(
                        f"could not switch to test session {TEST_SESSION_NAME!r}: {e}"
                    )

    try:
        yield b
    finally:
        if prior_session and prior_session != TEST_SESSION_NAME:
            try:
                b.session_load(prior_session)
            except Exception:
                pass  # best-effort restore; don't mask test failures
        b.close()


@pytest.fixture
def board(_board_session):
    """Per-test board fixture: resets transport/page/mutes AND reloads AUTOTEST
    pattern A1 so par/trg/CC layers start clean from disk every test.

    Without the pattern reload, tests that mutate track CCs (bounce tests set
    ECHO_REPEATS, STEPS_REPEAT, DIRECTION, etc.) contaminate later generator
    tests — RESET_STATE doesn't touch CC bytes. ~100ms of SD I/O per test is
    a cheap price for determinism.
    """
    try:
        _board_session.reset()
    except TimeoutError:
        # Firmware doesn't support RESET_STATE (older build); proceed anyway.
        pass
    if TEST_SESSION_NAME:
        try:
            _board_session.pattern_load(group=0, bank=0, pattern=0)
        except (TimeoutError, RuntimeError):
            # Firmware predates PATTERN_LOAD, or A1 isn't on the active session;
            # let the test run with whatever's in RAM.
            pass
    yield _board_session
