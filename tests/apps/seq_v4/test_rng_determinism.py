"""Per-track-RNG keystone (2026-06-19) — determinism pins.

The keystone moved every GENERATIVE random draw off the single global RNG
(jsw_rand / the static random_value in seq_random.c) onto caller-owned
per-stream state, so a span of playback can be re-simulated deterministically:

  - GENERATORS draw from a per-pool-slot xorshift seed (seq_generator_t.seed),
    minted fresh from the global RNG at ENGAGE then advanced by every
    reroll / perturb / rate-gate draw. Independent of pool iteration / alloc
    order (per-slot grain).
  - RANDOM TRAVERSAL (Random_Dir / Step / D_S) draws from a per-track xorshift
    seed (seq_core_trk_t.random_traverse_state), minted fresh at run-start.
  - CHORD_MASK's per-step gate became a pure (track,instr,step) grip_hash
    (shared with TENSION), so its gripped subset is stable per render instead
    of re-rolled. Its determinism is covered structurally by the shared
    grip_hash (already pinned by the TENSION suite) and is validated by ear
    (the texture shifts from per-render-fresh to stable-per-step); there is no
    clean harness hook for a bus-PC-set render here, so no HIL pin in this file.

The acceptance bar is "reproducible AND still musical", explicitly NOT
"bit-identical to the old global-RNG output" — draw order changed, so the
exact notes differ from before.

ROLL (GP1-while-engaged) is the determinism lever: roll_loop rerolls EVERY
unlocked step purely from the slot seed (no rate gate, no read of the prior
loop), so `set seed=S; ROLL` lands the loop in a pure function of S AND leaves
the seed in a deterministic post-roll state — a reproducible starting point for
the subsequent rate-gated wander.

The traversal-trajectory re-sim is NOT pinned here: there is no clock-step
harness verb to advance playback through the random-traversal NextStep path
deterministically. The seed get/set round-trip is pinned; the trajectory is a
by-ear / manual-playback validation (and the conversion mirrors the
already-proven robotize per-track seed precedent).
"""

import time

import pytest

from harness import (
    Board,
    Button,
    Encoder,
    Page,
)
from harness.sysex import (
    DIAL_DEPTH,
    DIAL_RANGE_MAX,
    DIAL_RANGE_MIN,
    DIAL_RATE,
)


PITCHGEN_TRACK = 0
GP1 = Button.GP(1)

SETTLE = 0.10
ENGAGE_MSG_MS = 750
ROLL_MSG_MS = 500

# A couple of arbitrary but fixed u32 seeds for the pins.
SEED_A = 0x1234_5678
SEED_B = 0x0BAD_F00D


def _setup_pitchgen(board: Board) -> None:
    board.track_drum_init(PITCHGEN_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    for _ in range(22):
        board.encoder(Encoder.DATAWHEEL, -3)
    time.sleep(SETTLE)


def _engage(board: Board) -> None:
    board.press(GP1)
    time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)


def _roll(board: Board) -> None:
    """GP1 while engaged = ROLL (reroll all unlocked steps from the seed)."""
    board.press(GP1)
    time.sleep((ROLL_MSG_MS / 1000.0) + SETTLE)


def _known_wander_state(board: Board) -> None:
    """Engage + a wide, fully-active perturb wander config so the rate-gated
    mutate path actually moves steps (rate high, depth in perturb range)."""
    _setup_pitchgen(board)
    _engage(board)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MIN, 36)  # C-1
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MAX, 84)  # C-5
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_DEPTH, 64)  # perturb (<127)


# =============================================================================
# Generator: the seed is the sole determinant of the output
# =============================================================================

@pytest.mark.hardware
def test_fresh_engage_mints_nonzero_seed(board):
    """ENGAGE mints the slot seed fresh from the global RNG; it must be
    non-zero (a 0 state would self-substitute 0xdeadbabe and alias every
    un-minted slot onto one identical stream — the load-path aliasing bug
    SlotSet guards against)."""
    _setup_pitchgen(board)
    _engage(board)
    seed = board.gen_seed_get(PITCHGEN_TRACK, 0)
    assert seed is not None, "no generator slot allocated after ENGAGE"
    assert seed != 0, "fresh-engaged slot seed is 0 — mint did not run"


@pytest.mark.hardware
def test_roll_is_seed_deterministic(board):
    """`set seed=S; ROLL` must produce the SAME loop every time — roll_loop
    draws every unlocked step from the slot seed and reads nothing else, so
    the rolled loop is a pure function of S. This is the core keystone proof:
    same seed -> same generated output."""
    _known_wander_state(board)

    assert board.gen_seed_set(PITCHGEN_TRACK, 0, SEED_A)
    _roll(board)
    loop_a = board.generator_query(PITCHGEN_TRACK, 0).loop

    assert board.gen_seed_set(PITCHGEN_TRACK, 0, SEED_A)
    _roll(board)
    loop_b = board.generator_query(PITCHGEN_TRACK, 0).loop

    assert loop_b == loop_a, (
        "same seed must reproduce the rolled loop exactly\n"
        f"  run A: {loop_a.hex()}\n"
        f"  run B: {loop_b.hex()}"
    )


@pytest.mark.hardware
def test_different_seed_different_output(board):
    """Two different seeds must produce different rolled loops (the seed is a
    real determinant, not ignored). Guards against a swap that accidentally
    drops the seed and falls back to a constant stream."""
    _known_wander_state(board)

    assert board.gen_seed_set(PITCHGEN_TRACK, 0, SEED_A)
    _roll(board)
    loop_a = board.generator_query(PITCHGEN_TRACK, 0).loop

    assert board.gen_seed_set(PITCHGEN_TRACK, 0, SEED_B)
    _roll(board)
    loop_b = board.generator_query(PITCHGEN_TRACK, 0).loop

    assert loop_a != loop_b, (
        "different seeds produced identical loops — seed not driving output"
    )


@pytest.mark.hardware
def test_wander_resimulates_from_seed(board):
    """The full rate-gated wander must re-simulate from a captured seed. After
    `set seed=S; ROLL`, BOTH the loop (= f(S)) and the seed (a deterministic
    post-roll value) are identical across runs, so a sequence of mutate cycles
    reproduces step-for-step. This is the property retroactive CAPTURE needs:
    a span replays exactly from its starting seed."""
    _known_wander_state(board)
    K = 8

    def run() -> list[bytes]:
        assert board.gen_seed_set(PITCHGEN_TRACK, 0, SEED_A)
        _roll(board)
        traj = [board.generator_query(PITCHGEN_TRACK, 0).loop]
        for _ in range(K):
            board.generator_tick_force(PITCHGEN_TRACK, 0)
            traj.append(board.generator_query(PITCHGEN_TRACK, 0).loop)
        return traj

    traj_a = run()
    traj_b = run()

    assert traj_b == traj_a, (
        "wander did not re-simulate identically from the same seed:\n"
        + "\n".join(
            f"  cycle {i}: A={a.hex()} B={b.hex()}"
            for i, (a, b) in enumerate(zip(traj_a, traj_b))
            if a != b
        )
    )
    # Sanity: the wander actually moved (otherwise the test is vacuous).
    assert traj_a[0] != traj_a[-1], (
        "wander never changed across cycles — rate/depth not exercised"
    )


@pytest.mark.hardware
def test_two_instruments_get_distinct_seeds(board):
    """Two generator slots minted fresh must get DISTINCT seeds (independent
    streams). Same-seed slots would wander in lockstep — the aliasing failure
    mode. Engages a second instrument on the same drum track."""
    _setup_pitchgen(board)
    _engage(board)  # instrument 0
    seed0 = board.gen_seed_get(PITCHGEN_TRACK, 0)
    assert seed0 is not None

    board.ui_instrument_set(1)
    _engage(board)  # instrument 1
    seed1 = board.gen_seed_get(PITCHGEN_TRACK, 1)
    assert seed1 is not None

    assert seed0 != seed1, (
        f"two fresh-engaged slots share a seed ({seed0:#x}) — streams aliased"
    )


# =============================================================================
# Traversal: seed get/set round-trip (trajectory re-sim is by-ear — no
# clock-step verb to drive the random-traversal NextStep path here)
# =============================================================================

@pytest.mark.hardware
def test_traverse_seed_roundtrips(board):
    """Pins the CMD_RNG_SEED traversal sub-ops (set/get plumbing) and the
    non-zero seeding invariant.

    NB: this does NOT exercise SEQ_CORE_Reset's per-track mint — that runs at
    boot and on playback START, and the harness has no clock-START verb, while
    board.reset()/CMD_RESET_STATE only calls SEQ_BPM_Stop. So the non-zero value
    read here is the BOOT mint (the aliasing guard that keeps a track off the
    0xdeadbabe stream); we assert it is non-zero but cannot pin re-mint-on-start.
    The full traversal-trajectory re-sim is by-ear only (no clock-step verb);
    the same GenXorshift conversion is proven end-to-end by the generator
    wander pins above."""
    # The boot mint must leave a non-zero seed (otherwise the track would alias
    # onto the shared 0xdeadbabe xorshift stream).
    minted = board.traverse_seed_get(PITCHGEN_TRACK)
    assert minted != 0, "traversal seed 0 — boot mint did not run (aliasing risk)"

    # Round-trip through set/get proves the testctrl plumbing + the field.
    board.traverse_seed_set(PITCHGEN_TRACK, SEED_B)
    assert board.traverse_seed_get(PITCHGEN_TRACK) == SEED_B, (
        "traversal seed did not round-trip through set/get"
    )
    # And a distinct value round-trips too (not a stuck read).
    board.traverse_seed_set(PITCHGEN_TRACK, SEED_A)
    assert board.traverse_seed_get(PITCHGEN_TRACK) == SEED_A, (
        "traversal seed did not round-trip through set/get (second value)"
    )
