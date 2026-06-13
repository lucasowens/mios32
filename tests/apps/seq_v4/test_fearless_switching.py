"""FEARLESS SWITCHING Stage A — the save-model inversion's writeback half.

Design §9 (2026-06-11): the working state always persists — a group whose
live RAM diverged from its working slot (dirty) is written back to that slot
before any pattern load replaces it. Protection becomes the explicit
CHECKPOINT/REVERT act (Stage C); this file pins the dirty bookkeeping and the
writeback itself.

The dirty model: SEQ_PAR_Set / SEQ_TRG_Set* / SEQ_CC_Set chokepoints set the
group's bit; it clears when slot == live again (any group load, a save INTO
the working slot, the writeback). CMD_PATTERN_CHANGE drives the real switch
path (SEQ_PATTERN_Change); CMD_PATTERN_LOAD stays a raw load that bypasses
the writeback on purpose.

Suite-safety invariant pinned here implicitly: every test parks group 0's
working slot on a scratch slot (raw load) before dirtying anything, and
CMD_RESET_STATE clears the dirty mask — together these keep auto-writeback
from ever rewriting the AUTOTEST A1-A3 baselines.

Destructive: overwrites (0, 61) and (0, 62) on the SD card. Same scratch-slot
conventions as test_track_load / test_capture_to_slot.
"""

import pytest

from harness import Board, Button, CC
from harness.sysex import RESET_DEFAULT


SCRATCH_BANK = 0
SCRATCH_A = 61  # group 0's parked working slot in these tests
SCRATCH_B = 62  # the "other" slot switched to; never a baseline (0/1/2)

LEN_SLOT_A = 7   # LENGTH CC baked into slot A
LEN_SLOT_B = 10  # LENGTH CC baked into slot B (must differ from A)
LEN_EDIT = 12    # the "jam edit" (must differ from both)


def _park(board: Board) -> None:
    """Give both scratch slots known content, then park group 0's working
    slot on SCRATCH_A with a clean dirty bit.

    Build order matters: the slots are saved from live RAM, then a RAW load
    (no writeback) re-points seq_pattern[0] at SCRATCH_A — which also clears
    group 0's dirty bit by construction (slot == live after a load)."""
    board.cc_set(0, CC.LENGTH, LEN_SLOT_B)
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_B), "slot B build should commit"
    board.cc_set(0, CC.LENGTH, LEN_SLOT_A)
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A), "slot A build should commit"
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A), "park load should commit"
    mask, _ = board.dirty_query()
    assert not (mask & 0x01), f"park should leave group 0 clean, mask={mask:#04x}"


@pytest.mark.hardware
def test_edit_sets_dirty_and_raw_load_clears_without_writing(board):
    """A single CC edit through the chokepoint flags the group; a raw load
    makes slot == live again and swallows the flag WITHOUT bumping the
    writeback counter (pattern_load deliberately bypasses the inversion)."""
    board.reset(RESET_DEFAULT)
    _park(board)

    _, count0 = board.dirty_query()
    board.cc_set(0, CC.LENGTH, LEN_EDIT)
    mask, _ = board.dirty_query()
    assert mask & 0x01, "CC edit should dirty group 0"

    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A)
    mask, count1 = board.dirty_query()
    assert not (mask & 0x01), "raw load should clear the dirty bit"
    assert count1 == count0, "raw load must never auto-writeback"


@pytest.mark.hardware
def test_switch_writes_back_and_edit_survives_round_trip(board):
    """THE Stage A pin — jam, switch away, come back: nothing lost. The edit
    made on slot A's live state is auto-committed to slot A when the group
    switches to slot B, and is still there when the group returns."""
    board.reset(RESET_DEFAULT)
    _park(board)

    board.cc_set(0, CC.LENGTH, LEN_EDIT)  # the jam edit
    _, count0 = board.dirty_query()

    # Switch away: the real path. Writeback (slot A <- live) then load B.
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B), "switch to B should commit"
    mask, count1 = board.dirty_query()
    assert count1 == count0 + 1, "dirty switch should fire exactly one writeback"
    assert not (mask & 0x01), "writeback + load should leave group 0 clean"
    assert board.cc_get(0, CC.LENGTH) == LEN_SLOT_B, "live state should now be slot B"

    # Come back: clean switch — no writeback, and the edit persisted.
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_A), "return to A should commit"
    _, count2 = board.dirty_query()
    assert count2 == count1, "clean switch must not write"
    assert board.cc_get(0, CC.LENGTH) == LEN_EDIT, (
        "the jam edit should have survived the round trip (nothing lost)"
    )


@pytest.mark.hardware
def test_clean_switch_skips_writeback(board):
    """No divergence, no SD write: the writeback counter stays flat across a
    switch on a clean group."""
    board.reset(RESET_DEFAULT)
    _park(board)

    _, count0 = board.dirty_query()
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B)
    mask, count1 = board.dirty_query()
    assert count1 == count0, "clean switch must not write"
    assert not (mask & 0x01)


@pytest.mark.hardware
def test_aimed_save_clears_dirty_only_for_working_slot(board):
    """An aimed save INTO the working slot makes slot == live (clears the
    bit); an aimed save to a DIFFERENT slot leaves the divergence — and the
    flag — intact."""
    board.reset(RESET_DEFAULT)
    _park(board)

    board.cc_set(0, CC.LENGTH, LEN_EDIT)
    mask, _ = board.dirty_query()
    assert mask & 0x01

    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_B)  # aimed elsewhere
    mask, _ = board.dirty_query()
    assert mask & 0x01, "saving to a non-working slot must keep the dirty bit"

    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_A)  # the working slot
    mask, _ = board.dirty_query()
    assert not (mask & 0x01), "saving the working slot is the writeback by hand"


@pytest.mark.hardware
def test_clear_sets_dirty(board):
    """CLEAR (the by-ear bug class, 2026-06-12): edits that write through
    direct memset / preset-copy helpers instead of the Set chokepoints — the
    pattern-page paste was heard, then silently discarded on switch because
    no writeback fired. CLEAR_Track exercises the same bypass family from the
    panel, so it's the cheap HIL pin for the whole sweep (preset import /
    multi-clear / UTIL undo got the same explicit dirty-set)."""
    board.reset(RESET_DEFAULT)
    _park(board)

    board.press(Button.CLEAR)
    mask, _ = board.dirty_query()
    assert mask & 0x01, "CLEAR must dirty the group (memset bypasses SEQ_TRG_Set)"


@pytest.mark.hardware
def test_pull_dirties_destination_group(board):
    """The RECOMBINE pull transfuses a stored track into live RAM — the
    destination group's live state now diverges from its working slot, so the
    pull must flag it (via the CC-replay chokepoint inside TrackRead)."""
    board.reset(RESET_DEFAULT)
    _park(board)

    mask0, _ = board.dirty_query()
    assert not (mask0 & 0x02), "group 1 should start clean"

    assert board.track_load(
        dst_track=6, bank=SCRATCH_BANK, pattern=SCRATCH_A, slot_track=1
    ), "pull should commit"
    mask1, _ = board.dirty_query()
    assert mask1 & 0x02, "pull should dirty the destination group"

    # Track undo (restore the pre-pull victim) is a live write too — the bit
    # must stay set, not get optimized away.
    assert board.track_undo() == 6
    mask2, _ = board.dirty_query()
    assert mask2 & 0x02, "undo restore keeps the group dirty (live still moved)"

    # Politeness: don't leave group 1 armed for a writeback into its own
    # working slot (B-bank) during teardown of THIS test; the next test's
    # reset would clear it anyway.
    board.dirty_set(1, False)


@pytest.mark.hardware
def test_push_trample_restore_preserves_clean_flag(board):
    """CaptureToSlotTrack (the push) borrows the dst group's live RAM and
    restores it byte-for-byte — a clean group must come out clean, not
    flagged by the trample's internal CC replay."""
    board.reset(RESET_DEFAULT)
    _park(board)

    mask0, _ = board.dirty_query()
    assert not (mask0 & 0x01)

    # Push track 0 into slot B section 2 (dst_track=2 keeps the trample in
    # group 0, the group we're watching).
    assert board.capture_to_slot_track(0, 2, SCRATCH_BANK, SCRATCH_B), (
        "push should commit"
    )
    mask1, _ = board.dirty_query()
    assert not (mask1 & 0x01), "trample+restore must not leave a false dirty bit"


@pytest.mark.hardware
def test_forced_dirty_fires_writeback_without_real_edit(board):
    """The test knob proves the writeback is gated on the flag alone — and
    gives Stage B/C a cheap way to force commit traffic."""
    board.reset(RESET_DEFAULT)
    _park(board)

    board.dirty_set(0, True)
    _, count0 = board.dirty_query()
    assert board.pattern_change(0, SCRATCH_BANK, SCRATCH_B)
    _, count1 = board.dirty_query()
    assert count1 == count0 + 1, "forced flag should fire the writeback"


@pytest.mark.hardware
def test_edit_survives_session_hop(board):
    """The session-switch path mirrors the menu flow: dirty groups are
    written back into the OUTGOING session before the name changes. The jam
    edit must be on SD before the banks reload."""
    board.reset(RESET_DEFAULT)
    session = board.session_name_get()
    _park(board)

    board.cc_set(0, CC.LENGTH, LEN_EDIT)
    _, count0 = board.dirty_query()

    # Reload the SAME session: the writeback-all hook fires first (the name
    # technically changes to itself — the code path is identical to a real
    # hop), then all banks re-read. discard_dirty=False: this test pins the
    # writeback-on-hop semantic itself (the harness default discards).
    assert board.session_load(session, timeout=15.0, discard_dirty=False) == session
    _, count1 = board.dirty_query()
    assert count1 == count0 + 1, "session hop should write the dirty group back"

    # The reload landed on the session's config patterns; re-park and verify
    # the edit reached the slot.
    assert board.pattern_load(0, SCRATCH_BANK, SCRATCH_A)
    assert board.cc_get(0, CC.LENGTH) == LEN_EDIT, (
        "the jam edit should be IN the slot the session hop wrote back"
    )
