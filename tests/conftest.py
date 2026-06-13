"""Session-wide fixtures for the MIDIbox test harness."""

import time

import pytest

from harness import Board, BoardNotFound

# Name of the dedicated test session on the device's SD card. When set, the
# session fixture switches the device to this session for the test run and
# restores the user's previous session on teardown. Leave as None until the
# session has been built on the device (see doc/plans/2026-05-25-test-session-rig.md).
TEST_SESSION_NAME: str | None = "AUTOTEST"

# AUTOTEST fixture self-heal. A1-A3 (bank 0, patterns 0-2) are read-only Note-mode
# fixtures the suite leans on, but a hands-on session (engaging generators /
# drum-init + a pattern switch) + the auto-writeback save-model can clobber one to
# Drum event_mode — which fences the pitch processors / kills emission and breaks
# broad swaths of the suite with confusing "0 notes" failures (see the memory
# reference-autotest-a3-corruption). This guard probes each at suite start and
# rebuilds any that drifted off Note to its spec, so the rig is robust to manual
# perturbation. Maps pattern -> gate byte per step8: A1 empty, A2 4-on-the-floor
# (steps 0,4 of each 8 -> bits 0,4 = 0x11), A3 every step.
_AUTOTEST_FIXTURE_GATES = {0: 0x00, 1: 0x11, 2: 0xff}
_FIXTURE_EVENT_MODE_NOTE = 0
_FIXTURE_NOTE = 60  # C; emitted on every gated step (A1 has no gates -> silent)


def _ensure_autotest_fixtures(b: Board) -> None:
    """Probe AUTOTEST A1-A3 track 0; rebuild any clobbered off Note to spec.
    Best-effort — silently skips if the firmware predates the trg-write verb."""
    from harness.sysex import CC

    try:
        for pattern, gate in _AUTOTEST_FIXTURE_GATES.items():
            b.pattern_load(group=0, bank=0, pattern=pattern)
            time.sleep(0.15)
            if b.cc_get(0, CC.EVENT_MODE) == _FIXTURE_EVENT_MODE_NOTE:
                continue  # healthy
            # Clobbered to Drum (or other) — rebuild track 0 to its Note spec.
            b.cc_set(0, CC.EVENT_MODE, _FIXTURE_EVENT_MODE_NOTE)
            time.sleep(0.05)
            n_step8 = 0
            for step8 in range(32):  # walk until the track refuses (past its length)
                if not b.trg_byte_set(track=0, step8=step8, value=gate,
                                      trg_layer=0, instrument=0):
                    break
                n_step8 += 1
            for step in range(min(n_step8 * 8, 128)):
                b.track_par_set(0, 0, 0, step, _FIXTURE_NOTE)
            b.pattern_save(0, 0, pattern)
            time.sleep(0.2)
            print(f"[conftest] self-healed AUTOTEST fixture bank0/pat{pattern} -> Note mode")
    except (TimeoutError, RuntimeError):
        pass  # firmware predates the trg-write verb; leave fixtures as-is


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
            # On AUTOTEST now — self-heal any fixture a prior hands-on session
            # clobbered to Drum (else broad "0 notes" failures across the suite).
            _ensure_autotest_fixtures(b)

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
