"""Rig builders (tests/harness/rigs.py) — validate that a host-built rig leaves
the device in the configured state, and that a saved rig round-trips through SD.

These pin the throughput tooling itself: if a builder silently mis-configures a
track, by-ear sessions start from a wrong rig and the time is wasted. The build
path uses existing verbs (runs on any recent firmware); the save/reload path
needs CMD_PATTERN_SAVE (Stage-C batch flash)."""

import time

import pytest

from harness import Button, CC
from harness.rigs import build_tension, TENSION_MELODY

MODE_FLAGS = 0x41
FORCE_SCALE_BIT = 0x08
LEAD = 0  # group 0 → lead track 0


@pytest.mark.hardware
def test_tension_rig_configures_device(board):
    """build_tension leaves the lead gripped, FTS off, key pinned, melody seeded,
    GRAVITY at the detent."""
    res = build_tension(board, group=0, scale=0, root=0, grip=110, gravity=0, verbose=False)
    assert res.lead_track == LEAD

    # GRIP applied
    assert board.cc_get(LEAD, CC.TENSION_GRIP) == 110

    # melody seeded across the bar
    for step in (0, 3, 7, 11, 15):
        assert board.track_par_get(LEAD, 0, 0, step) == TENSION_MELODY[step], (
            f"step {step}: expected {TENSION_MELODY[step]}"
        )

    # FORCE_SCALE off (POC rule)
    assert (board.cc_get(LEAD, MODE_FLAGS) & FORCE_SCALE_BIT) == 0, "FTS should be off"

    # GRAVITY at the detent
    assert board.tension_get() == 0

    # key pinned to C major: with no chord held the SCALE-zone band is C major.
    zone, band = board.tension_band_get(-10, track=LEAD)
    assert (zone, band) == (3, 0xAB5), f"scale not pinned to C major: zone={zone} band=0x{band:03x}"


@pytest.mark.hardware
def test_tension_rig_save_reload_round_trips(board):
    """A saved rig restores its melody from SD (proving CMD_PATTERN_SAVE). Scratch
    slot B8 (pattern 15) — never A3, which the suite depends on. GRIP is not yet
    persisted (Stage D), so only the pattern-format state (notes) is asserted.

    Reads via a brief PLAY/STOP: pattern_load refreshes the source but not the
    stopped output mirror that track_par_get reads, so without a render the read
    is stale (see reference-sdcard-sessions-disposable)."""
    SCRATCH = (0, 0, 15)
    build_tension(board, group=0, grip=110, save=SCRATCH, verbose=False)

    # clobber the live RAM so a successful reload can only have come from SD
    for s in (0, 7, 15):
        board.track_par_set(LEAD, 0, 0, s, 1)

    # reload, then force an identity render (detent + GRIP off → no field) so the
    # output mirror reflects the loaded source
    board.tension_set(0)
    board.cc_set(LEAD, CC.TENSION_GRIP, 0)
    board.pattern_load(*SCRATCH)
    board.press(Button.PLAY)
    time.sleep(0.25)
    board.press(Button.STOP)
    time.sleep(0.1)

    for step in (0, 7, 15):
        assert board.track_par_get(LEAD, 0, 0, step) == TENSION_MELODY[step], (
            f"reload step {step}: expected {TENSION_MELODY[step]} from SD"
        )
