"""Unified action journal — the play-readiness panic UNDO/REDO net (design
§10(a2), play-readiness safety net 2026-06-23).

ONE one-deep store now sits behind every deliberate track-grain gesture (pull /
utility copy-paste-clear / generator ENGAGE / capture-to-track), consolidating
the three bespoke one-deeps and adding the REDO that never existed. The
contract pinned here:

  - edit -> UNDO -> REDO round-trips the track BYTE-EXACT (lazy `after`:
    the post-gesture state is captured at UNDO time, restored by REDO);
  - the SELECT+CLEAR gesture is a TOGGLE — press once = undo, again = redo,
    again = undo — and from an EMPTY journal it NEVER falls through to a
    destructive clear (a slipped SELECT can't cost material);
  - ambient generator wander (auto-mutate ticks) injected between UNDO and
    REDO must NOT pollute or invalidate the journal — only the four deliberate
    verbs arm it.

RAM only, no SD. Uses drum-init scratch tracks; no session/bank writes.
"""

import time

import pytest

from harness import Board, Button, CC, Page
from harness.sysex import (
    RESET_DEFAULT,
    JRNL_EMPTY,
    JRNL_UNDOABLE,
    JRNL_REDOABLE,
    JRNL_TRACK,
    JRNL_ORGANISM,
    DIAL_DEPTH,
    DIAL_RATE,
    DIAL_RANGE_MAX,
    DIAL_RANGE_MIN,
)

MEASURE_TICKS = 16 * 96  # steps_per_measure(16) * ticks_per_16th(96)
SEQ_CC_LENGTH = 0x4d
EVENT_MODE_NOTE = 0

pytestmark = pytest.mark.hardware

SETTLE = 0.10
ENGAGE_MSG_MS = 750
GP1 = Button.GP(1)

# A short fingerprint of a track's Note par-layer (drum instr 0) is enough to
# prove the byte-exact round trip without dumping the whole 1 KB buffer.
FP_STEPS = range(6)


def _note_fp(board: Board, track: int, instr: int = 0) -> list[int]:
    return [board.track_drum_par_get(track, instr, s) for s in FP_STEPS]


def _seed_note(board: Board, track: int, base: int, instr: int = 0) -> None:
    """Write a known, non-default Note value per step (base+step), so two
    tracks can be seeded distinguishably."""
    for s in FP_STEPS:
        board.track_drum_par_set(track, instr, s, (base + s) & 0x7f)


def _select_clear(board: Board) -> None:
    """The SELECT+CLEAR gesture: hold SELECT, tap CLEAR, release SELECT
    (depressed=False = press, =True = release — harness convention)."""
    board.button(Button.SELECT, depressed=False)
    try:
        board.press(Button.CLEAR)
    finally:
        board.button(Button.SELECT, depressed=True)
    time.sleep(SETTLE)


def _engage(board: Board, track: int, instr: int = 0) -> None:
    """ENGAGE a Turing generator on (track, instr) via the PITCHGEN page —
    the same recipe as the pitchgen / gen-state tests. GP1 ENGAGE acts on the
    VISIBLE track, so select it first (default-visible is track 0)."""
    board.track_drum_init(track)
    board.ui_track_set(track)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(instr)
    time.sleep(SETTLE)
    board.press(GP1)
    time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)
    g = board.generator_query(track, instr)
    assert g is not None and g.engaged, "ENGAGE preamble failed"


def test_undo_redo_byte_exact(board):
    """capture-to-track arms the journal; UNDO restores dst byte-exact, REDO
    re-applies it byte-exact, and a third step toggles back — the core net."""
    board.reset(RESET_DEFAULT)
    SRC, DST = 1, 0
    board.track_drum_init(SRC)
    board.track_drum_init(DST)
    _seed_note(board, SRC, base=40)
    _seed_note(board, DST, base=80)
    time.sleep(SETTLE)

    before = _note_fp(board, DST)
    assert before == [(80 + s) & 0x7f for s in FP_STEPS]

    assert board.capture_to_track(SRC, DST), "capture-to-track should commit"
    time.sleep(SETTLE)
    after = _note_fp(board, DST)
    assert after == [(40 + s) & 0x7f for s in FP_STEPS], (
        "capture should copy src's note content onto dst"
    )
    assert after != before

    # journal armed by the capture
    undo_available, state, _, _ = board.track_undo_query()
    assert undo_available and state == JRNL_UNDOABLE

    # UNDO -> dst byte-exact pre-capture
    assert board.track_undo() == DST
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == before, "UNDO must restore dst byte-exact"
    _, state, _, _ = board.track_undo_query()
    assert state == JRNL_REDOABLE

    # REDO -> dst byte-exact post-capture
    assert board.track_redo() == DST
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == after, "REDO must re-apply the capture byte-exact"
    _, state, _, _ = board.track_undo_query()
    assert state == JRNL_UNDOABLE

    # UNDO again -> back to before (one-deep toggle)
    assert board.track_undo() == DST
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == before


def test_select_clear_toggles_undo_redo(board):
    """SELECT+CLEAR is the performance-surface toggle: press = undo, press =
    redo, press = undo. (The midiphy panel has no UNDO button.)"""
    board.reset(RESET_DEFAULT)
    SRC, DST = 1, 0
    board.track_drum_init(SRC)
    board.track_drum_init(DST)
    _seed_note(board, SRC, base=40)
    _seed_note(board, DST, base=80)
    time.sleep(SETTLE)

    before = _note_fp(board, DST)
    assert board.capture_to_track(SRC, DST), "capture should commit"
    time.sleep(SETTLE)
    after = _note_fp(board, DST)
    assert after != before

    _select_clear(board)                               # undo
    assert _note_fp(board, DST) == before, "first SELECT+CLEAR must undo"
    _select_clear(board)                               # redo
    assert _note_fp(board, DST) == after, "second SELECT+CLEAR must redo"
    _select_clear(board)                               # undo
    assert _note_fp(board, DST) == before, "third SELECT+CLEAR must undo again"


def test_select_clear_empty_never_clears(board):
    """Safety invariant preserved: with an EMPTY journal, SELECT+CLEAR reports
    and never falls through to a destructive clear, so a slipped SELECT can't
    cost material. (reset() empties the journal.)"""
    board.reset(RESET_DEFAULT)
    TRK = 0
    board.track_drum_init(TRK)
    _seed_note(board, TRK, base=55)
    time.sleep(SETTLE)
    fp = _note_fp(board, TRK)

    _, state, _, _ = board.track_undo_query()
    assert state == JRNL_EMPTY, "reset must leave the journal empty"

    _select_clear(board)
    assert _note_fp(board, TRK) == fp, (
        "SELECT+CLEAR with nothing armed must NEVER clear the track"
    )


def test_wander_between_undo_and_redo_does_not_pollute(board):
    """A generator wandering on another track between UNDO and REDO must not
    pollute or invalidate the journal — only the deliberate verbs arm it. The
    DST journal entry survives the wander and REDO is still byte-exact."""
    board.reset(RESET_DEFAULT)
    WANDER, SRC, DST = 2, 1, 0

    # Pre-engage the wanderer (its ENGAGE arm gets overwritten by the capture
    # below, so the journal entry under test belongs to DST, not WANDER).
    _engage(board, WANDER)

    board.track_drum_init(SRC)
    board.track_drum_init(DST)
    _seed_note(board, SRC, base=40)
    _seed_note(board, DST, base=80)
    time.sleep(SETTLE)

    before = _note_fp(board, DST)
    assert board.capture_to_track(SRC, DST), "capture should commit"
    time.sleep(SETTLE)
    after = _note_fp(board, DST)
    assert after != before

    assert board.track_undo() == DST                   # journal now REDOABLE
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == before

    # Inject wander on WANDER while the DST entry sits REDOABLE.
    for _ in range(8):
        board.generator_tick_force(WANDER, 0)
    _, state, _, _ = board.track_undo_query()
    assert state == JRNL_REDOABLE, (
        "ambient generator wander must not pollute/invalidate the journal"
    )

    # REDO still byte-exact, unaffected by the wander.
    assert board.track_redo() == DST
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == after, "REDO must survive intervening wander"


def test_symmetric_redo_preserves_post_undo_edit(board):
    """REDO is symmetric (snapshots live first), so a redo that overwrites work
    done AFTER the undo is itself undoable — the toggle never silently loses
    material. (Review fix D: an asymmetric redo would discard the hand edit.)"""
    board.reset(RESET_DEFAULT)
    SRC, DST, INSTR = 1, 0, 0
    board.track_drum_init(SRC)
    board.track_drum_init(DST)
    _seed_note(board, SRC, base=40)
    _seed_note(board, DST, base=80)
    time.sleep(SETTLE)

    before = _note_fp(board, DST)
    assert board.capture_to_track(SRC, DST), "capture should commit"
    time.sleep(SETTLE)
    after = _note_fp(board, DST)

    assert board.track_undo() == DST          # dst -> before, journal REDOABLE
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == before

    # Hand-edit the rescued track — a direct par write that does NOT arm/invalidate
    # the journal (the dangerous interleave: edit between undo and redo).
    for s in FP_STEPS:
        board.track_drum_par_set(DST, INSTR, s, (10 + s) & 0x7f)
    edited = _note_fp(board, DST)
    assert edited != before and edited != after

    assert board.track_redo() == DST          # dst -> after (re-applies capture)
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == after

    # The payoff: UNDO after the REDO restores the HAND EDIT, not the original
    # pre-capture content — an asymmetric (one-way) redo would have lost it.
    assert board.track_undo() == DST
    time.sleep(SETTLE)
    assert _note_fp(board, DST) == edited, (
        "symmetric redo: undo after redo must restore the post-undo edit, not discard it"
    )


def test_generator_engage_undo_redo_byte_exact(board):
    """The generator-ENGAGE arming verb round-trips through the journal: ENGAGE
    arms (on new-alloc success) + writes the loop; UNDO removes the seeded gen
    AND restores the pre-engage source; REDO re-applies the gen byte-exact. This
    is the path whose `after`/REDO carries a non-zero generator pool entry."""
    board.reset(RESET_DEFAULT)
    TRK, INSTR = 0, 0
    board.track_drum_init(TRK)
    _seed_note(board, TRK, base=70)
    time.sleep(SETTLE)
    before = _note_fp(board, TRK)
    assert board.generator_query(TRK, INSTR) is None

    # ENGAGE inline (NOT via _engage, which re-inits the track and would wipe the
    # seed before the journal arms): select the track, then GP1 ENGAGE so the
    # seeded note layer IS the pre-engage state the journal snapshots.
    board.ui_track_set(TRK)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(INSTR)
    time.sleep(SETTLE)
    board.press(GP1)
    time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)
    after = _note_fp(board, TRK)
    assert board.generator_query(TRK, INSTR) is not None
    assert after != before, "ENGAGE should overwrite the seeded note layer with the loop"

    assert board.track_undo() == TRK
    time.sleep(SETTLE)
    assert board.generator_query(TRK, INSTR) is None, "UNDO must remove the seeded generator"
    assert _note_fp(board, TRK) == before, "UNDO must restore the pre-ENGAGE source byte-exact"

    assert board.track_redo() == TRK
    time.sleep(SETTLE)
    g = board.generator_query(TRK, INSTR)
    assert g is not None and g.engaged, "REDO must re-install the generator (engaged)"
    assert _note_fp(board, TRK) == after, "REDO must restore the post-ENGAGE source byte-exact"


def test_pattern_load_invalidates_journal(board):
    """A raw pattern load (SEQ_PATTERN_Load) drops the journal so a later UNDO
    can't clobber the freshly-loaded track with pre-load bytes."""
    board.reset(RESET_DEFAULT)
    board.track_drum_init(1)
    board.track_drum_init(0)
    _seed_note(board, 1, base=40)
    assert board.capture_to_track(1, 0), "capture should arm the journal"
    _, state, _, _ = board.track_undo_query()
    assert state == JRNL_UNDOABLE

    board.pattern_load(0, 0, 0)               # raw load of group 0 -> invalidate
    time.sleep(SETTLE)
    _, state, _, _ = board.track_undo_query()
    assert state == JRNL_EMPTY, "a pattern load must invalidate the journal"
    assert board.track_undo() is None, "no undo across a load (would clobber loaded bytes)"


def test_cross_gesture_one_deep_clobber(board):
    """One shared one-deep store: a gesture on track B overwrites the undo armed
    by a gesture on track A — only the most recent deliberate gesture is undoable."""
    board.reset(RESET_DEFAULT)
    SRC, A, B = 2, 0, 1
    for t in (SRC, A, B):
        board.track_drum_init(t)
    _seed_note(board, SRC, base=40)
    time.sleep(SETTLE)

    assert board.capture_to_track(SRC, A), "first gesture (-> A) commits"
    _, _, track, _ = board.track_undo_query()
    assert track == A

    assert board.capture_to_track(SRC, B), "second gesture (-> B) commits"
    valid, state, track, _ = board.track_undo_query()
    assert valid and state == JRNL_UNDOABLE and track == B, (
        "the second gesture must own the one-deep slot"
    )
    assert board.track_undo() == B, "undo restores B (A's undo was clobbered)"


def _setup_wander_track(board: Board, track: int) -> None:
    """A melodic, fully-active, gated pitch generator on `track` — a real
    generative line for the ring to capture (mirrors test_capture_span)."""
    board.track_drum_init(track)
    board.cc_set(track, CC.EVENT_MODE, EVENT_MODE_NOTE)  # -> Note layer for materialize
    board.cc_set(track, SEQ_CC_LENGTH, 15)               # 16 steps = one whole measure
    board.ui_track_set(track)
    board.page_set(Page.PITCHGEN)
    board.ui_instrument_set(0)
    board.press(GP1)                                     # ENGAGE
    time.sleep((ENGAGE_MSG_MS / 1000.0) + SETTLE)
    board.generator_dial_set(track, 0, DIAL_RANGE_MIN, 36)
    board.generator_dial_set(track, 0, DIAL_RANGE_MAX, 84)
    board.generator_dial_set(track, 0, DIAL_RATE, 127)
    board.generator_dial_set(track, 0, DIAL_DEPTH, 64)
    board.trg_byte_set(track, 0, 0xff)                   # gate every step
    board.trg_byte_set(track, 1, 0xff)
    time.sleep(SETTLE)


def test_capture_span_gesture_is_undoable(board):
    """THE gap the green suite missed (review fix A): the live performance-surface
    CAPTURE gesture (SEQ_CORE_CaptureSpan, the UTILITY-hold grab) must arm the
    journal so the grab is undoable — and must not corrupt the net's targeting."""
    board.reset(RESET_DEFAULT)
    SRC, DST, INSTR = 0, 2, 0

    # Fill the ring on a real generative SRC track.
    _setup_wander_track(board, SRC)
    remaining = 3 * MEASURE_TICKS
    while remaining > 0:
        chunk = min(remaining, 16000)
        board.clock_step(chunk)
        remaining -= chunk

    # Seed DST with known content (the capture victim).
    board.track_drum_init(DST)
    for s in FP_STEPS:
        board.track_par_set(DST, 0, INSTR, s, (90 + s) & 0x7f)
    time.sleep(SETTLE)
    before = [board.track_par_get(DST, 0, INSTR, s) for s in FP_STEPS]

    # The live grab (re-sim path, K=1) — overwrites DST.
    assert board.capture_span(SRC, 1, DST) == 0x01, "capture-span (K=1) should commit"
    time.sleep(SETTLE)
    _, state, track, scope = board.track_undo_query()
    assert state == JRNL_UNDOABLE and track == DST and scope == JRNL_TRACK, (
        "the live CAPTURE gesture must arm the journal for the dst track (TRACK scope)"
    )

    # UNDO restores the pre-capture dst byte-exact (the grab is reversible).
    assert board.track_undo() == DST
    time.sleep(SETTLE)
    assert [board.track_par_get(DST, 0, INSTR, s) for s in FP_STEPS] == before, (
        "UNDO of a CAPTURE grab must restore the pre-capture dst byte-exact"
    )
