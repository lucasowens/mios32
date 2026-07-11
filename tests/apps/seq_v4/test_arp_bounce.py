"""ARP tenant × bounce/capture — F1/F2 pins (OPEN_ITEMS 2026-07-10 review).

F1: every capture/bounce verb must NEUTRALIZE the ARP tenant on the frozen
copy (SEQ_CC_ResetGenerativeForBounce + SEQ_CORE_ProcessorBounce zero
arp_mode/arp_bus) — the capture bakes the arp's re-ordering into the notes, so
a kept arp_mode re-arps the frozen material when ArpSlotSync next fires.

F2: the slot-capture restore fans re-sync ALL processor-slot bridges
(SEQ_CORE_AllSlotSync) — before the fix they re-synced 4 tenants but not ARP,
so an arp-armed BYSTANDER track in the borrowed dst group came back with its
tcc intact but the slot disarmed: silently un-arped playback until the next
CC touch.

Sibling staleness (found with F1): the to-track paths (CaptureToTrack /
CaptureSpanPrepDst) inherit CCs 0x00..0x7f via SEQ_CC_Set — which arms dst's
slots from PRE-reset values — then reset tcc RAW. Without AllSlotSync after
the reset, a dst that previously ran an arp keeps a stale-armed ARP slot
(arp renders from slot->strength, unlike PITCH) and re-arps the frozen tape.

Fixture: AUTOTEST A3 (bank 0, pattern 2 — clean Note mode, every gate).
Destructive: overwrites bank 0 pattern 63 (the established scratch slot) and
loads patterns into group 1 (tracks 4..7).

Arp observability: Self-source mask with two pitch classes {D, A} and mode=Up
gives a step-dependent snap (step%2==0 → D, ==1 → A) that is unmistakably
different from the raw seeds:
    seeds {0:60, 1:64, 2:67}  →  arped mirror {0:62, 1:69, 2:62}
"""

import time

import pytest

from harness import Board, Button, CC, Page


NOTE_LAYER = 0
INSTR = 0
SWEEP_SETTLE = 0.12  # > SEQ_RENDER_SWEEP_MS (50ms)
SETTLE = 0.15
BOUNCE_MSG_MS = 1000  # "BOUNCED (proc)" popup hold

A3_PATTERN = 2  # AUTOTEST A3: Note mode, every gate

# Safe scratch slot (same one the other capture suites use).
SCRATCH_BANK = 0
SCRATCH_PATTERN = 63

ARP_UP = 1
# Static Self mask: pitch classes D (bit 2 → MASK_L) and A (bit 9 → MASK_H bit 1).
MASK_L_D = 0x04
MASK_H_A = 0x02

SEEDS = {0: 60, 1: 64, 2: 67}
ARPED = {0: 62, 1: 69, 2: 62}  # Up over {D,A}: nearest-D / nearest-A / nearest-D

GP8_BOUNCE = Button.GP(8)


def _seed(board: Board, track: int, seeds: dict[int, int]) -> None:
    for step, value in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, value)


def _quiet_render_retrigger(board: Board, track: int, seeds: dict[int, int]) -> None:
    """Wait out the sweep window, then re-write the seeds (dirty without a
    touched-refresh) so the next render is a full-buffer quiet pass."""
    time.sleep(SWEEP_SETTLE)
    _seed(board, track, seeds)
    time.sleep(0.05)


def _mirror(board: Board, track: int, steps) -> dict[int, int]:
    return {s: board.track_par_get(track, NOTE_LAYER, INSTR, s) for s in steps}


def _arm_arp(board: Board, track: int) -> None:
    board.cc_set(track, CC.CHORDMASK_MASK_L, MASK_L_D)
    board.cc_set(track, CC.CHORDMASK_MASK_H, MASK_H_A)
    board.cc_set(track, CC.ARP_BUS, 0)  # Self
    board.cc_set(track, CC.ARP_MODE, ARP_UP)


def _disarm_arp(board: Board, track: int) -> None:
    board.cc_set(track, CC.ARP_MODE, 0)
    board.cc_set(track, CC.CHORDMASK_MASK_L, 0)
    board.cc_set(track, CC.CHORDMASK_MASK_H, 0)


@pytest.mark.hardware
def test_capture_to_slot_track_neutralizes_dst_arp(board):
    """F1, slot path: capture an arp-armed track into a slot track → the frozen
    copy comes back from SD with ARP_MODE/ARP_BUS == 0 and the ARPED notes."""
    src = 0
    board.pattern_load(group=0, bank=0, pattern=A3_PATTERN)
    time.sleep(SETTLE)
    _seed(board, src, SEEDS)
    try:
        _arm_arp(board, src)
        _quiet_render_retrigger(board, src, SEEDS)
        assert _mirror(board, src, SEEDS) == ARPED, "precondition: arp not armed?"

        assert board.capture_to_slot_track(
            src_track=src, dst_track=4,
            dst_bank=SCRATCH_BANK, dst_pattern=SCRATCH_PATTERN,
        ), "CaptureToSlotTrack should commit"

        assert board.pattern_load(
            group=1, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN
        ), "loading the captured slot should succeed"
        time.sleep(SETTLE)

        assert board.cc_get(4, CC.ARP_MODE) == 0, (
            "frozen slot copy kept ARP_MODE — it will re-arp the baked notes "
            "when ArpSlotSync next fires (F1)"
        )
        assert board.cc_get(4, CC.ARP_BUS) == 0
        got = _mirror(board, 4, SEEDS)
        assert got == ARPED, (
            f"frozen copy should carry the HEARD (arped) notes as plain tape; "
            f"got {got}, want {ARPED}"
        )
    finally:
        _disarm_arp(board, src)


@pytest.mark.hardware
def test_bounce_group_slot_neutralizes_dst_arp_and_restores_src(board):
    """F1, whole-group path (CaptureToSlot): the bounced slot is arp-clean;
    the SOURCE track's live arp comes back byte-identical AND still audible
    (the sync-free reset + raw restore leaves slots consistent)."""
    src = 0
    board.pattern_load(group=0, bank=0, pattern=A3_PATTERN)
    time.sleep(SETTLE)
    _seed(board, src, SEEDS)
    try:
        _arm_arp(board, src)
        _quiet_render_retrigger(board, src, SEEDS)
        assert _mirror(board, src, SEEDS) == ARPED, "precondition: arp not armed?"

        assert board.bounce(
            src_track=src, dst_bank=SCRATCH_BANK, dst_pattern=SCRATCH_PATTERN
        ), "CaptureToSlot should commit"

        # Source restored: arp CC intact and still transforming the mirror.
        assert board.cc_get(src, CC.ARP_MODE) == ARP_UP, (
            "source track's live arp_mode should be restored after bounce"
        )
        _quiet_render_retrigger(board, src, SEEDS)
        assert _mirror(board, src, SEEDS) == ARPED, (
            "source track should still arp after the bounce restore"
        )

        # Bounced slot: arp-clean frozen tape.
        assert board.pattern_load(
            group=1, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN
        )
        time.sleep(SETTLE)
        assert board.cc_get(4, CC.ARP_MODE) == 0, (
            "bounced slot kept ARP_MODE — frozen material re-arps (F1)"
        )
        assert _mirror(board, 4, SEEDS) == ARPED, (
            "bounced slot should carry the heard (arped) notes as plain tape"
        )
    finally:
        _disarm_arp(board, src)


@pytest.mark.hardware
def test_processor_bounce_neutralizes_arp(board):
    """F1, in-place path (GP8 → SEQ_CORE_ProcessorBounce): arp is an enabled
    processor, so GP8 freezes it — ARP_MODE reads back 0, the mirror keeps the
    arped notes, and a forced re-render does NOT double-arp them."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=A3_PATTERN)
    time.sleep(SETTLE)
    _seed(board, track, SEEDS)
    _arm_arp(board, track)
    _quiet_render_retrigger(board, track, SEEDS)
    assert _mirror(board, track, SEEDS) == ARPED, "precondition: arp not armed?"

    board.page_set(Page.PITCHGEN)
    time.sleep(SETTLE)
    board.press(GP8_BOUNCE)
    time.sleep(0.05)
    popup = board.lcd_snapshot().text
    assert "BOUNCED (proc)" in popup, (
        f"GP8 with an armed ARP slot should freeze it in place; LCD reads:\n{popup}"
    )
    time.sleep((BOUNCE_MSG_MS / 1000.0) + SETTLE)

    assert board.cc_get(track, CC.ARP_MODE) == 0, (
        "in-place bounce kept ARP_MODE — next ArpSlotSync re-arms and re-arps "
        "the just-bounced material (F1)"
    )
    # Identity render: the baked (arped) notes must survive a re-render as-is.
    _quiet_render_retrigger(board, track, ARPED)
    assert _mirror(board, track, SEEDS) == ARPED, (
        "post-bounce render should be identity (no re-arp of the baked notes)"
    )


@pytest.mark.hardware
def test_capture_to_track_disarms_stale_dst_arp(board):
    """Sibling staleness: capture a PLAIN track onto a dst that previously ran
    an arp. The CC inherit arms dst's ARP slot from its pre-reset arp_mode;
    without AllSlotSync after the reset, the stale slot re-arps the frozen
    tape (mirror would read the snapped notes instead of the raw ones)."""
    src, dst = 0, 1
    board.pattern_load(group=0, bank=0, pattern=A3_PATTERN)
    time.sleep(SETTLE)
    _seed(board, src, SEEDS)
    try:
        _arm_arp(board, dst)  # dst is a live arp track before the capture
        time.sleep(0.05)

        assert board.capture_to_track(src, dst), "CaptureToTrack should commit"

        assert board.cc_get(dst, CC.ARP_MODE) == 0, (
            "dst ARP_MODE should be reset by the capture (F1)"
        )
        _quiet_render_retrigger(board, dst, SEEDS)
        got = _mirror(board, dst, SEEDS)
        assert got == SEEDS, (
            f"dst mirror should hold the RAW frozen notes; got {got} — a "
            f"stale-armed ARP slot is re-arping the tape (AllSlotSync missing)"
        )
    finally:
        _disarm_arp(board, dst)


@pytest.mark.hardware
@pytest.mark.parametrize("verb", ["capture_to_slot_track", "copy_track_live_to_slot"])
def test_slot_capture_restore_rearms_bystander_arp(board, verb):
    """F2: an arp-armed BYSTANDER track in the borrowed dst group must come
    back still arping. The staged slot load re-syncs its slots to the SLOT's
    CC state; the raw-memcpy restore must re-sync them back (AllSlotSync in
    the restore fan) — before the fix the tcc read back armed but the slot
    stayed disarmed: silently un-arped until the next CC touch."""
    src = 0
    bystander = 4  # group 1 track 0 — survives the borrow via the restore fan
    dst_track = 5  # capture target inside group 1 (not the bystander)

    board.pattern_load(group=0, bank=0, pattern=A3_PATTERN)
    time.sleep(SETTLE)
    _seed(board, src, SEEDS)

    board.pattern_load(group=1, bank=0, pattern=A3_PATTERN)
    time.sleep(SETTLE)
    _seed(board, bystander, SEEDS)
    try:
        _arm_arp(board, bystander)
        _quiet_render_retrigger(board, bystander, SEEDS)
        assert _mirror(board, bystander, SEEDS) == ARPED, (
            "precondition: bystander arp not armed?"
        )

        assert getattr(board, verb)(
            src_track=src, dst_track=dst_track,
            dst_bank=SCRATCH_BANK, dst_pattern=SCRATCH_PATTERN,
        ), f"{verb} should commit"
        time.sleep(SETTLE)

        assert board.cc_get(bystander, CC.ARP_MODE) == ARP_UP, (
            "bystander tcc should be restored byte-identical (pre-existing)"
        )
        # Re-render WITHOUT any CC touch (par writes only) — a disarmed slot
        # stays disarmed and the mirror falls back to the raw seeds.
        _quiet_render_retrigger(board, bystander, SEEDS)
        got = _mirror(board, bystander, SEEDS)
        assert got == ARPED, (
            f"bystander came back un-arped: mirror {got}, want {ARPED} — the "
            f"restore fan skipped SEQ_CORE_ArpSlotSync (F2)"
        )
    finally:
        _disarm_arp(board, bystander)
