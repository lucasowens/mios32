"""Track-grain load (the RECOMBINE pull verb) + the track undo keystone.

SEQ_CORE_LoadTrackFromSlot fills the missing grain cell: track-save
(CaptureToSlotTrack), group-save (PatternSave) and group-load (PatternLoad)
exist, but until now a stored track could only come back four-at-a-time. The
pull streams ONE stored section into ANY live track — any bank x pattern x
section -> any of the 16 tracks — without touching the destination group's
other tracks or seq_pattern[] (a transfusion, not a pattern switch). Every
pull first arms the one-deep track undo (full victim state: geometry, name,
CC image 0x00..0x9f, robotize anchors, par/trg); TRACK_UNDO is the one
gesture back.

Slot fixture built per test: drum-init + seed tracks 0/1 of group 0, save the
whole group to a scratch slot, then pull sections around. Destructive:
overwrites (0, 61) on the SD card and rewrites tracks in groups 0/1. Same
scratch-slot conventions as test_capture_to_slot / test_tension_persist.
"""

import time

import pytest

from harness import Board, Button, CC
from harness.sysex import RESET_UNMUTE_ALL, JRNL_UNDOABLE, JRNL_REDOABLE


SETTLE = 0.15

SCRATCH_BANK = 0
SCRATCH_PATTERN = 61  # scratch slot; never a baseline (0/1/2)

# Distinct per-section seeds: a stride bug (reading the wrong section) or a
# partial load shows immediately.
SEED_T0 = {0: 60, 1: 62, 7: 67, 17: 72, 31: 48, 63: 55}
SEED_T1 = {0: 36, 2: 38, 5: 43, 11: 51, 30: 46, 62: 49}
LENGTH_T0 = 7
LENGTH_T1 = 15

EVENT_MODE_DRUM = 3


def _seed_drum(board: Board, track: int, seed: dict[int, int]) -> None:
    for step, note in seed.items():
        board.track_drum_par_set(track, 0, step, note)


def _build_scratch_slot(board: Board) -> None:
    """Sections 0/1 of (SCRATCH_BANK, SCRATCH_PATTERN) get distinct drum
    content + LENGTH CCs; sections 2/3 carry whatever tracks 2/3 hold."""
    for track, seed, length in (
        (0, SEED_T0, LENGTH_T0),
        (1, SEED_T1, LENGTH_T1),
    ):
        board.track_drum_init(track)
        _seed_drum(board, track, seed)
        board.cc_set(track, CC.LENGTH, length)
    assert board.pattern_save(0, SCRATCH_BANK, SCRATCH_PATTERN), (
        "building the scratch slot should commit"
    )


@pytest.mark.hardware
def test_track_load_cross_group_cross_column(board):
    """THE missing grain cell: slot section 1 lands on live track 6 — a
    destination neither group-load (fans 4-at-a-time) nor remix-map tricks
    (destination fixed at group*4+section) can reach. Content, config and
    event mode all travel; the destination group's other tracks don't move."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)

    # Snapshot the destination group's OTHER tracks before the pull.
    neighbors = (4, 5, 7)
    before = {
        t: (board.cc_get(t, CC.LENGTH), board.cc_get(t, CC.EVENT_MODE))
        for t in neighbors
    }

    assert board.track_load(
        dst_track=6, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN, slot_track=1
    ), "pull should commit"
    time.sleep(SETTLE)

    # Section 1's content arrived on track 6 (group 1, column index 2).
    assert board.cc_get(6, CC.EVENT_MODE) == EVENT_MODE_DRUM, (
        "pulled section's event mode (drum) must travel with the section"
    )
    assert board.cc_get(6, CC.LENGTH) == LENGTH_T1, (
        "pulled section's LENGTH CC must travel (config, not just notes)"
    )
    for step, note in SEED_T1.items():
        v = board.track_drum_par_get(6, 0, step)
        assert v == note, (
            f"track 6 step {step}: expected section-1 byte {note}, got {v} — "
            f"wrong section read (stride bug) or partial load"
        )
    # And it is really section 1, not section 0/2 (the stride pin).
    assert board.track_drum_par_get(6, 0, 0) == SEED_T1[0] != SEED_T0[0]

    # The transfusion touched ONLY track 6.
    for t in neighbors:
        after = (board.cc_get(t, CC.LENGTH), board.cc_get(t, CC.EVENT_MODE))
        assert after == before[t], (
            f"track {t} changed across the pull ({before[t]} -> {after}); "
            f"a track-grain load must not disturb the dst group's other tracks"
        )


@pytest.mark.hardware
def test_track_load_ext_ccs_travel(board):
    """The V3 ext block is indexed per-section (ext_base + section*size):
    GRIP (0x9a) and a chord-mask CC set on the SAVED track must arrive on the
    pulled copy. Pins the ext-block seek math of SEQ_FILE_B_TrackRead."""
    board.reset(RESET_UNMUTE_ALL)
    board.cc_set(1, CC.TENSION_GRIP, 99)
    board.cc_set(1, CC.CHORDMASK_STRENGTH, 88)
    _build_scratch_slot(board)

    # Clobber the destination's ext CCs so a no-op read is detectable.
    board.cc_set(6, CC.TENSION_GRIP, 0)
    board.cc_set(6, CC.CHORDMASK_STRENGTH, 0)

    assert board.track_load(
        dst_track=6, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN, slot_track=1
    )
    time.sleep(SETTLE)

    assert board.cc_get(6, CC.TENSION_GRIP) == 99, (
        "GRIP (0x9a) must travel with the pulled section (V3 ext block)"
    )
    assert board.cc_get(6, CC.CHORDMASK_STRENGTH) == 88, (
        "chord-mask strength (0x96) must travel with the pulled section"
    )

    # Don't leak a live GRIP on track 6 into later suites (it would
    # gravity-snap any melodic content rendered there).
    board.cc_set(6, CC.TENSION_GRIP, 0)
    board.cc_set(6, CC.CHORDMASK_STRENGTH, 0)


@pytest.mark.hardware
def test_track_undo_restores_victim(board):
    """One gesture back: the pull overwrites a sculpted track; TRACK_UNDO
    restores it byte-identical (content, config, ext CCs, event mode) and is
    one-shot."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)

    # Sculpt a victim on track 6 — distinct from both slot sections.
    VICTIM_SEED = {0: 96, 3: 99, 9: 101, 21: 103}
    VICTIM_LENGTH = 11
    VICTIM_GRIP = 77
    board.track_drum_init(6)
    _seed_drum(board, 6, VICTIM_SEED)
    board.cc_set(6, CC.LENGTH, VICTIM_LENGTH)
    board.cc_set(6, CC.TENSION_GRIP, VICTIM_GRIP)

    assert board.track_load(
        dst_track=6, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN, slot_track=0
    )
    time.sleep(SETTLE)
    # The victim is gone (precondition for the restore to mean anything).
    assert board.cc_get(6, CC.LENGTH) == LENGTH_T0
    assert board.track_drum_par_get(6, 0, 0) == SEED_T0[0]

    # The query's middle field is now the unified journal state (the old `kind`
    # field is subsumed): a pull leaves the journal UNDOABLE for track 6.
    valid, state, track, _ = board.track_undo_query()
    assert (valid, state, track) == (True, JRNL_UNDOABLE, 6), (
        f"after a pull the journal must be UNDOABLE for track 6; "
        f"got valid={valid} state={state} track={track}"
    )

    assert board.track_undo() == 6, "restore should return the victim's track"
    time.sleep(SETTLE)

    assert board.cc_get(6, CC.LENGTH) == VICTIM_LENGTH, (
        "undo must restore the victim's LENGTH CC"
    )
    assert board.cc_get(6, CC.TENSION_GRIP) == VICTIM_GRIP, (
        "undo must restore the victim's ext CCs (GRIP)"
    )
    assert board.cc_get(6, CC.EVENT_MODE) == EVENT_MODE_DRUM
    for step, note in VICTIM_SEED.items():
        v = board.track_drum_par_get(6, 0, step)
        assert v == note, (
            f"undo must restore victim byte {note} at step {step}; got {v}"
        )
    # Unseeded victim step is 0 again (geometry re-init, not a partial merge).
    assert board.track_drum_par_get(6, 0, 1) == 0

    # Not one-shot anymore: the undo leaves the journal REDOABLE (the
    # 2026-06-23 net added redo). A second UNDO is a no-op (nothing UNDOABLE),
    # but a REDO re-applies the pull.
    assert board.track_undo() is None, "second undo must be a no-op (REDOABLE, not UNDOABLE)"
    valid, state, _, _ = board.track_undo_query()
    assert not valid and state == JRNL_REDOABLE
    assert board.track_redo() == 6, "REDO must re-apply the pull"
    time.sleep(SETTLE)
    assert board.track_drum_par_get(6, 0, 0) == SEED_T0[0], "redo restores the pulled content"
    # leave it undone so the victim's live GRIP doesn't leak into later suites.
    assert board.track_undo() == 6
    time.sleep(SETTLE)

    # Don't leak the victim's live GRIP on track 6 into later suites.
    board.cc_set(6, CC.TENSION_GRIP, 0)


@pytest.mark.hardware
def test_track_load_and_undo_while_running(board):
    """The live window: pull and undo with the transport RUNNING. The verb
    excludes ticks for the live-write window (forward-delay margin + critical
    section, the group-change recipe); this pins that the engine survives it —
    content arrives, the transport is still running, and the undo comes back."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)
    board.track_drum_init(6)
    _seed_drum(board, 6, {0: 96, 3: 99})

    board.press(Button.PLAY)
    try:
        time.sleep(0.3)
        assert board.tick_query()["running"], "transport should be running"

        assert board.track_load(
            dst_track=6, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN, slot_track=1
        ), "pull should commit while running"
        time.sleep(SETTLE)
        assert board.tick_query()["running"], (
            "transport must survive the pull's tick-exclusion window"
        )
        assert board.track_drum_par_get(6, 0, 0) == SEED_T1[0]

        assert board.track_undo() == 6, "undo should restore while running"
        time.sleep(SETTLE)
        assert board.tick_query()["running"], (
            "transport must survive the undo's tick-exclusion window"
        )
        assert board.track_drum_par_get(6, 0, 0) == 96
    finally:
        board.press(Button.STOP)


@pytest.mark.hardware
def test_track_load_refuses_bad_aim(board):
    """Pre-write refusals leave the live track untouched: a section index the
    slot doesn't store, and a bank out of range, both refuse cleanly (the
    aimed-gesture refuse rule — no smart-default fallback, no partial zeroing)."""
    board.reset(RESET_UNMUTE_ALL)
    _build_scratch_slot(board)

    board.track_drum_init(6)
    _seed_drum(board, 6, {0: 90, 4: 92})
    board.cc_set(6, CC.LENGTH, 13)
    # Refused pulls run no render — make the mirror fresh ourselves (sweep
    # settle + seed re-write, the _quiet_render_retrigger idiom) so the final
    # untouched-content read isn't a stale-mirror false negative.
    time.sleep(SETTLE)
    _seed_drum(board, 6, {0: 90, 4: 92})
    time.sleep(0.05)

    # Section index beyond what any slot stores (num_tracks <= 4).
    assert not board.track_load(
        dst_track=6, bank=SCRATCH_BANK, pattern=SCRATCH_PATTERN, slot_track=7
    ), "slot_track 7 must be refused"
    # Bank beyond SEQ_FILE_B_NUM_BANKS.
    assert not board.track_load(
        dst_track=6, bank=5, pattern=SCRATCH_PATTERN, slot_track=0
    ), "bank 5 must be refused"

    time.sleep(SETTLE)
    assert board.cc_get(6, CC.LENGTH) == 13, (
        "a refused pull must leave the live track's config untouched"
    )
    assert board.track_drum_par_get(6, 0, 0) == 90, (
        "a refused pull must leave the live track's content untouched"
    )
