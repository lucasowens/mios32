"""Phase F.3: cross-track processor BOUNCE-capture — non-destructive
copy of a processor track's post-stack output into an empty track's
source. The §3 "empty target → additive" half of the processor bounce
gesture (symmetric with gen BounceRelocate).

Dispatch under test (seq_ui_trkpitchgen.c, Button_Handler GP8 + the
visible-track-empty branch added by phase F.3):
  - Visible track empty AND exactly one other track has an enabled
    processor → SEQ_CORE_ProcessorBounceCapture(src, dst). LCD reads
    "CAPTURED from T<N>"; src processor untouched; dst source now
    carries the captured material.
  - Visible track empty AND ≥2 other tracks have processors → refuse
    with "multi proc - pick one" guidance.

LCD hint under test (Row 1 RHS): when on an empty track with exactly
one other-track-processor candidate → "Proc on T<N>  GP8 captures here".

Bytes-match assertion uses the chord_mask=strength=0 passthrough
property: with no held chord on the bus and strength 0, output ≡ source,
so the captured dst par-layer should equal the seeded src par-layer
byte-for-byte.
"""

import time

import pytest

from harness import Board, Button, CC, Page


SRC_TRACK = 0
DST_TRACK = 1
SECOND_PROC_TRACK = 2

# Mirror of seq_core_trk_playmode_t from seq_core.h.
TRKMODE_NORMAL = 1
TRKMODE_CHORDMASK = 4

GP8_BOUNCE = Button.GP(8)

SETTLE = 0.15
CAPTURE_MSG_MS = 1200
REFUSE_MSG_MS = 2000

# Source byte we'll seed into multiple steps of SRC_TRACK D0 — using a
# value far from 0 so any partial / off-by-N copy bug shows immediately.
SEED_NOTE = 72  # MIDI 72 = C5


def _setup_two_drum_tracks(board: Board) -> None:
    """Reinit T0 and T1 to empty drum mode, leave visible track on T0
    by default (the harness reset baseline)."""
    board.track_drum_init(SRC_TRACK)
    board.track_drum_init(DST_TRACK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    time.sleep(SETTLE)


def _enable_chordmask(board: Board, track: int, *, strength: int = 0) -> None:
    """Put `track` into TRKMODE_ChordMask with the given strength. Strength
    0 + no held chord ≡ source passthrough — output bytes equal source bytes,
    which is what the capture-byte-equality assertion relies on."""
    board.cc_set(track, CC.MODE, TRKMODE_CHORDMASK)
    board.cc_set(track, CC.CHORDMASK_STRENGTH, strength)
    # Brief settle so the SlotSync hook runs.
    time.sleep(0.05)


def _seed_source(board: Board, track: int, instr: int, steps: list[int]) -> None:
    """Plant SEED_NOTE into each step of `steps` for (track, instr)."""
    for s in steps:
        board.track_drum_par_set(track, instr, s, SEED_NOTE)


def _lcd_text(board: Board) -> str:
    return board.lcd_snapshot().text


def _wait_msg_clear(ms: int) -> None:
    time.sleep((ms / 1000.0) + SETTLE)


@pytest.mark.hardware
def test_lcd_hint_advertises_cross_track_capture(board):
    """Visible on T1 (empty), processor enabled on T0 → row 1 RHS hint
    'Proc on T 1  GP8 captures here'."""
    _setup_two_drum_tracks(board)
    _enable_chordmask(board, SRC_TRACK, strength=0)

    board.ui_track_set(DST_TRACK)
    time.sleep(SETTLE)
    text = _lcd_text(board)
    assert "Proc on T" in text, (
        f"empty visible track + one processor on another track should "
        f"show the 'Proc on Tn' capture hint:\n{text}"
    )


@pytest.mark.hardware
def test_capture_writes_dst_and_preserves_src(board):
    """GP8 on empty T1 with chord_mask enabled on T0 →
       - LCD popup names src track ("CAPTURED from T 1")
       - T0 source untouched (the seeded bytes are still there)
       - T0 still in chord_mask mode (processor preserved)
       - T1 source now holds the captured material (bytewise equality
         under strength=0 passthrough)."""
    _setup_two_drum_tracks(board)
    seeded_steps = [0, 1, 7, 17, 31]
    _seed_source(board, SRC_TRACK, 0, seeded_steps)
    _enable_chordmask(board, SRC_TRACK, strength=0)

    board.ui_track_set(DST_TRACK)
    time.sleep(SETTLE)

    board.press(GP8_BOUNCE)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "CAPTURED from T" in popup, (
        f"cross-track capture should fire the 'CAPTURED from Tn' popup; "
        f"LCD reads:\n{popup}"
    )

    _wait_msg_clear(CAPTURE_MSG_MS)

    # Src processor still enabled, src source still seeded.
    assert board.cc_get(SRC_TRACK, CC.MODE) == TRKMODE_CHORDMASK, (
        "src track playmode should still be CHORDMASK after capture"
    )
    for s in seeded_steps:
        v = board.track_drum_par_get(SRC_TRACK, 0, s)
        assert v == SEED_NOTE, (
            f"src track D0 step {s} should still be seeded ({SEED_NOTE}); "
            f"got {v} — capture must be non-destructive on src"
        )

    # Dst source now matches src (passthrough capture). Note: only the
    # seeded steps are non-zero in src; capture should mirror the WHOLE
    # par buffer, so dst's non-seeded steps must remain 0.
    for s in seeded_steps:
        v = board.track_drum_par_get(DST_TRACK, 0, s)
        assert v == SEED_NOTE, (
            f"dst track D0 step {s} should hold captured byte ({SEED_NOTE}); "
            f"got {v}"
        )
    # Spot-check a non-seeded step stays 0 (capture wrote, not OR'd).
    assert board.track_drum_par_get(DST_TRACK, 0, 8) == 0, (
        "dst track unseeded step should remain 0 after capture"
    )


@pytest.mark.hardware
def test_capture_refuses_when_multiple_src_candidates(board):
    """Two processors enabled (T0 + T2), visible on empty T1 → refuse
    with 'multi proc - pick one'. T1 source remains untouched (all
    zeros)."""
    _setup_two_drum_tracks(board)
    board.track_drum_init(SECOND_PROC_TRACK)
    _seed_source(board, SRC_TRACK, 0, [0, 4])
    _enable_chordmask(board, SRC_TRACK, strength=0)
    _enable_chordmask(board, SECOND_PROC_TRACK, strength=0)

    board.ui_track_set(DST_TRACK)
    time.sleep(SETTLE)

    board.press(GP8_BOUNCE)
    time.sleep(0.05)
    popup = _lcd_text(board)
    assert "multi proc" in popup, (
        f"multi-source-track ambiguity should fire 'multi proc - pick one'; "
        f"LCD reads:\n{popup}"
    )

    _wait_msg_clear(REFUSE_MSG_MS)

    # Dst source must remain empty.
    assert board.track_drum_par_get(DST_TRACK, 0, 0) == 0, (
        "refused capture must NOT have written to dst"
    )
    assert board.track_drum_par_get(DST_TRACK, 0, 4) == 0, (
        "refused capture must NOT have written to dst"
    )

    # Both srcs still chord_mask, srcs still seeded.
    assert board.cc_get(SRC_TRACK, CC.MODE) == TRKMODE_CHORDMASK
    assert board.cc_get(SECOND_PROC_TRACK, CC.MODE) == TRKMODE_CHORDMASK
    assert board.track_drum_par_get(SRC_TRACK, 0, 0) == SEED_NOTE
