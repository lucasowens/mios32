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

from harness import Board, CC, Page
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
def test_capture_to_slot_preserves_groove(board):
    """The bounced slot must keep the source's GROOVE (style + value).

    Groove is deterministic *shaping* applied at emission (swing/velocity/length),
    never baked into the captured par/trg. SEQ_CC_ResetGenerativeForBounce used to
    zero it on the destination alongside the genuinely-generative CCs, so a grooved
    drum track froze to a dead-straight copy — it sounded "a lot different, wrong"
    the moment you reloaded the slot. Groove is on the *shaping* axis, not the
    *generation* axis: re-applying the preserved CC re-grooves the copy identically,
    so it must survive the capture. Mirrors test_capture_to_slot_preserves_gate_
    assignment (a structural CC that must round-trip).
    """
    GROOVE_STYLE = 3  # Shuffle2 (seq_groove_presets index)
    GROOVE_VALUE = 20

    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)
    _seed(board, SRC_TRACK, 0, SEED)

    board.cc_set(SRC_TRACK, CC.GROOVE_STYLE, GROOVE_STYLE)
    board.cc_set(SRC_TRACK, CC.GROOVE_VALUE, GROOVE_VALUE)
    # Read back what the device actually stored — assert on these, robust to the
    # groove_style bitfield encoding.
    src_style = board.cc_get(SRC_TRACK, CC.GROOVE_STYLE)
    src_value = board.cc_get(SRC_TRACK, CC.GROOVE_VALUE)
    assert src_style != 0, (
        f"precondition: source should carry a groove style; got {src_style}"
    )

    assert board.bounce(
        src_track=SRC_TRACK, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN
    )
    assert board.pattern_load(
        group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    )
    time.sleep(SETTLE)

    dst_style = board.cc_get(VERIFY_TRACK, CC.GROOVE_STYLE)
    dst_value = board.cc_get(VERIFY_TRACK, CC.GROOVE_VALUE)
    assert dst_style == src_style and dst_value == src_value, (
        f"bounced slot lost its groove: source style/value={src_style}/{src_value}, "
        f"loaded target style/value={dst_style}/{dst_value} (0/0 = stripped to "
        f"dead-straight). Groove is deterministic shaping and must be preserved."
    )


@pytest.mark.hardware
def test_capture_to_slot_track_inherits_full_config(board):
    """The PATTERN-hold gesture verb (SEQ_CORE_CaptureToSlotTrack) must carry the
    SOURCE's full track config onto the frozen copy — length AND groove — not just
    the note data + drum layout.

    Regression for the real-hardware report: freezing a grooved beat via the
    gesture produced a copy that ran "almost too fast" with the groove off, because
    CaptureToSlotTrack inherited only the lower-48 CCs (lay_const) from the source.
    Length (0x4d), clock divider (0x4c), groove (0x52/0x53) and the trigger-layer
    assignments (0x60+) all live ABOVE that range, so the frozen track kept the
    DESTINATION slot's defaults. The fix copies the source's full 0x00..0x7f CC
    space (mirroring PASTE_CLR_ALL in seq_ui_util.c).

    Distinct from test_capture_to_slot_preserves_groove, which drives the in-place
    CaptureToSlot (board.bounce) — a different verb that already preserved config by
    snapshotting the whole source tcc. THIS test pins the actual UI gesture verb,
    which the bounce test does not exercise.
    """
    GROOVE_STYLE = 3   # Shuffle2 (seq_groove_presets index)
    GROOVE_VALUE = 20
    NEW_LENGTH = 7     # distinct from the drum-init default — the "too fast" pin

    board.reset(RESET_UNMUTE_ALL)
    board.track_drum_init(SRC_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)

    default_length = board.cc_get(SRC_TRACK, CC.LENGTH)
    assert default_length != NEW_LENGTH, (
        f"precondition: NEW_LENGTH must differ from the drum-init default "
        f"({default_length}) for length to be a valid pin"
    )
    board.cc_set(SRC_TRACK, CC.LENGTH, NEW_LENGTH)
    board.cc_set(SRC_TRACK, CC.GROOVE_STYLE, GROOVE_STYLE)
    board.cc_set(SRC_TRACK, CC.GROOVE_VALUE, GROOVE_VALUE)
    src_style = board.cc_get(SRC_TRACK, CC.GROOVE_STYLE)
    src_value = board.cc_get(SRC_TRACK, CC.GROOVE_VALUE)
    _seed(board, SRC_TRACK, 0, {0: 60, 1: 64, 5: 67})

    # The actual UI gesture verb: capture src into dst_track of a stored slot,
    # persisted to SD (dst_track == src_track == the gesture's default).
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

    # Groove must travel with the frozen copy (the "groove off" symptom).
    assert (
        board.cc_get(VERIFY_TRACK, CC.GROOVE_STYLE) == src_style
        and board.cc_get(VERIFY_TRACK, CC.GROOVE_VALUE) == src_value
    ), (
        "gesture-frozen copy lost the source groove — CaptureToSlotTrack must "
        "inherit groove CCs (0x52/0x53) from the source, not keep the dst slot's"
    )
    # Length must travel too (the "almost too fast" symptom).
    got_length = board.cc_get(VERIFY_TRACK, CC.LENGTH)
    assert got_length == NEW_LENGTH, (
        f"gesture-frozen copy has the wrong length: expected source length "
        f"{NEW_LENGTH}, got {got_length} (= the dst slot's default — the 'too "
        f"fast' bug; CaptureToSlotTrack must inherit LENGTH 0x4d from the source)"
    )
    # And the captured note data still round-trips.
    assert board.track_drum_par_get(VERIFY_TRACK, 0, 0) == 60


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
