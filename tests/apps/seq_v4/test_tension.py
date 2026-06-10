"""Tension Workbench Stage A — the GRAVITY field processor (design doc §5;
plan 2026-06-09).

GRAVITY is a global bipolar dial (−64..+63, center 0 = pass-through). It snaps
note-bearing par bytes toward a target pitch-class *band* computed from a
stability ladder, gated per note by a deterministic grip hash scaled by the
per-track GRIP CC. Pull (CCW) collapses toward the chord root / scale; push (CW)
aims at structured dissonance.

Two kinds of pin here:

1. BAND-MASK pinning (the heart, pure function via CMD_TENSION_BAND_GET) — with
   a known chord held + a known scale, the 12-bit band for each zone is exactly
   computable. No render/transport needed.

2. RENDER-PATH pins (detent pass-through, full-grip collapse, determinism,
   partial-grip stability) — run offline like test_chord_mask's drum test:
   cc_set/par_set mark render-dirty and the firmware synchronously re-renders
   when stopped, with the sweep→quiet regime dance (arm, wait past
   SEQ_RENDER_SWEEP_MS, re-trigger so the quiet full-buffer render hits).

Fixture key for the band pins: scale = C Major (index 0), root = C (0), and a
C-major triad {C,E,G} held on bus 1 (lowest = C → chord root PC 0).
  L0 = {0} (root C)          L1 = {0,7} (root+fifth, C+G)
  L2 = {0,4,7} (chord)       L3 = {0,2,4,5,7,9,11} (C major)
"""

import time

import pytest

from harness import Button, CC, MidiPort

# seq_core_trk_playmode_t
TRKMODE_NORMAL = 1

# Bus 1 (its default input channel is 2 → wire nibble 1), matching test_chord_mask.
BUS_INDEX = 1
BUS_CHANNEL_WIRE = 1

# C-major triad, lowest note C4 (root proxy PC 0).
CHORD_CMAJ_NOTES = (60, 64, 67)

# Zone ids returned by the firmware (seq_core.c SEQ_CORE_TensionBandMask).
ZONE_DETENT, ZONE_DRONE, ZONE_CHORD, ZONE_SCALE, ZONE_LEAN, ZONE_RUB, ZONE_SLIP = (
    0, 1, 2, 3, 4, 5, 6,
)


def _bus_note_msg(note: int, velocity: int) -> bytes:
    status = (0x90 if velocity > 0 else 0x80) | BUS_CHANNEL_WIRE
    return bytes([status, note & 0x7F, velocity & 0x7F])


def _push_chord(board, notes):
    for n in notes:
        board.send_raw(_bus_note_msg(n, 100))
    time.sleep(0.05)


def _clear_chord(board, notes):
    for n in notes:
        board.send_raw(_bus_note_msg(n, 0))
    time.sleep(0.05)


# ---------------------------------------------------------------------------
# 1. Band-mask pinning (pure function)
# ---------------------------------------------------------------------------

# Exact expected (zone, band) per representative gravity, C major + C-maj triad.
# Bands derived in the module docstring; bits are pitch classes 0..11.
CHORD_HELD_CASES = [
    # gravity, zone,        band     (pitch classes)
    (  0,     ZONE_DETENT,  0x000),  # detent → pass-through (band 0)
    (-10,     ZONE_SCALE,   0xAB5),  # ≤L3 : C major {0,2,4,5,7,9,11}
    (-35,     ZONE_CHORD,   0x091),  # ≤L2 : {0,4,7}
    (-50,     ZONE_DRONE,   0x081),  # ≤L1 : {0,7}  (root + fifth)
    (-60,     ZONE_DRONE,   0x001),  # L0  : {0}    (root only, extreme)
    (+12,     ZONE_LEAN,    0xA24),  # in-scale non-chord {2,5,9,11}
    (+35,     ZONE_RUB,     0x14A),  # chromatic chord-neighbours {1,3,6,8}
    (+55,     ZONE_SLIP,    0x122),  # chord planed +1 semitone {1,5,8}
]


@pytest.mark.hardware
def test_tension_band_masks_with_chord_held(board):
    """Every zone's band is the exact computed pitch-class set (the heart)."""
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major, root C
    _clear_chord(board, CHORD_CMAJ_NOTES)
    _push_chord(board, CHORD_CMAJ_NOTES)
    try:
        for gravity, exp_zone, exp_band in CHORD_HELD_CASES:
            zone, band = board.tension_band_get(gravity, track=0, bus=BUS_INDEX)
            assert (zone, band) == (exp_zone, exp_band), (
                f"gravity={gravity}: expected zone={exp_zone} band=0x{exp_band:03x}, "
                f"got zone={zone} band=0x{band:03x}"
            )
    finally:
        _clear_chord(board, CHORD_CMAJ_NOTES)


@pytest.mark.hardware
def test_tension_pull_bands_nest(board):
    """Pull side is monotone: L0 ⊂ ≤L1 ⊂ ≤L2 ⊂ ≤L3 — each deeper pull is a
    subset, so a CCW sweep can only move notes further down the ladder."""
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)
    _clear_chord(board, CHORD_CMAJ_NOTES)
    _push_chord(board, CHORD_CMAJ_NOTES)
    try:
        _, scale_band = board.tension_band_get(-10, track=0, bus=BUS_INDEX)  # ≤L3
        _, chord_band = board.tension_band_get(-35, track=0, bus=BUS_INDEX)  # ≤L2
        _, fifth_band = board.tension_band_get(-50, track=0, bus=BUS_INDEX)  # ≤L1
        _, root_band  = board.tension_band_get(-60, track=0, bus=BUS_INDEX)  # L0
    finally:
        _clear_chord(board, CHORD_CMAJ_NOTES)

    # subset chain: each deeper-pull band ⊆ the shallower one
    assert root_band & fifth_band == root_band, "L0 ⊄ ≤L1"
    assert fifth_band & chord_band == fifth_band, "≤L1 ⊄ ≤L2"
    assert chord_band & scale_band == chord_band, "≤L2 ⊄ ≤L3"
    # and strictly shrinking (this fixture has distinct levels)
    assert root_band != fifth_band != chord_band != scale_band


@pytest.mark.hardware
def test_tension_band_masks_solo_no_chord(board):
    """Graceful degradation: with no chord held the field still works from the
    global scale/root. L0/L1 come from the global root (C); ≤L3 is the scale."""
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)  # C major, root C
    _clear_chord(board, CHORD_CMAJ_NOTES)  # ensure bus empty
    # SCALE = C major; CHORD/DRONE collapse to root(+fifth) since L2 is empty.
    assert board.tension_band_get(-10, track=0, bus=BUS_INDEX) == (ZONE_SCALE, 0xAB5)
    assert board.tension_band_get(-35, track=0, bus=BUS_INDEX) == (ZONE_CHORD, 0x081)
    assert board.tension_band_get(-60, track=0, bus=BUS_INDEX) == (ZONE_DRONE, 0x001)


# ---------------------------------------------------------------------------
# 2. Render-path pins (offline; sweep→quiet dance like test_chord_mask)
# ---------------------------------------------------------------------------

NOTE_LAYER = 0
INSTR = 0
SWEEP_SETTLE = 0.12  # > SEQ_RENDER_SWEEP_MS (50ms) so the re-trigger renders quiet


def _grip_hash(track: int, instr: int, step: int, zone: int) -> int:
    """Python mirror of tension_grip_hash (seq_core.c) — same key-mix + one
    xorshift32 step, returns 0..126. Lets the partial-grip test predict the
    exact gripped set. Keep in sync with the firmware."""
    M = 0xFFFFFFFF
    h = (track * 2654435761 + instr * 40503 + step * 2246822519
         + zone * 3266489917 + 0x9e3779b9) & M
    h ^= (h << 13) & M
    h ^= h >> 17
    h ^= (h << 5) & M
    return h % 127


def _threshold(abs_gravity: int, grip: int) -> int:
    return min(127, (abs_gravity * grip) >> 6)


def _quiet_render_retrigger(board, track, seeds):
    """Re-write the seed bytes (dirty, no touched-refresh) so the next render
    is a full-buffer quiet pass that applies the processor to every step."""
    time.sleep(SWEEP_SETTLE)
    for step, value in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, value)
    time.sleep(0.05)


@pytest.mark.hardware
def test_tension_detent_passes_through(board):
    """GRAVITY at the detent is a byte-identical bypass even with full GRIP —
    the §2.3 pass-through contract (early-return on gravity==0)."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)  # A3: every gate, note layer
    _clear_chord(board, CHORD_CMAJ_NOTES)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 65, 1: 62, 2: 58}
    for step, v in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, v)

    board.tension_set(0)                       # detent
    board.cc_set(track, CC.TENSION_GRIP, 127)  # fully held — but detent bypasses
    _quiet_render_retrigger(board, track, seeds)

    try:
        for step, v in seeds.items():
            got = board.track_par_get(track, NOTE_LAYER, INSTR, step)
            assert got == v, f"detent should pass through step {step}: {v} → {got}"
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)


@pytest.mark.hardware
def test_tension_full_pull_collapses_to_root(board):
    """Full pull (gravity −64, DRONE/L0) with full GRIP snaps every note-bearing
    step to the root pitch class (C). thr = 64*127>>6 = 127 ⇒ all steps grip."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)  # A3: par-A default 60 on every step
    _clear_chord(board, CHORD_CMAJ_NOTES)            # root from global scale root = C
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {0: 65, 1: 62, 2: 58, 3: 66}  # all within ±6 of 60 → snap to C 60
    for step, v in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, v)

    board.tension_set(-64)                     # DRONE extreme, band = {C}
    board.cc_set(track, CC.TENSION_GRIP, 127)
    _quiet_render_retrigger(board, track, seeds)

    try:
        for step in list(seeds) + [4, 5]:  # seeded + default-60 steps
            got = board.track_par_get(track, NOTE_LAYER, INSTR, step)
            assert got % 12 == 0, f"full pull: step {step} should be a C, got {got}"
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)


@pytest.mark.hardware
def test_tension_pull_grip_set_grows_monotonically(board):
    """Monotone pull (§2.2 / §8.1 "collapse, not dropout"): deeper CCV pull only
    ADDS gripped notes — the gripped set at −64 ⊇ −40 ⊇ −10, even across the
    SCALE→CHORD→DRONE zone boundaries. This is the contract the pull-side single
    grip class buys; a regression to a zone-keyed pull hash would reshuffle the
    set and fail the subset checks (a voice popping back outward as you pull)."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    _clear_chord(board, CHORD_CMAJ_NOTES)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    SRC = 61  # C#, out of C major → a gripped step moves, an ungripped one stays
    steps = list(range(12))
    seeds = {s: SRC for s in steps}
    for step, v in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, v)
    board.cc_set(track, CC.TENSION_GRIP, 127)

    gripped = {}
    for gravity in (-10, -40, -64):  # SCALE, CHORD, DRONE
        board.tension_set(gravity)
        _quiet_render_retrigger(board, track, seeds)
        gripped[gravity] = {
            s for s in steps
            if board.track_par_get(track, NOTE_LAYER, INSTR, s) != SRC
        }

    try:
        assert gripped[-10] <= gripped[-40] <= gripped[-64], (
            f"pull not monotone: {gripped[-10]} ⊄ {gripped[-40]} ⊄ {gripped[-64]}"
        )
        assert gripped[-64] == set(steps), (
            f"full pull (gravity −64, grip 127) should grip all 12: {gripped[-64]}"
        )
        assert gripped[-10] != gripped[-64], "thresholds did not actually differ"
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)


@pytest.mark.hardware
def test_tension_render_is_deterministic(board):
    """Same knob position ⇒ identical output across re-renders (the determinism
    that makes states returnable and bounce faithful)."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    _clear_chord(board, CHORD_CMAJ_NOTES)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    seeds = {s: 60 + (s % 7) for s in range(8)}
    for step, v in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, v)

    board.tension_set(-35)                     # CHORD zone (partial grip)
    board.cc_set(track, CC.TENSION_GRIP, 100)
    _quiet_render_retrigger(board, track, seeds)
    first = {s: board.track_par_get(track, NOTE_LAYER, INSTR, s) for s in seeds}

    _quiet_render_retrigger(board, track, seeds)
    second = {s: board.track_par_get(track, NOTE_LAYER, INSTR, s) for s in seeds}

    try:
        assert first == second, f"non-deterministic render: {first} != {second}"
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)


@pytest.mark.hardware
def test_tension_partial_grip_matches_hash(board):
    """Partial grip: the exact gripped set is predicted by the deterministic
    hash + threshold. Gripped steps snap into the SCALE band; ungripped steps
    keep their source byte. Pins both the gate and the threshold formula."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)
    _clear_chord(board, CHORD_CMAJ_NOTES)
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)

    # Source = C# (61, PC1) on the first 12 steps. With no chord held the CHORD
    # band is ≤L1 = {C, G}; C# is out of it, so a gripped note must move (→ C)
    # and an ungripped one must stay 61 — unambiguous either way.
    SRC = 61
    steps = list(range(12))
    seeds = {s: SRC for s in steps}
    for step, v in seeds.items():
        board.track_par_set(track, NOTE_LAYER, INSTR, step, v)

    grip = 85
    gravity = -48          # CHORD zone (abs 48); pull → hash_zone 0; thr 63 → ~half
    hash_zone = 0          # firmware collapses ALL pull zones to grip class 0
    thr = _threshold(48, grip)
    board.tension_set(gravity)
    board.cc_set(track, CC.TENSION_GRIP, grip)
    _quiet_render_retrigger(board, track, seeds)

    try:
        gripped_seen = 0
        ungripped_seen = 0
        for step in steps:
            got = board.track_par_get(track, NOTE_LAYER, INSTR, step)
            predicted_grip = _grip_hash(track, INSTR, step, hash_zone) < thr
            if predicted_grip:
                gripped_seen += 1
                assert got != SRC, (
                    f"step {step} predicted gripped (thr={thr}) but byte unchanged ({got})"
                )
                assert got % 12 != 1, (
                    f"step {step} gripped but still C# (PC1, out of scale): {got}"
                )
            else:
                ungripped_seen += 1
                assert got == SRC, (
                    f"step {step} predicted ungripped but byte changed: {SRC} → {got}"
                )
        # The chosen thr must actually exercise both sides (guards a vacuous pass).
        assert gripped_seen and ungripped_seen, (
            f"thr={thr} did not split the 12 steps: "
            f"{gripped_seen} gripped / {ungripped_seen} ungripped"
        )
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)


# ---------------------------------------------------------------------------
# 3. RESOLVE (§3) — bar-quantized ramp to the detent
# ---------------------------------------------------------------------------

@pytest.mark.hardware
def test_resolve_snaps_now_when_stopped(board):
    """With the transport stopped there is no measure to ramp into, so RESOLVE
    snaps GRAVITY to the detent immediately."""
    board.tension_set(-40)
    assert board.tension_get() == -40
    board.tension_resolve()
    assert board.tension_get() == 0, "stopped RESOLVE should land on the detent now"


@pytest.mark.hardware
def test_resolve_lands_on_detent_when_running(board):
    """Running, RESOLVE ramps GRAVITY to exactly 0 by the next downbeat. Poll
    (BPM-agnostic) until it lands — the ramp + the ref_step==0 boundary pin."""
    board.tension_set(-50)
    board.press(Button.PLAY)
    try:
        board.tension_resolve()
        deadline = time.monotonic() + 6.0
        val = board.tension_get()
        while val != 0 and time.monotonic() < deadline:
            time.sleep(0.1)
            val = board.tension_get()
        assert val == 0, f"running RESOLVE should land GRAVITY on 0, got {val}"
    finally:
        board.press(Button.STOP)
        time.sleep(0.1)
        board.tension_set(0)


@pytest.mark.hardware
def test_tension_reaches_emission(board):
    """The field changes EMITTED notes, not just the render mirror. Captures real
    MIDI: a gripped melodic line collapses to the root under full pull (DRONE) and
    passes the varied melody through at the detent. (The Stage A pins read par_get
    = the output mirror; this is the missing proof that the render-stack push
    reaches the played note — the gap that hid the 'nothing affects the sound'
    session confound.) Runs on AUTOTEST's clean single-layer A3 via the fixture."""
    track = 0
    board.pattern_load(group=0, bank=0, pattern=2)   # clean single-layer A3
    board.global_scale_set(0, 0, 0)                   # C major, root C
    board.set_force_scale(track, False)
    melody = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 66, 65, 64, 62, 60]
    for step, note in enumerate(melody):
        board.track_par_set(track, NOTE_LAYER, INSTR, step, note)
    board.track_config(track, MidiPort.USB0, 0)       # capture port, ch 0
    board.cc_set(track, CC.TENSION_GRIP, 127)

    def play():
        t = board.capture_start()
        board.press(Button.PLAY)
        time.sleep(1.6)
        board.press(Button.STOP)
        time.sleep(0.15)
        return [e.note for e in board.capture_notes(since=t) if e.is_on and e.channel == 0]

    try:
        board.tension_set(-64)              # DRONE full pull → band {C}, all gripped
        drone = play()
        assert len(drone) >= 4, f"expected emitted notes under DRONE, saw {len(drone)}"
        assert all(n % 12 == 0 for n in drone), (
            f"full pull should emit only the root C, got {sorted(set(drone))}"
        )

        board.tension_set(0)                # detent → pass-through
        detent = play()
        assert len(detent) >= 4, f"expected emitted notes at detent, saw {len(detent)}"
        assert len({n % 12 for n in detent}) > 1, (
            f"detent should pass the varied melody through, got {sorted(set(detent))}"
        )
    finally:
        board.cc_set(track, CC.TENSION_GRIP, 0)
        board.tension_set(0)
