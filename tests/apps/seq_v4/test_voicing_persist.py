"""V5 ext-CC block — voicing persistence + degrade ladder (2026-07-11 bump).

The V5 bump widened the per-track ext-CC block 0x80..0x9f -> 0x80..0xaf
(tag 0x05) so the voicing dials INV/STRUM/DROP/TILT (0xa0..0xa3) persist
with the pattern. Slots reserve ext room at bank-create time, so only
sessions created by V5 firmware carry the full block; older slots degrade
the record per the V5 -> V4 -> V3 -> none ladder. Two pins:

1. fresh-session round-trip — a session created by V5 firmware reserves
   V5-sized slots; all five voicing dials (+ GRIP as an old-range bystander)
   survive save -> clobber -> reload.

2. old-slot degrade is non-corrupting — AUTOTEST's banks were created by
   older firmware. Saving there must degrade the record rather than overflow
   into the next slot: the frozen 32-CC range (incl. SPREAD at 0x9f) still
   round-trips exactly, and the 0xa0+ dials come back EITHER persisted (the
   slot had slack for a V5 record) OR untouched-in-RAM (degraded record
   carries no voicing bytes) — never a third value, which would mean a
   misaligned read.
"""

import pytest

from harness import CC

SRC = 0

V5_SESSION = "V5EXT"
AUTOTEST_SESSION = "AUTOTEST"

# distinct write/clobber pairs so the two legitimate pin-2 outcomes are
# distinguishable from each other AND from misalignment garbage
VOICING_WRITES = {
    CC.VOICE_SPREAD: 5,    # inside the frozen 32-CC range -> persists at V4/V3 too
    CC.VOICE_INV: 0x0E,    # -2 as a two's-complement nibble
    CC.VOICE_STRUM: 70,
    CC.VOICE_DROP: 2,
    CC.VOICE_TILT: 40,
    CC.TENSION_GRIP: 99,   # old-range bystander: V5 must keep the V3 payload intact
}
CLOBBER = {
    CC.VOICE_SPREAD: 0,
    CC.VOICE_INV: 0,
    CC.VOICE_STRUM: 64,    # neutral = center detent
    CC.VOICE_DROP: 0,
    CC.VOICE_TILT: 64,
    CC.TENSION_GRIP: 0,
}


# --------------------------------------------------------------------------- 1

@pytest.mark.hardware
@pytest.mark.timeout(180)  # session create is an async SD format (~10s) + two session loads
def test_voicing_ccs_persist_in_v5_slots(board):
    """All five voicing dials + GRIP survive save/reload in a slot created by
    V5 firmware. Runs on a dedicated session (NEVER AUTOTEST — its banks are
    older-firmware-sized and would silently degrade the record)."""
    # LOW slot on purpose: a fresh bank is sparse (Create is header-only), so
    # the first write to slot N must allocate the cluster chain up to
    # N x pattern_size (~9 KB/slot) — slot 61 = ~550 KB of FAT allocation,
    # which outran the 4s reply timeout on the first run. Any slot is scratch
    # on the dedicated session; slot 1 keeps the extension trivial.
    GROUP, BANK, PATTERN = 0, 0, 1

    try:
        active = board.session_load(V5_SESSION)
    except Exception:
        active = ""
    try:
        if active != V5_SESSION:
            board.session_create(V5_SESSION)

        for cc, v in VOICING_WRITES.items():
            board.cc_set(SRC, cc, v)
        # generous timeout: still the bank's first-touch write on a fresh session
        assert board.pattern_save(GROUP, BANK, PATTERN, timeout=30.0), "save should commit"

        for cc, v in CLOBBER.items():
            board.cc_set(SRC, cc, v)
        assert board.pattern_load(GROUP, BANK, PATTERN)

        for cc, v in VOICING_WRITES.items():
            got = board.cc_get(SRC, cc)
            assert got == v, (
                f"CC 0x{cc:02x} must persist in a V5-sized slot (wrote {v}, read {got})"
            )
    finally:
        # the suite's other tests assume AUTOTEST is active — restore it even on failure
        try:
            board.session_load(AUTOTEST_SESSION)
        except Exception as e:  # pragma: no cover - diagnostics only
            print(f"[test_voicing_persist] WARNING: could not restore {AUTOTEST_SESSION}: {e}")


# --------------------------------------------------------------------------- 2

@pytest.mark.hardware
def test_old_slot_save_degrades_without_corruption(board):
    """Saving into an older-firmware bank slot degrades the ext record (per the
    fit ladder) instead of overflowing; the frozen 32-CC payload round-trips
    exactly and the 0xa0+ dials never read back garbage."""
    GROUP, BANK, PATTERN = 0, 0, 61  # scratch slot on AUTOTEST; never a baseline (0/1/2)

    for cc, v in VOICING_WRITES.items():
        board.cc_set(SRC, cc, v)
    assert board.pattern_save(GROUP, BANK, PATTERN), "save should commit"

    for cc, v in CLOBBER.items():
        board.cc_set(SRC, cc, v)
    assert board.pattern_load(GROUP, BANK, PATTERN)

    # the frozen 32-CC range must round-trip at EVERY degrade level >= V3
    for cc in (CC.TENSION_GRIP, CC.VOICE_SPREAD):
        got = board.cc_get(SRC, cc)
        assert got == VOICING_WRITES[cc], (
            f"CC 0x{cc:02x} is inside the frozen range and must persist "
            f"(wrote {VOICING_WRITES[cc]}, read {got})"
        )

    # the widened-range dials: persisted (slot had slack for a V5 record) or
    # left at the clobber values (record degraded to V4) — a third value means
    # the read arm mis-strode the ext block
    for cc in (CC.VOICE_INV, CC.VOICE_STRUM, CC.VOICE_DROP, CC.VOICE_TILT):
        got = board.cc_get(SRC, cc)
        assert got in (VOICING_WRITES[cc], CLOBBER[cc]), (
            f"CC 0x{cc:02x} read {got} — neither the written {VOICING_WRITES[cc]} "
            f"nor the clobbered {CLOBBER[cc]}: misaligned ext read"
        )
