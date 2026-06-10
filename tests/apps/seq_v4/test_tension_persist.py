"""Tension Workbench Stage D — GRIP persistence + freeze-faithfulness.

Two pins:

1. ext-CC round-trip — the widened V3 ext-CC range carries GRIP (0x9a) and the
   chord-mask CCs (0x96-0x99) through save/reload. Run on a FRESHLY-created bank:
   banks created by older firmware reserved V2-sized slots that can't hold the
   wider block, so write_ext skips them. CC reads use cc_get (tcc directly), so
   no render/mirror dance is needed.

2. freeze-faithfulness — the INVERSE of test_capture_force_scale. FORCE_SCALE is
   emission-time, so the output mirror holds the RAW note and capture must BAKE.
   TENSION is a render-stack processor, so the push is ALREADY in the mirror — the
   existing bounce verb captures it faithfully with ZERO new bake code. We bounce a
   fully-pulled groove (DRONE → everything to the root) and confirm the frozen copy
   reads as the root, baked into the notes (read with the field off so the read
   reflects the captured source, not a live re-snap).
"""

import time

import pytest

from harness import CC
from harness.sysex import RESET_UNMUTE_ALL

SRC = 0
NOTE_LAYER = 0
SETTLE = 0.15


# --------------------------------------------------------------------------- 1

@pytest.mark.hardware
def test_grip_and_chordmask_ccs_persist(board):
    """GRIP (0x9a) + chord-mask CCs (0x96-0x99) survive save/reload via the widened
    V3 ext-CC range. Existing bank slots have room for the wider block (the layer
    data dominates pattern_size), so a scratch slot works — no fresh bank needed.
    CC reads use cc_get (tcc directly), so no render/mirror dance."""
    GROUP, BANK, PATTERN = 0, 0, 61  # scratch slot; never a baseline (0/1/2)
    board.cc_set(SRC, CC.TENSION_GRIP, 99)
    board.cc_set(SRC, CC.CHORDMASK_STRENGTH, 88)
    board.cc_set(SRC, CC.CHORDMASK_BUS, 2)
    board.cc_set(SRC, CC.CHORDMASK_DRUM_L, 0x55)
    assert board.pattern_save(GROUP, BANK, PATTERN), "save should commit"

    # clobber every widened-range CC, then reload from SD
    for cc in (CC.TENSION_GRIP, CC.CHORDMASK_STRENGTH, CC.CHORDMASK_BUS, CC.CHORDMASK_DRUM_L):
        board.cc_set(SRC, cc, 0)
    assert board.pattern_load(GROUP, BANK, PATTERN)

    assert board.cc_get(SRC, CC.TENSION_GRIP) == 99, "GRIP (0x9a) must persist"
    assert board.cc_get(SRC, CC.CHORDMASK_STRENGTH) == 88, "chordmask strength (0x96) must persist"
    assert board.cc_get(SRC, CC.CHORDMASK_BUS) == 2, "chordmask bus (0x97) must persist"
    assert board.cc_get(SRC, CC.CHORDMASK_DRUM_L) == 0x55, "chordmask drum-l (0x98) must persist"


# --------------------------------------------------------------------------- 2

# DRONE band is the root C; every seeded non-C note snaps to a C when fully pulled.
SEED = {0: 62, 1: 64, 2: 65, 3: 67, 4: 69, 5: 71}  # D E F G A B — none is a C
VERIFY_GROUP = 1
VERIFY_TRACK = 4  # group-1 track 0
BOUNCE_BANK = 0
BOUNCE_PATTERN = 62  # scratch slot (notes persist in any slot; ext CCs not needed here)


@pytest.mark.hardware
def test_tension_freeze_is_faithful_with_no_bake(board):
    """A pushed groove, bounced, comes back as the pushed pitches — captured by the
    existing bounce verb with no TENSION-specific bake code (the §2.3 claim)."""
    board.reset(RESET_UNMUTE_ALL)
    board.pattern_load(group=0, bank=0, pattern=2)  # A3: note track 0
    board.global_scale_set(0, 0, 0)                  # C major, root C
    board.set_force_scale(SRC, False)                # POC rule: no emission snap
    for step, note in SEED.items():
        board.track_par_set(SRC, NOTE_LAYER, 0, step, note)

    # full pull + full grip → DRONE band {C}, every step gripped → snaps to a C
    board.tension_set(-64)
    board.cc_set(SRC, CC.TENSION_GRIP, 127)
    time.sleep(SETTLE)

    # bounce the pushed groove. capture forces a render (TENSION applies) and saves
    # the snapped OutputActive — no FORCE_SCALE-style bake step exists for tension.
    assert board.capture_to_slot_track(
        src_track=SRC, dst_track=SRC, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN
    ), "capture should commit"

    # read the frozen copy with the field OFF, so the read reflects the captured
    # source bytes (a baked push), not a live re-snap
    board.tension_set(0)
    assert board.pattern_load(group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN)
    time.sleep(SETTLE)

    for step in SEED:
        v = board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, step)
        assert v % 12 == 0, (
            f"frozen step {step}: pushed groove should bake to a C (PC 0), got {v}"
        )
        assert v != SEED[step], (
            f"frozen step {step}: still the raw seed {SEED[step]} — the push was NOT captured"
        )

    board.cc_set(SRC, CC.TENSION_GRIP, 0)
