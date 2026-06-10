"""Track 2 Stage A — pitch-chain migration: transpose + force-to-scale move from
emission into the render stack (plan 2026-06-10).

The PITCH processor (slot 0, upstream of CHORD_MASK/TENSION) reproduces
SEQ_CORE_Transpose's Normal/Transpose-playmode semantics + the emission FTS
snap, writing the HEARD pitch into the output mirror. Emission keeps the legacy
chain only for Arpeggiator playmode and Chord par layers (fenced).

Pin groups:

1. MIRROR parity — static transpose, FTS snap, neutral pass-through, and the
   entanglement pin (bus planing stays in-scale: snap-after-transpose) read
   back via track_par_get. Render forcing uses the test_tension dance:
   cc_set touches (sweep regime) → wait past SEQ_RENDER_SWEEP_MS → re-write
   seeds via track_par_set (dirty, no touch) → quiet full-buffer render.

2. EMISSION evidence — the POC-rule retirement: FORCE_SCALE and TENSION GRIP
   coexist on one track, and a RUB push survives to the EMITTED notes (under
   the old emission chain, FTS re-snapped every pushed note after the stack —
   off-scale emitted PCs with FORCE_SCALE on were impossible by construction).

Fixture: AUTOTEST A3 (clean single note layer, every gate), C major / root C.
"""

import time

import pytest

from harness import Button, CC, MidiPort

# seq_core_trk_playmode_t
TRKMODE_NORMAL = 1
TRKMODE_TRANSPOSE = 2

# trkmode_flags bits (seq_core_trkmode_flags_t)
FLAG_FORCE_SCALE = 0x08

# Bus 1 (input channel 2 → wire nibble 1), matching test_tension/test_chord_mask.
BUS_INDEX = 1
BUS_CHANNEL_WIRE = 1

NOTE_LAYER = 0
INSTR = 0
SWEEP_SETTLE = 0.12  # > SEQ_RENDER_SWEEP_MS (50ms)

C_MAJOR_PCS = {0, 2, 4, 5, 7, 9, 11}


def _bus_note(board, note: int, velocity: int) -> None:
    status = (0x90 if velocity > 0 else 0x80) | BUS_CHANNEL_WIRE
    board.send_raw(bytes([status, note & 0x7F, velocity & 0x7F]))


def _seed(board, track, seeds):
    for step, value in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, value)


def _quiet_render_retrigger(board, track, seeds):
    """Wait out the sweep window, then re-write the seeds (dirty without a
    touched-refresh) so the next render is a full-buffer quiet pass."""
    time.sleep(SWEEP_SETTLE)
    _seed(board, track, seeds)
    time.sleep(0.05)


def _mirror(board, track, steps):
    return {s: board.track_par_get(track, NOTE_LAYER, INSTR, s) for s in steps}


def _reset_pitch_chain(board, track):
    board.cc_set(track, CC.MODE, TRKMODE_NORMAL)
    board.cc_set(track, CC.MODE_FLAGS, 0)
    board.cc_set(track, CC.TRANSPOSE_SEMI, 0)
    board.cc_set(track, CC.TRANSPOSE_OCT, 0)
    board.cc_set(track, CC.BUSASG, 0)


# ---------------------------------------------------------------------------
# 1. Mirror parity
# ---------------------------------------------------------------------------

@pytest.mark.hardware
def test_static_transpose_in_mirror(board):
    """Normal playmode +2 semitones: the mirror holds the transposed notes
    (emission parity for the static path SEQ_CORE_BakeForceScale used to
    reproduce at capture time)."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)  # A3
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 60, 1: 64, 2: 67}
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.TRANSPOSE_SEMI, 2)  # +2 (signed nibble encoding)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == {0: 62, 1: 66, 2: 69}, f"+2 semi mirror mismatch: {got}"
    finally:
        _reset_pitch_chain(board, track)


@pytest.mark.hardware
def test_static_transpose_negative_octave(board):
    """-1 octave (wrap encoding: 15 → −1) lands in the mirror."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 60, 1: 72}
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.TRANSPOSE_OCT, 15)  # −1 oct
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == {0: 48, 1: 60}, f"−1 oct mirror mismatch: {got}"
    finally:
        _reset_pitch_chain(board, track)


@pytest.mark.hardware
def test_neutral_pitch_chain_passthrough(board):
    """All-neutral chain (no transpose, FTS off, Normal playmode) is a
    byte-identical bypass — the slot is disarmed, not just inert."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 61, 1: 66, 2: 70, 3: 127, 4: 1}  # deliberately off-scale + extremes
    _reset_pitch_chain(board, track)
    _seed(board, track, seeds)
    _quiet_render_retrigger(board, track, seeds)
    got = _mirror(board, track, seeds)
    assert got == seeds, f"neutral chain must pass through byte-identical: {got}"


@pytest.mark.hardware
def test_fts_snaps_mirror_to_scale(board):
    """FORCE_SCALE puts the snap into the mirror: off-scale seeds land on scale
    PCs, in-scale seeds pass through exactly."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major

    seeds = {0: 61, 1: 66, 2: 60, 3: 67}  # C#, F# (off-scale), C, G (in-scale)
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.MODE_FLAGS, FLAG_FORCE_SCALE)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        for step, v in got.items():
            assert v % 12 in C_MAJOR_PCS, f"step {step}: {v} not in C major"
        assert got[2] == 60 and got[3] == 67, f"in-scale seeds must not move: {got}"
        assert got[0] != 61 and got[1] != 66, f"off-scale seeds must snap: {got}"
    finally:
        _reset_pitch_chain(board, track)


@pytest.mark.hardware
def test_planing_stays_in_scale(board):
    """THE entanglement pin: in Transpose playmode with FORCE_SCALE on, the bus
    offset applies FIRST and the snap SECOND — planed material stays in-scale
    (the reason transpose and FTS migrate together, design doc §8 Track 2)."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major

    seeds = {0: 60, 1: 64, 2: 67, 3: 71}  # C E G B
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.BUSASG, BUS_INDEX)
        board.cc_set(track, CC.MODE_FLAGS, FLAG_FORCE_SCALE)
        board.cc_set(track, CC.MODE, TRKMODE_TRANSPOSE)
        _bus_note(board, 62, 100)  # D4: offset +2 from the C-3(60) base
        time.sleep(0.05)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)

        # Everything in scale despite the +2 plane (E+2=F#, B+2=C# are off-scale
        # raw — the snap must catch exactly those).
        for step, v in got.items():
            assert v % 12 in C_MAJOR_PCS, f"step {step}: planed {v} not in C major"
        # In-scale-after-plane stays exact: C→D, G→A.
        assert got[0] == 62 and got[2] == 69, f"planed in-scale notes wrong: {got}"
        # Off-scale-after-plane snapped to a neighbor, not passed through.
        assert got[1] in (65, 67) and got[1] != 66, f"E+2 must snap off F#: {got}"
        assert got[3] in (72, 74) and got[3] != 73, f"B+2 must snap off C#: {got}"
    finally:
        _bus_note(board, 62, 0)
        _reset_pitch_chain(board, track)


@pytest.mark.hardware
def test_transposer_no_key_rests(board):
    """Transpose playmode with no key held silences the track — rests in the
    mirror (was velocity=0 at emission; same silence, now bounce-visible) —
    and the notes return when a key arrives."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 60, 1: 64, 2: 67}
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.BUSASG, BUS_INDEX)
        # HOLD off explicitly — A3 ships with HOLD set, and a held transposer
        # legitimately keeps the last released key (legacy parity).
        board.cc_set(track, CC.MODE_FLAGS, 0)
        board.cc_set(track, CC.MODE, TRKMODE_TRANSPOSE)  # no key held
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == {0: 0, 1: 0, 2: 0}, f"no key → rests expected: {got}"

        _bus_note(board, 60, 100)  # C4: offset 0
        time.sleep(0.05)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == seeds, f"key at base → seeds pass through: {got}"
    finally:
        _bus_note(board, 60, 0)
        _reset_pitch_chain(board, track)


# ---------------------------------------------------------------------------
# 2. Stage B — the LIMIT processor (slot 3, the final range fold)
# ---------------------------------------------------------------------------

CC_LIMIT_LOWER = 0x43
CC_LIMIT_UPPER = 0x44
CC_ASG_NO_FX = 0x67


@pytest.mark.hardware
def test_limit_folds_in_mirror(board):
    """The note limit lives in the mirror now. SEQ_CORE_Limit parity: TrimNote
    OCTAVE-FOLDS into [lower, upper] preserving pitch class (72→60, 73→61,
    59→71 in a [60,71] window); swapped bounds normalize; unset upper = 127."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 72, 1: 73, 2: 59, 3: 65}
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC_LIMIT_LOWER, 60)
        board.cc_set(track, CC_LIMIT_UPPER, 71)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == {0: 60, 1: 61, 2: 71, 3: 65}, f"[60,71] fold mismatch: {got}"

        # swapped bounds normalize to the same window
        board.cc_set(track, CC_LIMIT_LOWER, 71)
        board.cc_set(track, CC_LIMIT_UPPER, 60)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == {0: 60, 1: 61, 2: 71, 3: 65}, f"swapped-bounds mismatch: {got}"

        # lower-only window [60,127]: upward folds only
        board.cc_set(track, CC_LIMIT_LOWER, 60)
        board.cc_set(track, CC_LIMIT_UPPER, 0)
        seeds2 = {0: 48, 1: 100}
        _seed(board, track, seeds2)
        _quiet_render_retrigger(board, track, seeds2)
        got = _mirror(board, track, seeds2)
        assert got == {0: 60, 1: 100}, f"lower-only fold mismatch: {got}"
    finally:
        board.cc_set(track, CC_LIMIT_LOWER, 0)
        board.cc_set(track, CC_LIMIT_UPPER, 0)
        _reset_pitch_chain(board, track)


@pytest.mark.hardware
def test_limit_folds_after_tension_push(board):
    """Stack ordering: LIMIT (slot 3) runs AFTER TENSION (slot 2) — a RUB push
    escaping the window is folded back in, pitch class preserved (emission
    parity: the limit was the last Fx in the chain)."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major

    rub_pcs = {1, 6, 8}  # RUB band, no chord held (anchor = root+fifth)
    seeds = {s: n for s, n in enumerate([60, 62, 64, 65, 67, 69, 71, 72])}
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.TENSION_GRIP, 127)
        board.tension_set(35)  # RUB
        board.cc_set(track, CC_LIMIT_LOWER, 60)
        board.cc_set(track, CC_LIMIT_UPPER, 71)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)

        assert all(60 <= v <= 71 for v in got.values()), (
            f"every note must land in the window (LIMIT after TENSION): {got}"
        )
        pushed = {v % 12 for v in got.values()} & rub_pcs
        assert pushed, f"the push must survive the fold (PC-preserving): {got}"
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)
        board.tension_set(0)
        board.cc_set(track, CC_LIMIT_LOWER, 0)
        board.cc_set(track, CC_LIMIT_UPPER, 0)
        _reset_pitch_chain(board, track)


@pytest.mark.hardware
def test_limit_nofx_step_escapes_fold(board):
    """The per-step no_fx TRG layer escapes the fold (emission parity). Trick:
    point the no_fx assignment at the GATE layer (assignment 1) — A3 has every
    gate ON, so every step escapes; clearing the assignment folds again."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 72, 1: 48}
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC_LIMIT_LOWER, 60)
        board.cc_set(track, CC_LIMIT_UPPER, 71)
        board.cc_set(track, CC_ASG_NO_FX, 1)  # no_fx ≡ gate layer (all ON)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == seeds, f"no_fx steps must escape the fold: {got}"

        board.cc_set(track, CC_ASG_NO_FX, 0)
        _quiet_render_retrigger(board, track, seeds)
        got = _mirror(board, track, seeds)
        assert got == {0: 60, 1: 60}, f"fold must apply with no_fx cleared: {got}"
    finally:
        board.cc_set(track, CC_ASG_NO_FX, 0)
        board.cc_set(track, CC_LIMIT_LOWER, 0)
        board.cc_set(track, CC_LIMIT_UPPER, 0)
        _reset_pitch_chain(board, track)


# ---------------------------------------------------------------------------
# 3. Emission evidence — the POC-rule retirement
# ---------------------------------------------------------------------------

@pytest.mark.hardware
def test_fts_grip_push_coexists_at_emission(board):
    """FORCE_SCALE and TENSION GRIP on the SAME track: a RUB push survives to
    the emitted notes. FTS (slot 0) runs upstream of TENSION (slot 2), so the
    push is no longer re-corrected — under the old emission chain, off-scale
    emitted PCs with FORCE_SCALE on were impossible by construction. Retires
    the Track-1 POC rule ("FTS off on gripped tracks")."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)   # clean single-layer A3
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major

    # In-scale melody (FTS pass-through) — the push is the only off-scale source.
    melody = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 64]
    for step, note in enumerate(melody):
        board.track_par_set(track, NOTE_LAYER, INSTR, step, note)
    board.track_config(track, MidiPort.USB0, 0)      # capture port, ch 0

    # RUB band with no chord held (anchor = root+fifth {0,7}): chromatic
    # neighbors ∩ chromatic-remainder of C major = {1, 6, 8}.
    rub_pcs = {1, 6, 8}

    try:
        board.cc_set(track, CC.MODE_FLAGS, FLAG_FORCE_SCALE)
        board.cc_set(track, CC.TENSION_GRIP, 127)
        board.tension_set(35)                        # RUB push

        t = board.capture_start()
        board.press(Button.PLAY)
        time.sleep(1.6)
        board.press(Button.STOP)
        time.sleep(0.15)
        emitted = [e.note for e in board.capture_notes(since=t)
                   if e.is_on and e.channel == 0]

        assert len(emitted) >= 8, f"expected a bar of notes, saw {len(emitted)}"
        pcs = {n % 12 for n in emitted}
        pushed = pcs & rub_pcs
        passed = pcs & C_MAJOR_PCS
        assert pushed, (
            f"no RUB pitch-classes emitted with FORCE_SCALE on — the push was "
            f"re-corrected (POC rule NOT retired): PCs={sorted(pcs)}"
        )
        assert passed, f"ungripped notes should pass through in-scale: PCs={sorted(pcs)}"
        # Nothing outside scale ∪ band: the push is aimed, not noise.
        assert pcs <= (C_MAJOR_PCS | rub_pcs), f"stray PCs emitted: {sorted(pcs)}"
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)
        board.tension_set(0)
        _reset_pitch_chain(board, track)


# ---------------------------------------------------------------------------
# 4. Stage C — emission note-mutator carve-out
# ---------------------------------------------------------------------------

CC_HUMANIZE_VALUE = 0x56
CC_HUMANIZE_MODE = 0x57


@pytest.mark.hardware
def test_humanized_note_stays_in_scale_at_emission(board):
    """Stage C carve-out: humanize-note mutates pitch at emission AFTER the
    stack — with FORCE_SCALE on, the narrow re-snap keeps mutated notes in
    scale. Without the carve-out ~half the moved notes emit off-scale."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major

    melody = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 64]
    for step, note in enumerate(melody):
        board.track_par_set(track, NOTE_LAYER, INSTR, step, note)
    board.track_config(track, MidiPort.USB0, 0)

    try:
        board.cc_set(track, CC.MODE_FLAGS, FLAG_FORCE_SCALE)
        board.cc_set(track, CC_HUMANIZE_MODE, 1)    # bit 0 = note wander
        board.cc_set(track, CC_HUMANIZE_VALUE, 127)  # max intensity (±~15 semis)

        t = board.capture_start()
        board.press(Button.PLAY)
        time.sleep(1.6)
        board.press(Button.STOP)
        time.sleep(0.15)
        emitted = [e.note for e in board.capture_notes(since=t)
                   if e.is_on and e.channel == 0]

        assert len(emitted) >= 8, f"expected a bar of notes, saw {len(emitted)}"
        off_scale = [n for n in emitted if n % 12 not in C_MAJOR_PCS]
        assert not off_scale, (
            f"humanized notes must be re-snapped to scale: off-scale {off_scale}"
        )
    finally:
        board.cc_set(track, CC_HUMANIZE_MODE, 0)
        board.cc_set(track, CC_HUMANIZE_VALUE, 0)
        _reset_pitch_chain(board, track)


# ---------------------------------------------------------------------------
# 5. Stage D — the bake is dead; capture is faithful by construction
# ---------------------------------------------------------------------------

# Same scratch-slot/verify-group convention as test_capture_force_scale.py.
BOUNCE_BANK = 0
BOUNCE_PATTERN = 63
VERIFY_GROUP = 1
VERIFY_TRACK = 4


@pytest.mark.hardware
def test_capture_planed_groove_is_faithful(board):
    """Stage D headline: capture-as-heard for a PLANED groove. The deleted bake
    only ever reproduced static Normal-mode transpose — a captured bus-planed
    track silently lost its planing. Now the mirror holds plane+snap, capture
    copies it, and the generative reset clears the posture: the frozen copy IS
    the heard material, immune to later key/transposer changes."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major

    seeds = {0: 60, 1: 64, 2: 67, 3: 71}  # C E G B
    _seed(board, track, seeds)
    try:
        board.cc_set(track, CC.BUSASG, BUS_INDEX)
        board.cc_set(track, CC.MODE_FLAGS, FLAG_FORCE_SCALE)
        board.cc_set(track, CC.MODE, TRKMODE_TRANSPOSE)
        _bus_note(board, 62, 100)  # plane +2 while capturing
        time.sleep(0.05)
        _quiet_render_retrigger(board, track, seeds)

        assert board.capture_to_slot_track(
            src_track=track, dst_track=track,
            dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN,
        ), "CaptureToSlotTrack should commit"

        assert board.pattern_load(
            group=VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
        )
        time.sleep(0.15)

        # Posture cleared: the frozen copy is tape, not a re-planed transposer.
        assert board.cc_get(VERIFY_TRACK, CC.MODE) != TRKMODE_TRANSPOSE, (
            "frozen copy must not stay in Transpose playmode"
        )
        assert board.cc_get(VERIFY_TRACK, CC.MODE_FLAGS) & FLAG_FORCE_SCALE == 0, (
            "FORCE_SCALE must be reset on the frozen copy"
        )

        got = {s: board.track_par_get(VERIFY_TRACK, NOTE_LAYER, INSTR, s)
               for s in seeds}
        # Heard = snap(C-major, seed+2): C→D exact, G→A exact; E+2=F# and
        # B+2=C# snap to a scale neighbor.
        assert got[0] == 62 and got[2] == 69, f"planed in-scale notes wrong: {got}"
        assert got[1] in (65, 67) and got[3] in (72, 74), (
            f"planed off-scale notes must hold the snapped pitch: {got}"
        )
    finally:
        _bus_note(board, 62, 0)
        _reset_pitch_chain(board, track)
