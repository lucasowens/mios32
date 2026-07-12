"""Rung 3 — GRAVITY x Voicing register collapse (seq_core.c tension_render_range;
LOG 2026-07-12).

Deep pull narrows WHAT the chord is (act 2, byte substitution); rung 3 narrows
HOW WIDE it sits: through the DRONE zone (gravity -49..-64) the TENSION pass
scales each step's effective spread (Sprd dial + painted VSprd offset, clamped
0..12 like the expansion) toward 0 and writes the collapsed OFFSET byte back
into the render mirror's VSprd layer — mirror-faithful via the rung-2 layer,
"collapse, not dropout" for register width.

Firmware math (pinned exactly here):
  depth     = -gravity - 48                    # 1..16 across DRONE
  keep_num  = 2032 - depth*grip                # 2032 = 16*127
  collapsed = (eff*keep_num + 1016) // 2032    # round-to-nearest, ties down
  write     = 64 - dial + collapsed            # only when collapsed != eff

Continuous across ALL steps (no grip-hash gate — GRIP scales depth, not step
selection), pull-side DRONE only, chord-layer tracks with a VSprd layer only.
The Sprd dial is a live render input (folded into render_live_sig): the
dial-move pin fails if that fold regresses. The sig is evaluated in the TICK
PROLOGUE only (SEQ_CORE_RenderTracks) — stopped, renders happen solely through
explicit dirty sites, so a stopped dial turn is caught at the first tick of
the next PLAY (same class as held-chord changes). The dial-move pin therefore
runs with the transport playing.

Fixture: like test_voicing_steps — track 0 re-provisioned via track_note_init
(A3's stock geometry is a SINGLE par layer), layer A -> Chord1 painted Maj.I,
layer B -> VSprd. No transport, no note capture: every pin reads the OUTPUT
mirror (track_par_get) after the sweep->quiet re-render dance from
test_tension_chord. Voicing bypass (Sprd bit 7) is NOT pinned: the CC path is
7-bit SysEx, bit 7 unreachable from the harness.
"""

import time

import pytest

from harness import Button, CC


# seq_par_layer_type_t
PAR_TYPE_NOTE = 1
PAR_TYPE_CHORD1 = 2
PAR_TYPE_VSPRD = 21

TRACK = 0
CHORD_LAYER = 0  # par layer A
VSPRD_LAYER = 1  # par layer B
INSTR = 0
NUM_STEPS = 16
LAY_CONST_B1 = CC.LAY_CONST_A1 + 1

MAJ_I = 0x40  # Maj.I, transpose-0 octave bits -> 3 voices

SWEEP_SETTLE = 0.12   # > SEQ_RENDER_SWEEP_MS so the re-trigger renders quiet
QUIET_CATCHUP = 0.5   # playing: sig change -> quiet full render in a tick prologue

NEUTRAL_DIALS = {
    CC.VOICE_SPREAD: 0,
    CC.VOICE_INV: 0,
    CC.VOICE_STRUM: 64,
    CC.VOICE_DROP: 0,
    CC.VOICE_TILT: 64,
}


def _collapsed(eff, gravity, grip):
    """Python mirror of the firmware collapse — keep in sync with
    tension_render_range (rung 3 block)."""
    depth = -gravity - 48
    keep_num = 2032 - depth * grip
    return (eff * keep_num + 1016) // 2032


def _setup(board):
    """4-par-layer track, layer A Chord1 (Maj.I everywhere), layer B VSprd
    (unpainted), dials neutral, deterministic band context (C major, no chord
    held — conftest clears the bus stacks at suite start)."""
    board.track_note_init(TRACK)  # 16 steps x 4 par layers x 1 instr
    board.global_scale_set(scale=0, root_selection=1, keyb_root=0)
    board.cc_set(TRACK, CC.LAY_CONST_A1, PAR_TYPE_CHORD1)
    board.cc_set(TRACK, LAY_CONST_B1, PAR_TYPE_VSPRD)
    for cc, v in NEUTRAL_DIALS.items():
        board.cc_set(TRACK, cc, v)
    board.cc_set(TRACK, CC.TENSION_GRIP, 0)
    for step in range(NUM_STEPS):
        board.track_par_set(TRACK, CHORD_LAYER, INSTR, step, MAJ_I)
        board.track_par_set(TRACK, VSPRD_LAYER, INSTR, step, 0)


def _restore(board):
    try:
        board.cc_set(TRACK, CC.TENSION_GRIP, 0)
        board.tension_set(0)
        board.pattern_load(group=0, bank=0, pattern=2)  # A3
        for cc, v in NEUTRAL_DIALS.items():
            board.cc_set(TRACK, cc, v)
    except Exception as e:  # pragma: no cover - diagnostics only
        print(f"[test_tension_voicing] WARNING: restore failed: {e}")


def _paint_vsprd(board, values):
    """values: dict step -> source byte (missing steps -> 0 = unpainted)."""
    for step in range(NUM_STEPS):
        board.track_par_set(TRACK, VSPRD_LAYER, INSTR, step, values.get(step, 0))


def _quiet_render(board, vsprd_values=None):
    """Settle past any sweep window, then dirty via source writes so the next
    render is a full-buffer quiet pass (same dance as test_tension_chord)."""
    time.sleep(SWEEP_SETTLE)
    _paint_vsprd(board, vsprd_values or {})
    time.sleep(0.05)


def _mirror(board):
    return [board.track_par_get(TRACK, VSPRD_LAYER, INSTR, s)
            for s in range(NUM_STEPS)]


# --------------------------------------------------------------------------- 1

@pytest.mark.hardware
def test_collapse_only_inside_drone(board):
    """Detent, CHORD-zone edge (-48) and full PUSH (+63) all leave the VSprd
    layer byte-identical — the collapse exists only inside DRONE. Full grip,
    dial 12, painted offsets included (they must not be normalized)."""
    _setup(board)
    try:
        painted = {3: 64 + 4, 7: 64 - 2}
        board.cc_set(TRACK, CC.VOICE_SPREAD, 12)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        expect = [painted.get(s, 0) for s in range(NUM_STEPS)]
        for gravity in (0, -48, 63):
            board.tension_set(gravity)
            _quiet_render(board, painted)
            got = _mirror(board)
            assert got == expect, (
                f"gravity {gravity} must not touch the VSprd layer: {got}"
            )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 2

@pytest.mark.hardware
def test_full_pull_full_grip_closes_spread(board):
    """Gravity -64 + grip 127 (keep = 0): dial Sprd=12, unpainted layer ->
    every step reads 52 (offset -12 -> effective spread 0, close position)."""
    _setup(board)
    try:
        board.cc_set(TRACK, CC.VOICE_SPREAD, 12)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        board.tension_set(-64)
        _quiet_render(board)
        got = _mirror(board)
        assert got == [52] * NUM_STEPS, (
            f"full pull + full grip must close the voicing (all 52): {got}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 3

@pytest.mark.hardware
def test_ramp_composes_onto_painted_offsets(board):
    """Mid-DRONE (-56, depth 8) at full grip halves the EFFECTIVE spread
    (dial + painted offset), exact integer math: dial 4 with painted +4 ->
    eff 8 -> 4 (byte 64); painted -2 -> eff 2 -> 1 (byte 61); unpainted ->
    eff 4 -> 2 (byte 62)."""
    _setup(board)
    try:
        painted = {3: 64 + 4, 7: 64 - 2}
        board.cc_set(TRACK, CC.VOICE_SPREAD, 4)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        board.tension_set(-56)
        _quiet_render(board, painted)
        got = _mirror(board)
        for step in range(NUM_STEPS):
            src = painted.get(step, 0)
            eff = 4 + (src - 64 if src else 0)
            want = 64 - 4 + _collapsed(eff, -56, 127)
            assert got[step] == want, (
                f"step {step} (painted {src}, eff {eff}): {got[step]} != {want}"
            )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 4

@pytest.mark.hardware
def test_grip_scales_collapse_depth(board):
    """Same full pull (-64) at HALF grip collapses only partway: dial 12,
    grip 64 -> collapsed 6 -> byte 58 (f(gravity, grip), not gravity alone)."""
    _setup(board)
    try:
        board.cc_set(TRACK, CC.VOICE_SPREAD, 12)
        board.cc_set(TRACK, CC.TENSION_GRIP, 64)
        board.tension_set(-64)
        _quiet_render(board)
        want = 64 - 12 + _collapsed(12, -64, 64)
        got = _mirror(board)
        assert got == [want] * NUM_STEPS, (
            f"half grip must half-collapse (all {want}): {got}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 5

@pytest.mark.hardware
def test_closed_voicing_stays_unpainted(board):
    """Dial 0 + unpainted layer = nothing to collapse: full pull + full grip
    writes NOTHING (0 stays 0 — no painted-neutral normalization)."""
    _setup(board)
    try:
        board.cc_set(TRACK, CC.VOICE_SPREAD, 0)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        board.tension_set(-64)
        _quiet_render(board)
        got = _mirror(board)
        assert got == [0] * NUM_STEPS, (
            f"closed voicing must stay unpainted: {got}"
        )
    finally:
        _restore(board)


# --------------------------------------------------------------------------- 6

@pytest.mark.hardware
def test_dial_move_rerenders_collapse(board):
    """The Sprd dial is a live render input (render_live_sig fold): while the
    transport RUNS, ONLY turning the dial 12 -> 6 must re-render the collapse
    (52 -> 58). No source rewrite, no tension_set — the dial CC has no slot
    sync, so without the sig fold nothing dirties the track and the mirror
    stays stale at 52. (The sleep after PLAY lets the first-tick catch-up
    render store the sig BEFORE the dial moves, so the pin discriminates.)"""
    _setup(board)
    try:
        board.cc_set(TRACK, CC.VOICE_SPREAD, 12)
        board.cc_set(TRACK, CC.TENSION_GRIP, 127)
        board.tension_set(-64)
        _quiet_render(board)
        assert _mirror(board) == [52] * NUM_STEPS

        board.press(Button.PLAY)
        try:
            time.sleep(0.3)
            board.cc_set(TRACK, CC.VOICE_SPREAD, 6)
            time.sleep(QUIET_CATCHUP)
        finally:
            board.press(Button.STOP)
            time.sleep(0.1)
        got = _mirror(board)
        assert got == [58] * NUM_STEPS, (
            f"dial move while playing must re-render the collapse (all 58): {got}"
        )
    finally:
        _restore(board)
