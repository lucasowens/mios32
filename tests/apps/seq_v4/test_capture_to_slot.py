"""Capture → slot: SEQ_CORE_CaptureToSlot round-trip (the "not lost on
switching" guarantee).

The user's concern was that a bounced pattern might be lost when you switch to
it. It is not: CaptureToSlot's SEQ_FILE_B_PatternWrite f_close-flushes the slot
to the SD bank file before returning, and a slot's presence is derived from its
name bytes (which the write stamps "BNC…") — there is no separate index to
update. This pins the end-to-end proof: seed a track → bounce to a slot →
load that slot into a DIFFERENT group → the captured bytes come back from SD.

Loading into a fresh group (not the source's own) is what makes it airtight: the
verified bytes can only have come from the persisted slot, not leftover source
RAM (CaptureToSlot restores the source to its pre-bounce state).

Destructive: overwrites (BOUNCE_BANK, BOUNCE_PATTERN) on the SD card and loads
it into group 1 (tracks 4..7). Same safe scratch slot the previous bounce suite
used. Skip via -k if you don't want the SD perturbed.
"""

import time

import pytest

from harness import Board, Page
from harness.sysex import RESET_UNMUTE_ALL


SRC_TRACK = 0

# Trigger-layer assignment CCs (mirror seq_cc.h). The GATE assignment is the
# structural one a bounce must preserve — if it's cleared, the track has no gate
# layer and MBSEQ plays every step.
CC_ASG_GATE = 0x60

# Safe scratch slot: bank 0, pattern 63 (last slot of bank A in MBSEQ's default
# layout) — same slot the prior bounce suite used.
BOUNCE_BANK = 0
BOUNCE_PATTERN = 63

# Load the bounced slot into group 1 (tracks 4..7) to inspect it without
# touching the source group. Slot track 0 → group-1 track 0 = global track 4.
VERIFY_GROUP = 1
VERIFY_TRACK = 4

SETTLE = 0.15

# Widely-spaced steps with distinct values — a partial/empty load or a stride
# bug shows immediately.
SEED = {0: 60, 1: 62, 7: 67, 17: 72, 31: 48, 63: 55}


def _seed(board: Board, track: int, instr: int, seed: dict[int, int]) -> None:
    for step, note in seed.items():
        board.track_drum_par_set(track, instr, step, note)


@pytest.mark.hardware
def test_capture_to_slot_round_trip(board):
    """Bounce a seeded track to a slot, load that slot into a fresh group, and
    confirm the captured content came back from SD (not lost on switching)."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)
    _seed(board, SRC_TRACK, 0, SEED)

    assert board.bounce(
        src_track=SRC_TRACK, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN
    ), "CaptureToSlot should commit successfully"

    # Switch a DIFFERENT group to the bounced slot — the bytes can only come
    # from the persisted SD slot now.
    assert board.pattern_load(
        group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    ), "loading the bounced slot should succeed"
    time.sleep(SETTLE)

    for step, note in SEED.items():
        v = board.track_drum_par_get(VERIFY_TRACK, 0, step)
        assert v == note, (
            f"bounced slot, loaded into a fresh group, should carry captured "
            f"byte {note} at step {step}; got {v} — target lost on switching?"
        )
    # An unseeded step must be 0 (whole-pattern persisted, not a partial slice).
    assert board.track_drum_par_get(VERIFY_TRACK, 0, 8) == 0, (
        "unseeded step should be 0 in the loaded slot"
    )


@pytest.mark.hardware
def test_capture_to_slot_preserves_gate_assignment(board):
    """The bounced slot must keep the source's GATE trigger-layer assignment.

    Regression for the bug where SEQ_CC_ResetGenerativeForBounce cleared ALL
    trigger assignments — leaving the bounced pattern with no gate layer, which
    MBSEQ plays as 'every step on' (every step fired C-3, CLEAR had no effect).
    The assignment is structural, not generative, and must survive the bounce.
    """
    board.reset(RESET_UNMUTE_ALL)
    board.pattern_load(group=0, bank=0, pattern=0)
    time.sleep(SETTLE)

    src_gate = board.cc_get(SRC_TRACK, CC_ASG_GATE)
    assert src_gate != 0, (
        f"precondition: source track should have a gate layer assigned "
        f"(ASG_GATE != 0); got {src_gate}"
    )

    assert board.bounce(
        src_track=SRC_TRACK, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN
    )
    assert board.pattern_load(
        group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    )
    time.sleep(SETTLE)

    dst_gate = board.cc_get(VERIFY_TRACK, CC_ASG_GATE)
    assert dst_gate == src_gate, (
        f"bounced slot lost the gate assignment: source ASG_GATE={src_gate}, "
        f"loaded target ASG_GATE={dst_gate} (0 = no gate layer = every step "
        f"plays). The structural trigger assignments must be preserved."
    )


@pytest.mark.hardware
def test_capture_to_slot_preserves_source(board):
    """The source track is byte-identical after a capture-to-slot (the in-RAM
    snapshot/restore), so the bounce never disturbs the live performance."""
    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)
    _seed(board, SRC_TRACK, 0, SEED)

    assert board.bounce(
        src_track=SRC_TRACK, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN
    )

    for step, note in SEED.items():
        v = board.track_drum_par_get(SRC_TRACK, 0, step)
        assert v == note, (
            f"source track step {step} should still be {note} after "
            f"capture-to-slot; got {v}"
        )
