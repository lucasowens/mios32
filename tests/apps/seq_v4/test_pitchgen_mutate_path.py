"""Phase H mutate-path coverage — pins the §8 step 6 carveouts that were
previously listen-test only.

The production mutate path (SEQ_GENERATOR_Tick → mutate_loop) only fires
on track-wrap (step → 0 transition). ROLL is the harness's other entry
point to the rerolled-loop machinery, but ROLL bypasses rate / depth /
MULT — it's the on-demand override — so it can't substitute for these
tests. Phase H added CMD_GENERATOR_TICK_FORCE to call mutate_loop +
write_loop_to_source synchronously on a target slot, plus
CMD_GENERATOR_DIAL_SET / CMD_GENERATOR_MULT_SET so tests can land on
exact dial values without spamming encoder events.

Tests in this file:
  - rate=0 pass-through (mutation_rate=0 leaves loop untouched)
  - depth=0 frozen (mutation_depth=0 leaves loop untouched)
  - LOCK survives across a real mutate cycle (not just ROLL)
  - MULT=0 step is frozen across many mutate cycles even at rate=127
  - MULT=3 step mutates harder than MULT=1 step (statistical)
  - UNIFORM contour produces roughly flat distribution (statistical)
  - LOW_BIAS, HIGH_BIAS, TRIANGLE shift the distribution as advertised
"""

import collections
import time

import pytest

from harness import (
    Board,
    Button,
    Encoder,
    Page,
)
from harness.sysex import (
    DIAL_CONTOUR,
    DIAL_DEPTH,
    DIAL_RANGE_MAX,
    DIAL_RANGE_MIN,
    DIAL_RATE,
)


PITCHGEN_TRACK = 0
GP1 = Button.GP(1)

SETTLE = 0.10
ENGAGE_MSG_MS = 750

# Contour codes — match seq_generator.h SEQ_GENERATOR_CONTOUR_*.
CONTOUR_UNIFORM = 0
CONTOUR_LOW = 1
CONTOUR_HIGH = 2
CONTOUR_TRIANGLE = 3

# MULT codes — match seq_generator.h SEQ_GENERATOR_MULT_*.
MULT_MUTE = 0
MULT_HALF = 1
MULT_DEFAULT = 2
MULT_DOUBLE = 3


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


def _wide_known_state(board: Board) -> None:
    """Set up the page + ENGAGE + force the slot into a known wide-range
    full-reroll config so mutate / contour math is well-defined for
    assertions.

    Default phase-H dials are bass-narrow (Lo:36 Hi:48) which is fine for
    most tests but for statistical contour checks we want a wider range
    (more bucket headroom). Tests that need that call this; tests that
    just check 'did anything change' can use the engage defaults.

    Self-contained: _setup_pitchgen runs the track-drum-init that ENGAGE
    requires (otherwise GP1 silently refuses with 'needs drum-mode track'
    and no slot is allocated, breaking the dial-sets below).
    """
    _setup_pitchgen(board)
    _engage(board)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MIN, 36)  # C-1
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MAX, 84)  # C-5 (4 octaves)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_DEPTH, 127)


# =============================================================================
# Rate / depth — early-out short-circuits in mutate_loop
# =============================================================================

@pytest.mark.hardware
def test_rate_zero_pass_through(board):
    """mutation_rate=0 must leave the loop bytes untouched across mutate
    cycles. §2.3 sweep contract — every dial must sweep to a true
    pass-through at 0.
    """
    _setup_pitchgen(board)
    _engage(board)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 0)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None

    for _ in range(5):
        board.generator_tick_force(PITCHGEN_TRACK, 0)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None
    assert after.loop == before.loop, (
        f"rate=0 must be pass-through; loop changed across 5 mutates.\n"
        f"  before: {before.loop.hex()}\n"
        f"  after:  {after.loop.hex()}"
    )


@pytest.mark.hardware
def test_depth_zero_freezes_loop(board):
    """mutation_depth=0 must leave the loop bytes untouched. Even with
    rate=127 the early-out 'depth == 0' branch in mutate_loop guarantees
    no step moves. The phase-G design contract.
    """
    _setup_pitchgen(board)
    _engage(board)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_DEPTH, 0)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None

    for _ in range(5):
        board.generator_tick_force(PITCHGEN_TRACK, 0)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None
    assert after.loop == before.loop, (
        f"depth=0 must freeze the loop; changed across 5 mutates.\n"
        f"  before: {before.loop.hex()}\n"
        f"  after:  {after.loop.hex()}"
    )


# =============================================================================
# LOCK across the real mutate path (not just ROLL — that path is already pinned)
# =============================================================================

@pytest.mark.hardware
def test_lock_survives_real_mutate_cycles(board):
    """LOCKed steps must survive mutate_loop, not just roll_loop. The
    test_pitchgen_step6 LOCK test exercises ROLL only; this hits the
    measure-boundary code path through TICK_FORCE. Catches a regression
    where LOCK-skip is wired into roll_loop but not mutate_loop.
    """
    _wide_known_state(board)
    locked = [3, 17, 42]
    for s in locked:
        # Reach into testctrl: there's no direct LockSet; use the existing
        # UI cursor + GP6 button. _setup parked cursor at 0 already.
        # Scroll to target step then press GP6.
        # Reset to 0 first to avoid drift.
        for _ in range(22):
            board.encoder(Encoder.DATAWHEEL, -3)
        remaining = s
        while remaining >= 3:
            board.encoder(Encoder.DATAWHEEL, +3)
            remaining -= 3
        if remaining > 0:
            board.encoder(Encoder.DATAWHEEL, +remaining)
        board.press(Button.GP(6))
        time.sleep((500 / 1000.0) + SETTLE)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None
    for s in locked:
        assert before.locks[s], f"step {s} should be locked pre-mutate"

    for _ in range(8):
        board.generator_tick_force(PITCHGEN_TRACK, 0)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None
    for s in locked:
        assert after.loop[s] == before.loop[s], (
            f"LOCKed step {s} value moved across 8 mutate cycles: "
            f"{before.loop[s]} → {after.loop[s]}"
        )
        assert after.locks[s], f"step {s} lock bit cleared by mutate"


# =============================================================================
# MULT — per-step rate scaling
# =============================================================================

@pytest.mark.hardware
def test_mult_zero_freezes_step_across_mutates(board):
    """A step with MULT=0 (mute) must never change across the rate-gated
    mutate path, even at rate=127. Contrast with LOCK: LOCK is a hard skip;
    MULT=0 short-circuits the rate threshold (`continue` before the rate
    comparison). Both should produce step-freezes, via different paths.
    """
    _wide_known_state(board)
    # Stamp MULT=0 at step 12 (odd-nibble path: 12 is even, so low nibble).
    board.generator_mult_set(PITCHGEN_TRACK, 0, 12, MULT_MUTE)
    # Also stamp MULT=0 at step 17 (odd → high nibble) for both-paths coverage.
    board.generator_mult_set(PITCHGEN_TRACK, 0, 17, MULT_MUTE)

    before = board.generator_query(PITCHGEN_TRACK, 0)
    assert before is not None
    assert before.mult[12] == MULT_MUTE
    assert before.mult[17] == MULT_MUTE

    for _ in range(15):
        board.generator_tick_force(PITCHGEN_TRACK, 0)

    after = board.generator_query(PITCHGEN_TRACK, 0)
    assert after is not None
    assert after.loop[12] == before.loop[12], (
        f"MULT=0 step 12 (even/low-nibble) mutated across 15 cycles: "
        f"{before.loop[12]} → {after.loop[12]}"
    )
    assert after.loop[17] == before.loop[17], (
        f"MULT=0 step 17 (odd/high-nibble) mutated across 15 cycles: "
        f"{before.loop[17]} → {after.loop[17]}"
    )

    # Sanity: at least some other (non-locked, non-muted) step DID change.
    other_changed = sum(
        1
        for i in range(64)
        if i not in (12, 17)
        and before.loop[i] != after.loop[i]
    )
    assert other_changed > 0, (
        "with rate=127 and 15 mutates, many non-muted steps should have "
        "changed — sanity check failed, possibly TICK_FORCE not firing"
    )


@pytest.mark.hardware
def test_mult_double_mutates_more_often_than_default(board):
    """A step with MULT=3 (2×) should change more often across a long
    sequence of mutate cycles than a step with MULT=2 (1×, the default).
    Statistical — exact ratios depend on the PRNG, but with ~30 cycles
    and rate=64 (~50% baseline touch chance), MULT=3 should hit roughly
    twice as often. Generous tolerance: assert MULT=3 strictly > MULT=2.
    """
    _wide_known_state(board)
    # Reset rate to a value where saturation doesn't mask the 2× scaling.
    # rate=64 → MULT=2 threshold = 128 (saturates u8 r ∈ [0,254] at ~50%);
    # MULT=3 threshold = 256 → clamped to 255 → effectively 100%. So MULT=3
    # ≈ 2× MULT=2 over enough samples.
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 64)
    # Stamp MULT=3 at step 8, leave step 24 at the default 2 (1×).
    board.generator_mult_set(PITCHGEN_TRACK, 0, 8, MULT_DOUBLE)
    # Lock every other step except 8 and 24 so we don't accidentally
    # measure noise from the rest of the loop. Lock via GP6 is slow; just
    # forcibly mute everything else via MULT=0.
    for s in range(64):
        if s not in (8, 24):
            board.generator_mult_set(PITCHGEN_TRACK, 0, s, MULT_MUTE)

    # Drive many cycles, counting changes at the two steps of interest.
    prev = board.generator_query(PITCHGEN_TRACK, 0)
    assert prev is not None
    changes_8 = 0
    changes_24 = 0
    N = 30
    for _ in range(N):
        board.generator_tick_force(PITCHGEN_TRACK, 0)
        cur = board.generator_query(PITCHGEN_TRACK, 0)
        assert cur is not None
        if cur.loop[8] != prev.loop[8]:
            changes_8 += 1
        if cur.loop[24] != prev.loop[24]:
            changes_24 += 1
        prev = cur

    assert changes_8 > changes_24, (
        f"MULT=3 step should mutate more often than MULT=2 step "
        f"across {N} cycles: got step8={changes_8} vs step24={changes_24}"
    )
    # And both should have changed at least a few times (otherwise the
    # test isn't measuring the right thing).
    assert changes_24 > 0, (
        f"baseline MULT=2 step never changed in {N} cycles — sanity fail"
    )


# =============================================================================
# Statistical contour shape — distribution of rerolled values
# =============================================================================


def _collect_reroll_distribution(
    board: Board,
    contour: int,
    n_cycles: int = 60,
    lo: int = 36,
    hi: int = 84,
) -> collections.Counter:
    """Run many full-reroll mutate cycles and collect the histogram of
    loop[] values. Uses rate=127, depth=127 so every unlocked, MULT=2
    step gets full-rerolled every cycle via the contour distribution.

    Each cycle replaces ~64 values; n_cycles=60 → ~3800 samples. Plenty
    for bucket-bias checks at the precision we need (a contour shape
    that biases by 30%+ vs uniform).
    """
    _setup_pitchgen(board)
    _engage(board)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MIN, lo)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RANGE_MAX, hi)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_RATE, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_DEPTH, 127)
    board.generator_dial_set(PITCHGEN_TRACK, 0, DIAL_CONTOUR, contour)

    counter: collections.Counter = collections.Counter()
    for _ in range(n_cycles):
        board.generator_tick_force(PITCHGEN_TRACK, 0)
        snap = board.generator_query(PITCHGEN_TRACK, 0)
        assert snap is not None
        for v in snap.loop:
            counter[v] += 1
    return counter


def _bucket_thirds(counter: collections.Counter, lo: int, hi: int) -> tuple[int, int, int]:
    """Return (low_third, mid_third, high_third) sample counts across the
    range [lo..hi]. Used to assert distribution bias coarsely.
    """
    span = hi - lo + 1
    boundary_lo = lo + span // 3
    boundary_hi = lo + (2 * span) // 3
    low = sum(c for v, c in counter.items() if lo <= v < boundary_lo)
    mid = sum(c for v, c in counter.items() if boundary_lo <= v < boundary_hi)
    high = sum(c for v, c in counter.items() if boundary_hi <= v <= hi)
    return low, mid, high


@pytest.mark.hardware
def test_contour_uniform_is_roughly_flat(board):
    """UNIFORM contour should fill the [min..max] range ~evenly. Allow a
    generous tolerance (any third within 50% of an even split). Tighter
    bounds would be flaky against the PRNG.
    """
    counter = _collect_reroll_distribution(board, CONTOUR_UNIFORM, n_cycles=40)
    low, mid, high = _bucket_thirds(counter, 36, 84)
    total = low + mid + high
    assert total > 0
    third = total / 3.0
    for label, val in (("low", low), ("mid", mid), ("high", high)):
        ratio = val / third
        assert 0.5 < ratio < 1.5, (
            f"UNIFORM {label} third ratio {ratio:.2f} outside [0.5,1.5]; "
            f"counts low={low} mid={mid} high={high} total={total}"
        )


@pytest.mark.hardware
def test_contour_triangle_peaks_at_midrange(board):
    """TRIANGLE contour (sum of two half-span uniforms) peaks at mid; both
    edges should be reduced. mid count > low AND mid count > high.
    """
    counter = _collect_reroll_distribution(board, CONTOUR_TRIANGLE, n_cycles=40)
    low, mid, high = _bucket_thirds(counter, 36, 84)
    assert mid > low, (
        f"TRIANGLE should peak at mid; mid={mid} not > low={low}"
    )
    assert mid > high, (
        f"TRIANGLE should peak at mid; mid={mid} not > high={high}"
    )


@pytest.mark.hardware
def test_contour_low_bias_skews_low(board):
    """LOW_BIAS (min of two uniforms) skews toward range_min. The low
    third should dominate; the high third should be the smallest.
    """
    counter = _collect_reroll_distribution(board, CONTOUR_LOW, n_cycles=40)
    low, mid, high = _bucket_thirds(counter, 36, 84)
    assert low > mid, (
        f"LOW_BIAS should pull low > mid; got low={low} mid={mid}"
    )
    assert low > high, (
        f"LOW_BIAS should pull low > high; got low={low} high={high}"
    )


@pytest.mark.hardware
def test_contour_high_bias_skews_high(board):
    """HIGH_BIAS (max of two uniforms) skews toward range_max. The high
    third should dominate; the low third should be the smallest.
    """
    counter = _collect_reroll_distribution(board, CONTOUR_HIGH, n_cycles=40)
    low, mid, high = _bucket_thirds(counter, 36, 84)
    assert high > mid, (
        f"HIGH_BIAS should pull high > mid; got high={high} mid={mid}"
    )
    assert high > low, (
        f"HIGH_BIAS should pull high > low; got high={high} low={low}"
    )
