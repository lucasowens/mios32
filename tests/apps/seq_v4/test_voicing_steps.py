"""Rung 2 — per-step voicing par layers (VSprd/VInv/VStrm/VTilt, 2026-07-11).

Thin 64-biased OFFSET layers composed onto the track voicing dials at chord
expansion: eff = clamp(dial + step - 64). 0 = unpainted (neutral). The layers
live in the render mirror like the chord byte, emit no MIDI of their own, and
are read per step by SEQ_LAYER_GetEvents via tcc->link_par_layer_v*.

Pin strategy: the offset path must be BYTE-EQUIVALENT to the dial path —
same eff value, same voices. So most pins capture emitted Note Ons twice
(dial-only vs layer-only / mixed) and assert set equality. Timing (VStrm)
and velocity (VTilt) pins assert the qualitative shape instead, since strum
rides the schedule and tilt the velocity ramp.

Fixture: track 0 re-provisioned via track_note_init (16 steps x 4 par layers —
AUTOTEST A3's stock geometry is a SINGLE par layer, and V* assignments beyond
the par geometry are correctly inert: SEQ_CC_LinkUpdate scans only real layers,
so the first version of these pins silently painted into the void). All 16
gates lit, layer A -> Chord1 painted Maj.I (byte 0x40: PCs {0,4,7}, transpose-0
octave bits, 3 voices), layer B -> the V* type under test. Dials neutral unless
the pin says otherwise. Restore = pattern_load(A3) (re-applies stored geometry)
plus explicit dial neutralization: AUTOTEST slots are old-format, so the
0xA0..0xA3 dials keep their in-RAM values across a pattern load.
"""

import time

import pytest

from harness import Button, CC, MidiPort


# seq_par_layer_type_t
PAR_TYPE_NOTE = 1
PAR_TYPE_CHORD1 = 2
PAR_TYPE_VSPRD = 21
PAR_TYPE_VINV = 22
PAR_TYPE_VSTRM = 23
PAR_TYPE_VTILT = 24

TRACK = 0
CHORD_LAYER = 0  # par layer A
VOFF_LAYER = 1   # par layer B
INSTR = 0
NUM_STEPS = 16
LAY_CONST_B1 = CC.LAY_CONST_A1 + 1

MAJ_I = 0x40  # Maj.I, transpose-0 octave bits -> 3 voices

PLAY_SECONDS = 2.0

# Baseline chord onsets are 1ms-task-quantized (near-simultaneous); a strummed
# chord staggers by eff ticks per pitch rank. 20 ticks/rank is tens of ms at
# any plausible session tempo/ppqn — the thresholds sit well apart.
SIMULTANEOUS_MS = 15.0
STAGGERED_MS = 25.0

NEUTRAL_DIALS = {
    CC.VOICE_SPREAD: 0,
    CC.VOICE_INV: 0,
    CC.VOICE_STRUM: 64,
    CC.VOICE_DROP: 0,
    CC.VOICE_TILT: 64,
}


def _setup_chord_track(board, voff_type):
    """Provision a 4-par-layer melodic track (A3's geometry has no layer B),
    light all 16 gates, flip layer A -> Chord1 (painted Maj.I everywhere) and
    layer B -> the V* type under test (painted 0 = unpainted), dials neutral."""
    board.track_note_init(TRACK)  # 16 steps x 4 par layers x 1 instr
    for step8 in (0, 1):
        board.trg_byte_set(track=TRACK, step8=step8, value=0xFF,
                           trg_layer=0, instrument=0)
    board.track_config(track=TRACK, midi_port=MidiPort.USB0, channel=0)
    board.cc_set(TRACK, CC.LAY_CONST_A1, PAR_TYPE_CHORD1)
    board.cc_set(TRACK, LAY_CONST_B1, voff_type)
    for cc, v in NEUTRAL_DIALS.items():
        board.cc_set(TRACK, cc, v)
    for step in range(NUM_STEPS):
        board.track_par_set(TRACK, CHORD_LAYER, INSTR, step, MAJ_I)
        board.track_par_set(TRACK, VOFF_LAYER, INSTR, step, 0)


def _restore(board):
    """pattern_load re-applies A3's stored geometry/layers/CCs; the 0xA0..0xA3
    dials are NOT in old-format slots, so neutralize them explicitly."""
    try:
        board.pattern_load(group=0, bank=0, pattern=2)
        for cc, v in NEUTRAL_DIALS.items():
            board.cc_set(TRACK, cc, v)
    except Exception as e:  # pragma: no cover - diagnostics only
        print(f"[test_voicing_steps] WARNING: restore failed: {e}")


def _paint_offsets(board, value):
    for step in range(NUM_STEPS):
        board.track_par_set(TRACK, VOFF_LAYER, INSTR, step, value)


def _play_and_capture(board):
    t0 = board.capture_start()
    board.press(Button.PLAY)
    try:
        time.sleep(PLAY_SECONDS)
    finally:
        board.press(Button.STOP)
        time.sleep(0.1)
    return [
        e
        for e in board.capture_notes(since=t0)
        if e.is_on and e.channel == 0
    ]


def _note_set(events):
    return {e.note for e in events}


def _first_onset_by_pitch(events):
    first = {}
    for e in events:
        if e.note not in first:
            first[e.note] = e.timestamp
    return first


# --------------------------------------------------------------------------- 1

@pytest.mark.hardware
def test_vsprd_layer_equals_spread_dial(board):
    """Dial Sprd=3 and layer-only +3 must expand to the SAME pitch set (the
    offset path is byte-equivalent to the dial path), and both differ from the
    close voicing."""
    _setup_chord_track(board, PAR_TYPE_VSPRD)
    try:
        baseline = _note_set(_play_and_capture(board))
        assert len(baseline) == 3, f"Maj.I close voicing should be 3 pitches, got {sorted(baseline)}"

        board.cc_set(TRACK, CC.VOICE_SPREAD, 3)
        dial_set = _note_set(_play_and_capture(board))
        assert dial_set != baseline, "Sprd=3 dial must widen the voicing"

        board.cc_set(TRACK, CC.VOICE_SPREAD, 0)
        _paint_offsets(board, 64 + 3)
        layer_set = _note_set(_play_and_capture(board))
        assert layer_set == dial_set, (
            f"VSprd layer +3 (dial 0) must equal dial Sprd=3: "
            f"dial={sorted(dial_set)} layer={sorted(layer_set)}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 2

@pytest.mark.hardware
def test_vsprd_offset_composes_onto_dial(board):
    """OFFSET semantics (not override): dial Sprd=2 + layer +1 == dial Sprd=3.
    An override would read eff=1 here and produce a narrower set."""
    _setup_chord_track(board, PAR_TYPE_VSPRD)
    try:
        board.cc_set(TRACK, CC.VOICE_SPREAD, 3)
        dial3_set = _note_set(_play_and_capture(board))

        board.cc_set(TRACK, CC.VOICE_SPREAD, 2)
        _paint_offsets(board, 64 + 1)
        mixed_set = _note_set(_play_and_capture(board))
        assert mixed_set == dial3_set, (
            f"dial 2 + layer +1 must equal dial 3 (offset composition): "
            f"dial3={sorted(dial3_set)} mixed={sorted(mixed_set)}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 3

@pytest.mark.hardware
def test_vsprd_unpainted_and_clamp(board):
    """Value 0 = unpainted (neutral: set equals no-voicing baseline even with
    the layer assigned); a huge +40 offset clamps to the dial ceiling (== dial
    Sprd=12) instead of wrapping."""
    _setup_chord_track(board, PAR_TYPE_VSPRD)
    try:
        baseline = _note_set(_play_and_capture(board))  # layer assigned, all 0

        board.cc_set(TRACK, CC.VOICE_SPREAD, 0)
        _paint_offsets(board, 64 + 40)
        clamped_set = _note_set(_play_and_capture(board))

        board.cc_set(TRACK, CC.VOICE_SPREAD, 12)
        _paint_offsets(board, 0)
        dial12_set = _note_set(_play_and_capture(board))

        assert clamped_set == dial12_set, (
            f"+40 offset must clamp to spread 12: "
            f"clamped={sorted(clamped_set)} dial12={sorted(dial12_set)}"
        )
        assert dial12_set != baseline, "spread 12 must differ from close voicing"

        board.cc_set(TRACK, CC.VOICE_SPREAD, 0)
        unpainted = _note_set(_play_and_capture(board))
        assert unpainted == baseline, (
            f"all-0 layer must be pass-through: "
            f"baseline={sorted(baseline)} unpainted={sorted(unpainted)}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 4

@pytest.mark.hardware
def test_vinv_layer_equals_inversion_dial(board):
    """Dial Inv=-2 (two's-complement nibble 0x0E) and layer-only -2 must expand
    to the same pitch set."""
    _setup_chord_track(board, PAR_TYPE_VINV)
    try:
        board.cc_set(TRACK, CC.VOICE_INV, 0x0E)  # -2
        dial_set = _note_set(_play_and_capture(board))

        board.cc_set(TRACK, CC.VOICE_INV, 0)
        _paint_offsets(board, 64 - 2)
        layer_set = _note_set(_play_and_capture(board))
        assert layer_set == dial_set, (
            f"VInv layer -2 (dial 0) must equal dial Inv=-2: "
            f"dial={sorted(dial_set)} layer={sorted(layer_set)}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 5

@pytest.mark.hardware
def test_vstrm_layer_staggers_and_dial_still_works(board):
    """Strum now rides the event as a precomputed tick offset. Three captures:
    baseline chords are near-simultaneous; a dial-only strum staggers (guards
    the emission-side refactor); a layer-only strum with the dial at the
    center detent staggers too (the new per-step path). Up-strum = low voice
    sounds first."""
    _setup_chord_track(board, PAR_TYPE_VSTRM)
    try:
        events = _play_and_capture(board)
        onsets = _first_onset_by_pitch(events)
        assert len(onsets) == 3, f"expected 3 pitches, got {sorted(onsets)}"
        lo, hi = min(onsets), max(onsets)
        base_ms = abs(onsets[hi] - onsets[lo]) * 1000.0
        assert base_ms < SIMULTANEOUS_MS, (
            f"baseline chord voices should be near-simultaneous, got {base_ms:.1f}ms"
        )

        board.cc_set(TRACK, CC.VOICE_STRUM, 64 + 20)
        onsets = _first_onset_by_pitch(_play_and_capture(board))
        dial_ms = (onsets[hi] - onsets[lo]) * 1000.0
        assert dial_ms > STAGGERED_MS, (
            f"dial-only up-strum must stagger low-before-high, got {dial_ms:.1f}ms"
        )

        board.cc_set(TRACK, CC.VOICE_STRUM, 64)
        _paint_offsets(board, 64 + 20)
        onsets = _first_onset_by_pitch(_play_and_capture(board))
        layer_ms = (onsets[hi] - onsets[lo]) * 1000.0
        assert layer_ms > STAGGERED_MS, (
            f"layer-only up-strum (dial at detent) must stagger, got {layer_ms:.1f}ms"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 6

@pytest.mark.hardware
def test_vtilt_layer_ramps_velocity(board):
    """Baseline voices share one velocity; a layer-only +tilt (dial at detent)
    accents the top voice and softens the bottom — strictly increasing velocity
    by pitch."""
    _setup_chord_track(board, PAR_TYPE_VTILT)
    try:
        events = _play_and_capture(board)
        vel_by_pitch = {}
        for e in events:
            vel_by_pitch.setdefault(e.note, set()).add(e.velocity)
        assert len(vel_by_pitch) == 3, f"expected 3 pitches, got {sorted(vel_by_pitch)}"
        base_vels = {v for vs in vel_by_pitch.values() for v in vs}
        assert len(base_vels) == 1, f"baseline velocities should be uniform, got {base_vels}"
        # flipping layer B to VTilt drops the Velocity link -> default 100; if a
        # fixture surprise pushes the base near 127 the +31 ramp would clamp flat
        # and this pin couldn't discriminate — fail loudly as a fixture problem
        base = next(iter(base_vels))
        assert base <= 110, f"fixture: base velocity {base} leaves no tilt headroom"

        _paint_offsets(board, 64 + 63)
        events = _play_and_capture(board)
        vel_by_pitch = {}
        for e in events:
            vel_by_pitch.setdefault(e.note, set()).add(e.velocity)
        for pitch, vs in vel_by_pitch.items():
            assert len(vs) == 1, f"tilt must be deterministic per pitch, got {pitch}: {vs}"
        ordered = [next(iter(vel_by_pitch[p])) for p in sorted(vel_by_pitch)]
        assert ordered[0] < ordered[1] < ordered[2], (
            f"+tilt must ramp velocity up the chord, got {ordered}"
        )
    finally:
        _restore(board)
