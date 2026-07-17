"""Slicer tenant — render-stack TAIL buffer permute (plan 2026-07-15).

The SLICE processor (slot 5, after LIMIT) divides the loop into equal slices
(GRID = 2/4/8/16 steps) and resequences them as a par+trg buffer permutation:
output slice i copies its step-block from source slice map[i]. The mapping is
deterministic (grip_hash zones 0x60-0x63): PAINTED order (a SlcOr par layer,
value 1..16, 0 = unpainted, painted anywhere inside a slice counts) wins →
SEED fills unpainted positions → REPT repeats the previous OUTPUT slice →
REV reverses steps inside a slice. STRENGTH is a thermometer over hash-ranked
slices — painted positions engage across the LOWER dial half (all in by 63),
seeded across the UPPER (all in at 127). REPT/REV are independent thermometers
that skip painted positions; a 127 dial engages EVERY slice (grip_hash caps at
126), which is what makes those pins hash-free and exactly predictable.

Pins (all stopped-transport: CC writes and par/trg writes flush the render
synchronously — SliceSlotSync → RenderTouched, track_par_set → RenderDirtySet):

1. pass-through — all dials 0 = identity; armed grid+seed with STRENGTH 0 is
   STILL identity (the grammar's true-zero rule).
2. painted order exact — full 4-slice swap painted on the SlcOr layer at
   strength 127: note + velocity blocks and the trg gate bits all move
   together; a mid-slice paint counts; the SlcOr layer itself is NOT permuted
   (control-topology exclusion).
3. thermometer halves — strength 63 engages ALL painted positions and NO
   seeded ones (painted thr 0..62 < 63 <= seeded thr 63..126).
4. REPT=127 — every slice repeats its predecessor => the whole loop becomes
   slice 0 (the cascade), par + trg.
5. REV=127 — every slice plays its steps backwards in place, par + trg.
6. seed rearrangement — a seeded chop is a rearrangement of source blocks
   (every output block equals SOME source block), byte-identical across
   re-renders, and not always identity across a seed sweep.
7. bypass bit — GRID bit 7 drops the chop out (identity) and back in
   (byte-identical), config preserved.
8. GRID clamp — writes above 4 clamp to 4, preserving the bypass bit.
9. drum mode — per-instrument permute: two drums' par ramps and their
   DIFFERENT gate patterns each reverse per-slice under REV=127.
10. persistence (degrade-safe) — slice CCs in an old-firmware AUTOTEST slot
    either persist (V5-sized slack) or stay untouched-in-RAM, never garbage.
"""

import time

import pytest

from harness import CC

TRACK = 0
NOTE, VEL, LEN, ORD = 0, 1, 2, 3  # track_note_init layers; layer 3 re-assigned to SlcOr
INSTR = 0
PAR_TYPE_SLICEORD = 25
SETTLE = 0.05

GRID_2, GRID_4, GRID_8 = 1, 2, 3  # CC clicks -> slice length 2/4/8 steps

RAMP = [60 + s for s in range(16)]   # note layer: value identifies the step
VRAMP = [40 + s for s in range(16)]  # velocity layer: moves with its step
GATES = (0x2D, 0x71)                 # distinctive 16 gate bits, LSB-first per byte


def _gate_bits(byts):
    v = byts[0] | (byts[1] << 8)
    return [(v >> s) & 1 for s in range(16)]


def _bits_to_bytes(bits):
    v = sum(b << s for s, b in enumerate(bits))
    return (v & 0xFF, (v >> 8) & 0xFF)


def _expect(map16, glen, rev=None, src=RAMP):
    """Expected par row: output step s <- source slice map16[s//glen] at offset
    k (mirrored when that slice is REV-flagged)."""
    out = []
    for s in range(16):
        osl, k = divmod(s, glen)
        kk = (glen - 1 - k) if (rev and rev[osl]) else k
        out.append(src[map16[osl] * glen + kk])
    return out


def _expect_gates(map16, glen, rev=None):
    src = _gate_bits(GATES)
    return _bits_to_bytes(_expect(map16, glen, rev, src=src))


def _init(board):
    """Fresh melodic fixture: Note/Vel ramps, layer 3 -> SlcOr, known gates."""
    board.track_note_init(TRACK)
    board.cc_set(TRACK, CC.LAY_CONST_A4, PAR_TYPE_SLICEORD)  # layer 3: Roll -> SlcOr
    for s in range(16):
        board.track_par_set(TRACK, NOTE, INSTR, s, RAMP[s])
        board.track_par_set(TRACK, VEL, INSTR, s, VRAMP[s])
    board.trg_byte_set(TRACK, 0, GATES[0])
    board.trg_byte_set(TRACK, 1, GATES[1])


def _slice_off(board, track=TRACK):
    for cc in (CC.SLICE_GRID, CC.SLICE_SEED, CC.SLICE_STRENGTH,
               CC.SLICE_REPT, CC.SLICE_REV):
        board.cc_set(track, cc, 0)


def _dial(board, grid=0, seed=0, strength=0, rept=0, rev=0, track=TRACK):
    board.cc_set(track, CC.SLICE_SEED, seed)
    board.cc_set(track, CC.SLICE_STRENGTH, strength)
    board.cc_set(track, CC.SLICE_REPT, rept)
    board.cc_set(track, CC.SLICE_REV, rev)
    board.cc_set(track, CC.SLICE_GRID, grid)  # grid last — the arming write
    time.sleep(SETTLE)


def _mirror(board, layer=NOTE, track=TRACK, instr=INSTR):
    return [board.track_par_get(track, layer, instr, s) for s in range(16)]


def _gates_out(board, instr=0):
    _, out = board.trg_byte_get(TRACK, 0, instr, 0, 2)
    return (out[0], out[1])


# --------------------------------------------------------------------------- 1

@pytest.mark.hardware
def test_pass_through(board):
    """All dials 0 = identity; and an ARMED grid+seed with STRENGTH 0 is still
    identity — the grammar's true-zero rule (rept/rev also 0)."""
    _init(board)
    try:
        _slice_off(board)
        time.sleep(SETTLE)
        assert _mirror(board) == RAMP, "all-zero slicer must be identity"

        _dial(board, grid=GRID_4, seed=37, strength=0)
        assert _mirror(board) == RAMP, "strength 0 must be a true pass-through"
        assert _gates_out(board) == GATES, "strength 0 must not move gates"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 2

@pytest.mark.hardware
def test_painted_order_exact(board):
    """Full painted swap at strength 127 (seed 0): par blocks (note AND
    velocity) and trg gate bits move together; mid-slice paint counts; the
    SlcOr layer itself stays un-permuted."""
    _init(board)
    try:
        # 4 slices of 4 steps; painted source slices (1-based): pos0<-2,
        # pos1<-1 (painted MID-slice at step 5), pos2<-4, pos3<-3.
        board.track_par_set(TRACK, ORD, INSTR, 0, 2)
        board.track_par_set(TRACK, ORD, INSTR, 5, 1)
        board.track_par_set(TRACK, ORD, INSTR, 8, 4)
        board.track_par_set(TRACK, ORD, INSTR, 12, 3)
        _dial(board, grid=GRID_4, strength=127)

        map16 = [1, 0, 3, 2]  # 0-based source slice per output position
        assert _mirror(board, NOTE) == _expect(map16, 4), "note blocks must follow the painted order"
        assert _mirror(board, VEL) == _expect(map16, 4, src=VRAMP), "velocity must travel with its step"
        assert _gates_out(board) == _expect_gates(map16, 4), "gate bits must travel with their step"

        got_ord = _mirror(board, ORD)
        want_ord = [2, 0, 0, 0, 0, 1, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0]
        assert got_ord == want_ord, "the SlcOr layer itself must NOT be permuted"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 3

@pytest.mark.hardware
def test_strength_halves(board):
    """Strength 63: ALL painted positions engaged (thr 0..62), NO seeded ones
    (thr 63..126) — the intent-first / chaos-second sweep."""
    _init(board)
    try:
        board.track_par_set(TRACK, ORD, INSTR, 0, 2)  # pos0 <- source slice 2
        _dial(board, grid=GRID_4, seed=99, strength=63)

        got = _mirror(board)
        assert got[0:4] == RAMP[4:8], "painted position must engage in the lower half"
        assert got[4:16] == RAMP[4:16], "seeded positions must stay identity below 64"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 4

@pytest.mark.hardware
def test_rept_full_stutter(board):
    """REPT 127 (everything else 0): every slice repeats its predecessor, so
    the cascade turns the whole loop into slice 0 — par and trg."""
    _init(board)
    try:
        _dial(board, grid=GRID_4, rept=127)
        map16 = [0, 0, 0, 0]
        assert _mirror(board) == _expect(map16, 4), "full REPT must cascade to slice 0"
        assert _gates_out(board) == _expect_gates(map16, 4), "gates must stutter with the slices"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 5

@pytest.mark.hardware
def test_rev_full_reverse(board):
    """REV 127 (everything else 0): every slice plays backwards in place."""
    _init(board)
    try:
        _dial(board, grid=GRID_4, rev=127)
        map16, rev = [0, 1, 2, 3], [1, 1, 1, 1]
        assert _mirror(board) == _expect(map16, 4, rev=rev), "full REV must mirror each slice"
        assert _gates_out(board) == _expect_gates(map16, 4, rev=rev), "gate bits must reverse too"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 6

@pytest.mark.hardware
def test_seed_deterministic_rearrangement(board):
    """A seeded chop is a rearrangement of source blocks (with replacement),
    byte-identical across re-renders, and a seed sweep is not stuck at
    identity."""
    _init(board)
    try:
        src_blocks = {tuple(RAMP[i:i + 2]) for i in range(0, 16, 2)}
        any_nonidentity = False
        for seed in range(1, 7):
            _dial(board, grid=GRID_2, seed=seed, strength=127)
            got = _mirror(board)
            for i in range(0, 16, 2):
                assert tuple(got[i:i + 2]) in src_blocks, (
                    f"seed {seed}: output block at step {i} is not a source block: {got[i:i+2]}"
                )
            if got != RAMP:
                any_nonidentity = True
        assert any_nonidentity, "seeds 1..6 all rendered identity — shuffle looks dead"

        # determinism: same dials re-rendered => byte-identical mirror
        _dial(board, grid=GRID_2, seed=3, strength=127)
        first = _mirror(board)
        for s in range(16):  # dirty re-seed (same values) forces a fresh quiet render
            board.track_par_set(TRACK, NOTE, INSTR, s, RAMP[s])
        time.sleep(SETTLE)
        assert _mirror(board) == first, "same seed must re-render byte-identically"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 7

@pytest.mark.hardware
def test_bypass_bit_preserves_config(board):
    """GRID bit 7 = config-preserving bypass: chop -> identity -> the SAME chop."""
    _init(board)
    try:
        board.track_par_set(TRACK, ORD, INSTR, 0, 2)
        board.track_par_set(TRACK, ORD, INSTR, 4, 1)
        _dial(board, grid=GRID_4, strength=127)
        chopped = _mirror(board)
        assert chopped != RAMP, "painted swap must chop"

        board.cc_set(TRACK, CC.SLICE_GRID, GRID_4 | 0x80)  # bypass on
        time.sleep(SETTLE)
        assert _mirror(board) == RAMP, "bypass must restore identity"

        board.cc_set(TRACK, CC.SLICE_GRID, GRID_4)  # bypass off
        time.sleep(SETTLE)
        assert _mirror(board) == chopped, "un-bypass must restore the SAME chop"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 8

@pytest.mark.hardware
def test_grid_clamps(board):
    """GRID writes above 4 clamp to 4; the clamp preserves the bypass bit."""
    _init(board)
    try:
        board.cc_set(TRACK, CC.SLICE_GRID, 0x07)
        assert board.cc_get(TRACK, CC.SLICE_GRID) == 4, "grid must clamp to 4"
        board.cc_set(TRACK, CC.SLICE_GRID, 0x87)
        assert board.cc_get(TRACK, CC.SLICE_GRID) == 0x84, "clamp must keep the bypass bit"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 9

@pytest.mark.hardware
def test_drum_mode_per_instrument(board):
    """Drum tracks permute per PAR instrument: two drums with different ramps
    each reverse per-slice under REV=127, and the (single-trg-instrument
    fixture's) gate bits reverse with them. track_drum_init geometry: par =
    64 steps x 1 layer x 16 drums, trg = 64 steps x 8 layers x 1 instrument —
    only steps 0..15 are seeded/asserted; the zero tail reverses to zeros."""
    ramps = {0: [30 + s for s in range(16)], 1: [90 + s for s in range(16)]}

    board.track_drum_init(TRACK)
    try:
        for instr, ramp in ramps.items():
            for s in range(16):
                board.track_drum_par_set(TRACK, instr, s, ramp[s])
        board.trg_byte_set(TRACK, 0, GATES[0], trg_layer=0, instrument=0)
        board.trg_byte_set(TRACK, 1, GATES[1], trg_layer=0, instrument=0)
        _dial(board, grid=GRID_4, rev=127)

        map16, rev = [0, 1, 2, 3], [1, 1, 1, 1]
        for instr, ramp in ramps.items():
            got = _mirror(board, layer=0, instr=instr)
            assert got == _expect(map16, 4, rev=rev, src=ramp), (
                f"drum {instr}: par blocks must reverse per slice"
            )
        want = _bits_to_bytes(_expect(map16, 4, rev=rev, src=_gate_bits(GATES)))
        _, out = board.trg_byte_get(TRACK, 0, 0, 0, 2)
        assert (out[0], out[1]) == want, "drum gates must reverse per slice"
    finally:
        _slice_off(board)


# --------------------------------------------------------------------------- 10

@pytest.mark.hardware
def test_persist_degrade_safe(board):
    """Old-firmware AUTOTEST slot: slice CCs either persist (the slot had V5
    slack) or stay untouched-in-RAM (degraded record) — never a third value
    (the voicing-persist misalignment sentinel, extended to 0xa4..0xa8)."""
    GROUP, BANK, PATTERN = 0, 0, 61  # scratch slot; never a baseline (0/1/2)
    WRITES = {
        CC.SLICE_GRID: 2,
        CC.SLICE_SEED: 42,
        CC.SLICE_STRENGTH: 101,
        CC.SLICE_REPT: 17,
        CC.SLICE_REV: 9,
    }
    CLOBBER = {cc: 0 for cc in WRITES}

    try:
        for cc, v in WRITES.items():
            board.cc_set(TRACK, cc, v)
        assert board.pattern_save(GROUP, BANK, PATTERN), "save should commit"

        for cc, v in CLOBBER.items():
            board.cc_set(TRACK, cc, v)
        assert board.pattern_load(GROUP, BANK, PATTERN)

        got = {cc: board.cc_get(TRACK, cc) for cc in WRITES}
        persisted = all(got[cc] == WRITES[cc] for cc in WRITES)
        untouched = all(got[cc] == CLOBBER[cc] for cc in WRITES)
        assert persisted or untouched, (
            f"slice CCs must persist or degrade cleanly, got mixed/garbage: "
            f"{ {hex(cc): got[cc] for cc in got} }"
        )
    finally:
        _slice_off(board)
