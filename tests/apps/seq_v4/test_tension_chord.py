"""Act 2 — chord-space GRAVITY: chord-BYTE substitution on Chord1/2/3 par layers
(seq_core.c tension_chord_snap; LOG 2026-07-11).

A chord par byte is a table index, so TENSION substitutes the whole chord along
the band ladder instead of snapping per-note: pull collapses quality toward the
stable skeleton (Maj.I -> R.+5th -> Root), push substitutes toward tense color
(Maj.I -> Maj.6 under LEAN). The substituted byte lands in the OUTPUT mirror
(track_par_get), so these pins are exact and offline (stopped transport, the
sweep->quiet re-render dance from test_tension).

Fixture key: AUTOTEST A3 track 0 flipped to a Chord1 par layer (set 0),
NO chord held on the context bus (conftest clears the stacks at suite start),
global scale = C Major, root = C, playmode Normal (no transposer) => the band
rotation PC is 0 and bands are:
  DRONE(-64) L0={0}   CHORD(-40) leL2={0,7}   SCALE(-10) leL3=C-major
  LEAN(+20)  = scale minus chord skeleton = {2,4,5,9,11}

Seed bytes use octave bits 010 (transpose 0): byte = 0x40 | chord_ix.
  Maj.I  = 64 (PCs {0,4,7})     Maj.+ = 79 (ix 15, PCs {0,4,8} - off-scale)
Expected substitutions (scoring: pull = max common PCs then nearest PC count
then lowest index; push = strictly-better band coverage then max common):
  -10: Maj.I stays (already in scale), Maj.+ -> 64 (Maj.I)
  -40: both -> 71 (R.+5th, ix 7)
  -64: both -> 67 (Root,   ix 3)
  +20: both -> 72 (Maj.6,  ix 8)
Byte 0 is the rest idiom: never touched, never emitted.
"""

import time

import pytest

from harness import CC

# seq_par_layer_type_t
PAR_TYPE_NOTE = 1
PAR_TYPE_CHORD1 = 2

TRACK = 0
CHORD_LAYER = 0
INSTR = 0

SWEEP_SETTLE = 0.12  # > SEQ_RENDER_SWEEP_MS so the re-trigger renders quiet

# Seed bytes (octave bits 010 = transpose 0)
MAJ_I = 0x40 | 0    # 64
MAJ_AUG = 0x40 | 15  # 79
REST = 0

# Seed steps chosen by their grip-hash values so every case exercises BOTH a
# gripped and an ungripped seed (pull zone-key 0 hashes: 7->18, 8->16, 14->3,
# 11->37, 1->112; LEAN zone-key 4: 14->6, 1->24, others high). Step 2 pins the
# rest idiom (byte 0 never touched even when its hash grips).
SEEDS = {7: MAJ_I, 8: MAJ_AUG, 14: MAJ_I, 1: MAJ_AUG, 2: REST, 11: MAJ_AUG}

# LEAN zone id (firmware SEQ_CORE_TensionBandMask) — the push grip-hash key;
# pull collapses to zone-class 0.
ZONE_LEAN = 4


def _grip_hash(track: int, instr: int, step: int, zone: int) -> int:
    """Python mirror of the firmware's deterministic grip gate — computes the
    exact gripped set. Keep in sync with seq_core.c grip_hash."""
    M = 0xFFFFFFFF
    h = (track * 2654435761 + instr * 40503 + step * 2246822519
         + zone * 3266489917 + 0x9e3779b9) & M
    h ^= (h << 13) & M
    h ^= h >> 17
    h ^= (h << 5) & M
    return h % 127


def _threshold(abs_gravity: int, grip: int) -> int:
    return min(127, (abs_gravity * grip) >> 6)


def _gripped(steps, abs_gravity: int, grip: int, zone: int) -> set:
    thr = _threshold(abs_gravity, grip)
    return {s for s in steps
            if _grip_hash(TRACK, CHORD_LAYER, s, zone) < thr}


def _quiet_render_retrigger(board):
    """Re-write the seed bytes (dirty, no touched-refresh) so the next render
    is a full-buffer quiet pass that applies the processor to every step."""
    time.sleep(SWEEP_SETTLE)
    for step, value in SEEDS.items():
        board.track_par_set(TRACK, CHORD_LAYER, INSTR, step, value)
    time.sleep(0.05)


def _chord_track(board):
    """Load A3 and flip par layer A to Chord1. Caller must restore (finally)."""
    board.pattern_load(group=0, bank=0, pattern=2)  # A3
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)
    board.cc_set(TRACK, CC.LAY_CONST_A1, PAR_TYPE_CHORD1)
    for step, value in SEEDS.items():
        board.track_par_set(TRACK, CHORD_LAYER, INSTR, step, value)


def _restore(board):
    board.cc_set(TRACK, CC.TENSION_GRIP, 0)
    board.tension_set(0)
    board.cc_set(TRACK, CC.LAY_CONST_A1, PAR_TYPE_NOTE)


def _mirror(board):
    return {s: board.track_par_get(TRACK, CHORD_LAYER, INSTR, s) for s in SEEDS}


@pytest.mark.hardware
def test_chord_detent_passes_through(board):
    """GRAVITY at the detent leaves chord bytes byte-identical, full GRIP."""
    _chord_track(board)
    try:
        board.tension_set(0)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        _quiet_render_retrigger(board)
        assert _mirror(board) == SEEDS
    finally:
        _restore(board)


@pytest.mark.hardware
def test_chord_full_pull_collapses_to_root_entry(board):
    """DRONE (-64, thr 127 => every step grips): every sounding chord byte
    substitutes to the Root entry (ix 3) with its octave bits preserved;
    byte 0 (rest) is never touched."""
    _chord_track(board)
    try:
        board.tension_set(-64)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        _quiet_render_retrigger(board)
        got = _mirror(board)
        expect = {s: (0x40 | 3 if v else 0) for s, v in SEEDS.items()}
        assert got == expect, f"DRONE collapse: {got} != {expect}"
    finally:
        _restore(board)


@pytest.mark.hardware
def test_chord_ladder_is_quality_substitution(board):
    """The pull ladder substitutes QUALITY along nested bands, gripped steps
    only: SCALE keeps diatonic chords and folds Maj.+ to Maj.I; CHORD zone
    lands both on R.+5th. Push LEAN substitutes toward the sus/add color
    (Maj.6). Ungripped steps stay byte-identical."""
    _chord_track(board)
    try:
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        cases = [
            #  gravity, zone-key, {seed byte -> expected when gripped}
            (-10, 0,         {MAJ_I: MAJ_I, MAJ_AUG: MAJ_I}),          # SCALE
            (-40, 0,         {MAJ_I: 0x40 | 7, MAJ_AUG: 0x40 | 7}),    # CHORD -> R.+5th
            (20,  ZONE_LEAN, {MAJ_I: 0x40 | 8, MAJ_AUG: 0x40 | 8}),    # LEAN -> Maj.6
        ]
        for gravity, zone_key, mapping in cases:
            board.tension_set(gravity)
            _quiet_render_retrigger(board)
            gripped = _gripped(SEEDS, abs(gravity), 127, zone_key)
            got = _mirror(board)
            for step, seed in SEEDS.items():
                want = seed
                if seed and step in gripped:
                    want = mapping[seed]
                assert got[step] == want, (
                    f"gravity {gravity} step {step} (seed {seed}, "
                    f"gripped={step in gripped}): {got[step]} != {want}"
                )
    finally:
        _restore(board)


@pytest.mark.hardware
def test_chord_substitution_is_deterministic(board):
    """Two renders of the same seeds at the same dial position are
    byte-identical (grip hash, no live RNG)."""
    _chord_track(board)
    try:
        board.tension_set(-40)
        board.cc_set(TRACK, CC.TENSION_GRIP, 96)  # partial grip
        _quiet_render_retrigger(board)
        first = _mirror(board)
        _quiet_render_retrigger(board)
        second = _mirror(board)
        assert first == second, f"re-render diverged: {first} != {second}"
        assert first != SEEDS, "partial grip at -40 should have moved something"
    finally:
        _restore(board)
