"""Phase C: chord_mask processor rewrites note-bearing par-output bytes
toward the bus's currently-held chord (PC-set).

Setup:
- AUTOTEST A3 (every gate lit, par-A defaults to MBSEQ note 0x3c = MIDI 60 a.k.a.
  "C-3" in MBSEQ notation) on track 0, routed to USB0 ch1 for capture.
- Track 0 in TRKMODE_ChordMask listening on bus 1 (via CHORDMASK_BUS, which is
  the per-processor bus selector — independent of the track's own BUSASG since
  the phase G/step-7 polish).
- A D-major triad (D/F#/A) pushed into bus 1's transposer notestack via
  external Note Ons on channel 2 — firmware default `seq_midi_in_channel[1] = 2`,
  bus 0 is the record path (MODE_PLAY=1) so bus 1 is the cheapest target.

Algorithm under test (chord_mask_snap, seq_core.c):
- Search outward d=0..6 from the source note; for each d, check `note - d` then
  `note + d` against the PC mask; first match wins (lower side ties).

For source C (PC 0) with mask {2, 6, 9}: d=0/1 miss; d=2 down → 58 (PC 10) no,
up → 62 (PC 2) yes → snap to 62.
"""

import time

import pytest

from harness import Button, CC, MidiPort


# seq_core_trk_playmode_t values (mirror of seq_core.h).
TRKMODE_NORMAL = 1
TRKMODE_CHORDMASK = 4

# Bus 1's default MIDI input channel (`seq_midi_in_channel[1] = 2` per
# seq_midi_in.c). Bus 0 defaults to MODE_PLAY (record path) so notes there
# don't populate the transposer notestack — bus 1 is the test target.
BUS_INDEX = 1
BUS_CHANNEL_WIRE = 1  # 0-indexed wire nibble for the status byte → ch 2 on the wire

# D-major triad: pitch classes {D=2, F#=6, A=9}.
CHORD_DMAJ_NOTES = (62, 66, 69)
CHORD_DMAJ_PC_SET = {2, 6, 9}

# Par-A default the AUTOTEST CLEAR fixture leaves in place (SEQ_PAR_InitValueGet
# for SEQ_PAR_Type_Note → 0x3c). Source pitch on every step of A3.
SOURCE_NOTE = 0x3C  # 60, MBSEQ "C-3"

PLAY_SECONDS = 2.5


def _bus_note_msg(note: int, velocity: int) -> bytes:
    status = (0x90 if velocity > 0 else 0x80) | BUS_CHANNEL_WIRE
    return bytes([status, note & 0x7F, velocity & 0x7F])


def _push_chord(board, notes):
    for n in notes:
        board.send_raw(_bus_note_msg(n, 100))
    # Let the firmware service the input queue + populate the notestack.
    time.sleep(0.05)


def _clear_chord(board, notes):
    for n in notes:
        board.send_raw(_bus_note_msg(n, 0))
    time.sleep(0.05)


def _configure_track_as_chordmask(board, *, strength: int):
    board.cc_set(0, CC.MODE, TRKMODE_CHORDMASK)
    # Phase G/step-7 polish: the chord_mask listens on CHORDMASK_BUS, which is
    # independent of the track's own BUSASG. Track BUSASG stays at default 0.
    board.cc_set(0, CC.CHORDMASK_BUS, BUS_INDEX)
    board.cc_set(0, CC.CHORDMASK_STRENGTH, strength)


def _play_and_capture(board) -> tuple[list, float]:
    capture_t0 = board.capture_start()
    board.press(Button.PLAY)
    try:
        time.sleep(PLAY_SECONDS)
    finally:
        board.press(Button.STOP)
        time.sleep(0.1)
    notes = [
        e
        for e in board.capture_notes(since=capture_t0)
        if e.is_on and e.channel == 0
    ]
    return notes, capture_t0


@pytest.mark.hardware
def test_chord_mask_hard_lock_snaps_to_bus_chord(board):
    """Strength=127 forces every emit to a chord-tone of the bus's held PC-set.

    Source par-A is 60 on every step (A3 + Note-type default); chord PCs
    {2, 6, 9}; nearest-PC search from 60 lands on 62 (D, +2 semitones). All
    emitted Note Ons must be 62.
    """
    board.pattern_load(group=0, bank=0, pattern=2)  # A3: every gate lit
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
    _configure_track_as_chordmask(board, strength=127)

    _clear_chord(board, CHORD_DMAJ_NOTES)  # defensive: pop any prior residual
    _push_chord(board, CHORD_DMAJ_NOTES)

    try:
        notes, t0 = _play_and_capture(board)
    finally:
        _clear_chord(board, CHORD_DMAJ_NOTES)

    assert len(notes) >= 4, (
        f"expected gates from A3 to emit notes; saw {len(notes)} in "
        f"{PLAY_SECONDS}s — is AUTOTEST A3 built (every-gate-lit, par-A default)?"
    )

    expected = 62  # see module docstring for the snap derivation
    bad = [e for e in notes if e.note != expected]
    assert not bad, (
        f"chord_mask did not hard-snap source {SOURCE_NOTE} to {expected}: "
        f"{[(round(e.timestamp - t0, 3), e.note) for e in bad[:8]]}"
    )


@pytest.mark.hardware
def test_chord_mask_zero_strength_passes_through(board):
    """Strength=0 is a bypass even with a non-empty bus chord.

    Catches regressions where the renderer rewrites the output unconditionally
    (e.g. if the early-return on `!p->strength` got dropped). Source pitch
    must reach the wire unchanged.
    """
    board.pattern_load(group=0, bank=0, pattern=2)  # A3
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
    _configure_track_as_chordmask(board, strength=0)

    _clear_chord(board, CHORD_DMAJ_NOTES)
    _push_chord(board, CHORD_DMAJ_NOTES)

    try:
        notes, t0 = _play_and_capture(board)
    finally:
        _clear_chord(board, CHORD_DMAJ_NOTES)

    assert len(notes) >= 4, (
        f"expected gates from A3 to emit notes; saw {len(notes)} in "
        f"{PLAY_SECONDS}s — is AUTOTEST A3 built?"
    )

    bad = [e for e in notes if e.note != SOURCE_NOTE]
    assert not bad, (
        f"strength=0 should pass through (expected note {SOURCE_NOTE}): "
        f"{[(round(e.timestamp - t0, 3), e.note) for e in bad[:8]]}"
    )


@pytest.mark.hardware
def test_chord_mask_per_processor_bus_independent_of_busasg(board):
    """Phase G/step-7 polish: chord_mask listens on CHORDMASK_BUS (not BUSASG).

    Set track BUSASG to bus 3 (where no chord is held) and CHORDMASK_BUS to
    bus 1 (where the D-major triad lives). If the processor still snaps to
    {2,6,9}, it's reading CHORDMASK_BUS — the untangle is intact. A regression
    that re-couples slot->bus to tcc->busasg.bus would see an empty PC-set on
    bus 3 and pass through SOURCE_NOTE unchanged, failing the assertion.
    """
    board.pattern_load(group=0, bank=0, pattern=2)  # A3
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)

    board.cc_set(0, CC.MODE, TRKMODE_CHORDMASK)
    board.cc_set(0, CC.BUSASG, 3)                  # decoy: a bus with no chord
    board.cc_set(0, CC.CHORDMASK_BUS, BUS_INDEX)   # the bus we'll push to
    board.cc_set(0, CC.CHORDMASK_STRENGTH, 127)

    _clear_chord(board, CHORD_DMAJ_NOTES)
    _push_chord(board, CHORD_DMAJ_NOTES)

    try:
        notes, t0 = _play_and_capture(board)
    finally:
        _clear_chord(board, CHORD_DMAJ_NOTES)

    assert len(notes) >= 4, (
        f"expected gates from A3 to emit notes; saw {len(notes)} in {PLAY_SECONDS}s"
    )

    expected = 62  # snap derivation as in test_chord_mask_hard_lock_snaps...
    bad = [e for e in notes if e.note != expected]
    assert not bad, (
        f"CHORDMASK_BUS={BUS_INDEX} (D-maj chord) should drive the snap even "
        f"with BUSASG=3 (empty). Saw unsnapped notes: "
        f"{[(round(e.timestamp - t0, 3), e.note) for e in bad[:8]]}"
    )


@pytest.mark.hardware
def test_chord_mask_drum_scope_only_touches_selected_drums(board):
    """Phase G/step-7 polish: chord_mask honors per-drum scope (CHORDMASK_DRUM_*).

    Seed two drum slots with the same source pitch, configure chord_mask with
    drum_mask = bit 0 only (drum 0). After a render with a held chord, drum 0's
    output byte must be snapped (60 → 62) while drum 1's byte stays at the
    source value — proving the renderer's drum-loop respects p->drum_mask.

    Runs offline (no transport): each cc_set + drum_par_set goes through
    SEQ_CORE_RenderDirtySet / RenderTouched, which synchronously re-renders
    when BPM is stopped. The final SlotSync (triggered by setting MODE last)
    fires the render that the assertions read.
    """
    track = 0

    # Drum-mode track with par_layer 0 = Note (so chord_mask has a target layer
    # to rewrite); 16 instrument slots — enough for the drum 0 / drum 1 split.
    board.track_drum_init(track)

    # Push the chord BEFORE arming the processor: the SlotSync's render needs
    # the bus PC-set to already exist or it produces an identity copy.
    _clear_chord(board, CHORD_DMAJ_NOTES)
    _push_chord(board, CHORD_DMAJ_NOTES)

    # Seed source: drum 0 step 0 = 60, drum 1 step 0 = 60. drum_par_set also
    # marks render-dirty + syncs render, so the output mirror starts ≡ source.
    board.track_drum_par_set(track, 0, 0, SOURCE_NOTE)
    board.track_drum_par_set(track, 1, 0, SOURCE_NOTE)

    # Configure chord_mask params first (slot still empty since MODE is Normal).
    board.cc_set(track, CC.CHORDMASK_BUS, BUS_INDEX)
    board.cc_set(track, CC.CHORDMASK_DRUM_L, 0x01)  # drum 0 in scope
    board.cc_set(track, CC.CHORDMASK_DRUM_H, 0x00)  # drums 8..15 all out
    board.cc_set(track, CC.CHORDMASK_STRENGTH, 127)

    # Arm: SlotSync fires, RenderTouched marks the track touched. With BPM
    # stopped the immediate sync render runs in *sweep regime* — a 4-step
    # window starting at seq_core_trk[track].step. If the prior test left
    # the position away from 0, the snap lands in a window that excludes
    # step 0 and our par_get would miss it.
    board.cc_set(track, CC.MODE, TRKMODE_CHORDMASK)

    # Wait past SEQ_RENDER_SWEEP_MS (50ms) so the next render-dirty trigger
    # falls into the *quiet regime* — full-buffer render, hits every step.
    import time as _t; _t.sleep(0.12)

    # Re-write the same source bytes: SEQ_PAR_Set marks render-dirty (without
    # refreshing touched_ms), which now hits the quiet path and produces a
    # whole-buffer snap pass.
    board.track_drum_par_set(track, 0, 0, SOURCE_NOTE)
    board.track_drum_par_set(track, 1, 0, SOURCE_NOTE)

    try:
        out_drum0 = board.track_drum_par_get(track, 0, 0)
        out_drum1 = board.track_drum_par_get(track, 1, 0)
    finally:
        _clear_chord(board, CHORD_DMAJ_NOTES)
        # Disarm so we leave the fixture clean for the next test.
        board.cc_set(track, CC.MODE, TRKMODE_NORMAL)

    expected_snapped = 62  # same snap derivation as the hard-lock test
    assert out_drum0 == expected_snapped, (
        f"drum 0 (in mask) should snap {SOURCE_NOTE} → {expected_snapped}, "
        f"saw {out_drum0}"
    )
    assert out_drum1 == SOURCE_NOTE, (
        f"drum 1 (NOT in mask) should pass {SOURCE_NOTE} through unchanged, "
        f"saw {out_drum1}"
    )
