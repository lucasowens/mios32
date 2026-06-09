"""Freeze faithfulness — FORCE_SCALE is BAKED into the captured notes, not
preserved as a flag (the §9 "bake heard pitches" decision).

Force-to-scale is applied at EMISSION (the tick, per played note, after
transpose) and is NEVER baked into the captured OutputActive — the captured Note
layer holds the RAW, off-scale note. Two ways to keep a frozen force-scale track
faithful:
  - preserve the FORCE_SCALE flag and re-snap LIVE → a later global-key change
    would silently re-pitch the frozen copy (rejected);
  - bake the heard pitch into the Note layer at capture time and leave FORCE_SCALE
    reset → the copy is immune to later key changes (chosen).

These pins prove the chosen behavior on the real gesture verb AND the in-place
verb: a seeded off-scale note comes back SNAPPED, an in-scale note is unchanged,
and the FORCE_SCALE flag is RESET on the frozen copy (the snap lives in the notes
now, not in a re-applied flag).

Key = C Major, pinned via board.global_scale_set (board.reset does not touch the
global scale). Major snap (root C): C#->D, D#->E, F#->G, A#->B; C/D/E/G unchanged.

Destructive: overwrites (BOUNCE_BANK, BOUNCE_PATTERN) on the SD card. Same scratch
slot the rest of the capture suite uses.
"""

import time

import pytest

from harness import Board, CC, Page
from harness.sysex import RESET_UNMUTE_ALL


SRC_TRACK = 0
NOTE_LAYER = 0

# Same scratch slot + verify-group convention as test_capture_to_slot.py: load the
# frozen slot into group 1 (tracks 4..7) so the bytes can only have come from SD,
# never leftover source RAM. Slot track 0 -> group-1 track 0 = global track 4.
BOUNCE_BANK = 0
BOUNCE_PATTERN = 63
VERIFY_GROUP = 1
VERIFY_TRACK = 4

# MODE_FLAGS bit 3 (seq_core.h seq_core_trkmode_flags_t): note values forced to
# scale at emission.
FORCE_SCALE_BIT = 0x08

MAJOR_SCALE = 0  # seq_scale_table index 0 == "Major"
ROOT_C = 0       # root_selection 0 + keyb_root 0 == C

# Note-limit CCs (seq_cc.h). Emission clamps to [lower, upper] AFTER the snap, so
# the bake must reproduce it; ResetGenerativeForBounce then clears these.
CC_LIMIT_LOWER = 0x43
CC_LIMIT_UPPER = 0x44

SETTLE = 0.15

# step -> seeded raw note, and the C-Major snap each must land on after freeze.
# Off-scale notes move; in-scale notes (and the unseeded A3 default 60) stay.
#   61 C#5 -> 62 D    63 D#5 -> 64 E    66 F#5 -> 67 G    70 A#5 -> 71 B
#   60 C5  -> 60      64 E5  -> 64      67 G5  -> 67
SEED_TO_SNAPPED = {0: (61, 62), 1: (63, 64), 4: (66, 67), 8: (70, 71),
                   2: (60, 60), 5: (64, 64), 9: (67, 67)}


def _setup_force_scale_source(board: Board) -> None:
    """A3 note track 0 in C Major with FORCE_SCALE armed and the seed written."""
    board.reset(RESET_UNMUTE_ALL)
    board.pattern_load(group=0, bank=0, pattern=2)  # A3: note-mode track 0, layer 0 = Note
    board.global_scale_set(MAJOR_SCALE, ROOT_C, ROOT_C)
    time.sleep(SETTLE)

    flags = board.cc_get(SRC_TRACK, CC.MODE_FLAGS)
    board.cc_set(SRC_TRACK, CC.MODE_FLAGS, flags | FORCE_SCALE_BIT)
    assert board.cc_get(SRC_TRACK, CC.MODE_FLAGS) & FORCE_SCALE_BIT, (
        "precondition: FORCE_SCALE should be armed on the source"
    )

    for step, (raw, _snapped) in SEED_TO_SNAPPED.items():
        board.track_par_set(SRC_TRACK, NOTE_LAYER, 0, step, raw)
    time.sleep(SETTLE)

    # The captured OutputActive holds the RAW note — force-scale is emission-time,
    # not in the mirror. This is exactly why baking is needed: a no-op freeze would
    # carry the off-scale 61 through.
    assert board.track_par_get(SRC_TRACK, NOTE_LAYER, 0, 0) == 61, (
        "source output mirror should be RAW (off-scale) — the snap is emission-time"
    )


def _assert_frozen_is_baked(board: Board) -> None:
    """Read the frozen copy (loaded into a fresh group) and assert the heard
    pitches were baked in and FORCE_SCALE was reset."""
    assert board.cc_get(VERIFY_TRACK, CC.MODE_FLAGS) & FORCE_SCALE_BIT == 0, (
        "FORCE_SCALE must be RESET on the frozen copy — the snap is baked into the "
        "notes, not re-applied live (else a later key change would re-pitch it)"
    )
    for step, (raw, snapped) in SEED_TO_SNAPPED.items():
        v = board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, step)
        assert v == snapped, (
            f"frozen note step {step}: expected C-Major-snapped {snapped} "
            f"(raw seed {raw}), got {v} — force-scale bake wrong or absent"
        )
    # Unseeded step keeps the A3 default (60 = C, in-scale -> unchanged): whole-
    # buffer freeze, and the bake leaves in-scale notes alone.
    assert board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, 3) == 60, (
        "unseeded step should carry A3's default note 60 (C, in C-major) unchanged"
    )


@pytest.mark.hardware
def test_capture_to_slot_track_bakes_force_scale(board):
    """The PATTERN-hold gesture verb (SEQ_CORE_CaptureToSlotTrack) bakes the
    force-to-scale snap into the frozen slot."""
    _setup_force_scale_source(board)

    assert board.capture_to_slot_track(
        src_track=SRC_TRACK,
        dst_track=SRC_TRACK,
        dst_bank=BOUNCE_BANK,
        dst_pattern=BOUNCE_PATTERN,
    ), "CaptureToSlotTrack should commit"

    assert board.pattern_load(
        group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    )
    time.sleep(SETTLE)
    _assert_frozen_is_baked(board)


@pytest.mark.hardware
def test_bounce_bakes_force_scale(board):
    """The in-place capture-to-slot verb (SEQ_CORE_CaptureToSlot, board.bounce)
    bakes the force-to-scale snap too — same axis-split path, different verb."""
    _setup_force_scale_source(board)

    assert board.bounce(
        src_track=SRC_TRACK, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN
    ), "CaptureToSlot should commit"

    assert board.pattern_load(
        group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    )
    time.sleep(SETTLE)
    _assert_frozen_is_baked(board)


@pytest.mark.hardware
def test_capture_bakes_force_scale_then_limit(board):
    """The heard pitch is noteLimit(forceScale(note)) — emission applies the note
    limit AFTER the snap. The bake must reproduce both, since
    ResetGenerativeForBounce clears the limit CCs.

    Note: SEQ_CORE_Limit's note limit is NOT a min/max clamp — SEQ_CORE_TrimNote
    OCTAVE-FOLDS an out-of-range note back toward the window, preserving pitch
    class. Window [60, 71] (C5..B5):
      72 C6  -> snap 72 C6 (in scale) -> fold down an octave -> 60 C5
      73 C#6 -> snap 74 D6           -> fold down an octave -> 62 D5
      65 F5  -> snap 65 F5 (in scale) -> in window, unchanged
    """
    LIMIT_LO, LIMIT_HI = 60, 71
    seed_to_baked = {0: (72, 60), 1: (73, 62), 4: (65, 65)}

    _setup_force_scale_source(board)
    board.cc_set(SRC_TRACK, CC_LIMIT_LOWER, LIMIT_LO)
    board.cc_set(SRC_TRACK, CC_LIMIT_UPPER, LIMIT_HI)
    for step, (raw, _baked) in seed_to_baked.items():
        board.track_par_set(SRC_TRACK, NOTE_LAYER, 0, step, raw)
    time.sleep(SETTLE)

    assert board.capture_to_slot_track(
        src_track=SRC_TRACK, dst_track=SRC_TRACK,
        dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN,
    ), "CaptureToSlotTrack should commit"
    assert board.pattern_load(
        group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    )
    time.sleep(SETTLE)

    for step, (raw, baked) in seed_to_baked.items():
        v = board.track_par_get(VERIFY_TRACK, NOTE_LAYER, 0, step)
        assert v == baked, (
            f"step {step}: raw {raw} should bake to snap-then-limit {baked}, got {v}"
        )
