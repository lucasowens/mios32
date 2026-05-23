"""Bounce-in-place tests.

What we're verifying:
  1. Source-side preservation. After a bounce, every CC + the runtime
     play_section on the source track is byte-identical to its pre-bounce
     value. The implementation uses an in-RAM snapshot (seq_capture.c steps
     1 and 7) — these tests pin that contract.
  2. Destination-side sanitization. The dst pattern slot is written with
     generative state neutralized (SEQ_CC_ResetGenerativeForBounce in seq_cc.c).

How the tests fill the bounce ring:
  The per-track capture ring fills automatically whenever the track emits
  MIDI (the tap is in SEQ_CORE_ScheduleEvent). We arm Euclidean(16, 16) on
  track 0 — every step triggers — then PLAY for PLAY_SECONDS to populate
  ~1 measure of events, STOP, then bounce. Without the play burst the ring
  is empty and SEQ_CAPTURE_CommitToSlot returns -2.

Destructive: these tests overwrite (BOUNCE_BANK, BOUNCE_PATTERN) on the SD
card. The "dst sanitization" test additionally loads that slot into
group 1, overwriting whatever pattern was loaded in tracks 4..7. Pick a
session you don't mind perturbing, or skip these tests via -k.
"""

import time

import pytest

from harness import Button, CC, Encoder, MidiPort, Page

# Slot we overwrite on every bounce. Bank 0, pattern 63 (the last slot in
# bank A in MBSEQ's default layout). Keep this consistent across all tests
# in this file so a single restore is enough afterwards if the user wants.
BOUNCE_BANK = 0
BOUNCE_PATTERN = 63

# Group used to load the destination pattern when we want to inspect its
# CCs without disturbing source (group 0). Group 1 = tracks 4..7.
DST_VERIFY_GROUP = 1
DST_VERIFY_TRACK = 4  # group-1 / track-0

# Number of bars to play before bouncing. Default tempo is ~140 BPM, 16 steps
# per measure -> ~1.7s per measure. 2.5s gives us a comfortable >1 measure of
# events in the ring even with timing slop.
PLAY_SECONDS = 2.5


def _setup_track0_with_triggers(board):
    """Configure track 0 to USB1 and arm every step via Euclidean(16, 16).

    Mirrors the setup used in test_robotize.py so the bounce tests share the
    same "every step plays" baseline.
    """
    board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
    board.page_set(Page.TRKEUCLID)
    time.sleep(0.1)
    board.turn(Encoder.GP(12), -10)   # generator type -> EUCLID
    board.turn(Encoder.GP(9), -99)    # length -> 0
    board.turn(Encoder.GP(10), -99)   # pulses -> 0
    board.turn(Encoder.GP(9), +15)    # length = 16
    board.turn(Encoder.GP(10), +16)   # pulses = 16 (every step)
    time.sleep(0.15)


def _fill_ring_via_playback(board, seconds: float = PLAY_SECONDS) -> None:
    """PLAY for `seconds`, STOP. The per-track capture ring fills as a side
    effect of MIDI emission, so we don't need to do anything else.
    """
    board.press(Button.PLAY)
    try:
        time.sleep(seconds)
    finally:
        board.press(Button.STOP)
        time.sleep(0.2)  # let the last in-flight notes drain


@pytest.mark.hardware
def test_bounce_preserves_source_direction_and_steps(board):
    """Setting non-default direction shaping on source must survive the bounce.

    These are the fields the user observed getting reset before the in-RAM
    snapshot fix landed. If this test ever fails, the source-restore path
    has regressed.
    """
    _setup_track0_with_triggers(board)
    board.cc_set(track=0, cc=CC.DIRECTION, value=1)        # Backward
    board.cc_set(track=0, cc=CC.STEPS_REPLAY, value=3)
    board.cc_set(track=0, cc=CC.STEPS_FORWARD, value=2)
    board.cc_set(track=0, cc=CC.STEPS_JMPBCK, value=1)

    _fill_ring_via_playback(board)
    assert board.bounce(src_track=0, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN), (
        "bounce returned failure — ring was empty or PatternWrite failed"
    )

    assert board.cc_get(track=0, cc=CC.DIRECTION) == 1, "DIRECTION reset on source"
    assert board.cc_get(track=0, cc=CC.STEPS_REPLAY) == 3, "STEPS_REPLAY reset on source"
    assert board.cc_get(track=0, cc=CC.STEPS_FORWARD) == 2, "STEPS_FORWARD reset on source"
    assert board.cc_get(track=0, cc=CC.STEPS_JMPBCK) == 1, "STEPS_JMPBCK reset on source"


@pytest.mark.hardware
def test_bounce_preserves_source_generative_cc(board):
    """A broader sweep across generative CCs that the sanitize function touches.

    The sanitize function neutralizes ~30+ fields in the destination CC;
    the source must come back identical for every one of them. We don't
    enumerate the whole list (the canonical inventory is in
    SEQ_CC_ResetGenerativeForBounce) but check one representative from each
    major category.
    """
    _setup_track0_with_triggers(board)

    expected = {
        CC.TRANSPOSE_SEMI:      5,
        CC.TRANSPOSE_OCT:       2,
        CC.GROOVE_VALUE:        10,
        CC.HUMANIZE_VALUE:      8,
        CC.ECHO_REPEATS:        3,
        CC.LFO_AMPLITUDE:       64,
        CC.ROBOTIZE_ACTIVE:     1,
        CC.ROBOTIZE_LOOP_CYCLES: 4,
        CC.MORPH_MODE:          1,
        CC.STEPS_SKIP:          2,
        CC.STEPS_REPEAT:        1,
    }
    for cc, value in expected.items():
        board.cc_set(track=0, cc=cc, value=value)

    _fill_ring_via_playback(board)
    assert board.bounce(src_track=0, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN)

    for cc, value in expected.items():
        got = board.cc_get(track=0, cc=cc)
        assert got == value, (
            f"CC 0x{cc:02x} reset on source: expected {value}, got {got}"
        )


@pytest.mark.hardware
def test_bounce_preserves_source_play_section(board):
    """play_section lives in seq_core_trk_t (runtime state) and is NOT in the
    pattern file. The SD-based restore path can't bring it back; the in-RAM
    snapshot path can. If this fails, the play_section restore line in
    seq_capture.c was lost.
    """
    _setup_track0_with_triggers(board)
    board.play_section_set(track=0, value=2)  # section C

    _fill_ring_via_playback(board)
    assert board.bounce(src_track=0, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN)

    assert board.play_section_get(track=0) == 2, "play_section reset on source"

    # Cleanup: drop back to section A so subsequent tests start clean.
    board.play_section_set(track=0, value=0)


@pytest.mark.hardware
def test_bounce_destination_has_sanitized_generative_cc(board):
    """The destination pattern slot, when loaded fresh, should have generative
    state neutralized. Identity (MIDI port/channel) carries over from source.

    Loads the dst slot into group 1 to avoid clobbering source in group 0.
    Track 4 in group 1 corresponds to dst pattern's track 0.
    """
    _setup_track0_with_triggers(board)
    # Set the same generative state on source as the preservation test.
    board.cc_set(track=0, cc=CC.DIRECTION, value=1)       # Backward
    board.cc_set(track=0, cc=CC.TRANSPOSE_SEMI, value=5)
    board.cc_set(track=0, cc=CC.GROOVE_VALUE, value=10)
    board.cc_set(track=0, cc=CC.ECHO_REPEATS, value=3)
    board.cc_set(track=0, cc=CC.ROBOTIZE_ACTIVE, value=1)
    board.cc_set(track=0, cc=CC.HUMANIZE_VALUE, value=8)
    board.cc_set(track=0, cc=CC.STEPS_REPLAY, value=3)

    _fill_ring_via_playback(board)
    assert board.bounce(src_track=0, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN)

    # Load dst into group 1 (overwrites tracks 4..7).
    assert board.pattern_load(
        group=DST_VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    ), "PATTERN_LOAD failed"
    time.sleep(0.1)

    # Generative fields should all be neutralized.
    assert board.cc_get(DST_VERIFY_TRACK, CC.DIRECTION) == 0, "dst DIRECTION not Forward"
    assert board.cc_get(DST_VERIFY_TRACK, CC.TRANSPOSE_SEMI) == 0, "dst TRANSPOSE_SEMI not 0"
    assert board.cc_get(DST_VERIFY_TRACK, CC.GROOVE_VALUE) == 0, "dst GROOVE_VALUE not 0"
    assert board.cc_get(DST_VERIFY_TRACK, CC.ECHO_REPEATS) == 0, "dst ECHO_REPEATS not 0"
    assert board.cc_get(DST_VERIFY_TRACK, CC.ROBOTIZE_ACTIVE) == 0, "dst ROBOTIZE_ACTIVE not 0"
    assert board.cc_get(DST_VERIFY_TRACK, CC.HUMANIZE_VALUE) == 0, "dst HUMANIZE_VALUE not 0"
    assert board.cc_get(DST_VERIFY_TRACK, CC.STEPS_REPLAY) == 0, "dst STEPS_REPLAY not 0"

    # Identity should carry over from source. We configured track 0 to USB0
    # at the start; track 4 (group 1's track 0) should now match.
    assert board.cc_get(DST_VERIFY_TRACK, CC.MIDI_PORT) == MidiPort.USB0, (
        "dst MIDI_PORT did not inherit from source"
    )
    assert board.cc_get(DST_VERIFY_TRACK, CC.MIDI_CHANNEL) == 0, (
        "dst MIDI_CHANNEL did not inherit from source"
    )


@pytest.mark.hardware
def test_bounce_destination_plays_captured_notes(board):
    """End-to-end: bounce a known pattern, load the destination, play it, and
    verify the destination emits notes. Doesn't verify *which* notes — that's
    sensitive to the capture window alignment — just that the bounce produced
    a playable pattern (vs. an empty one).
    """
    _setup_track0_with_triggers(board)

    _fill_ring_via_playback(board)
    assert board.bounce(src_track=0, dst_bank=BOUNCE_BANK, dst_pattern=BOUNCE_PATTERN)

    assert board.pattern_load(
        group=DST_VERIFY_GROUP, bank=BOUNCE_BANK, pattern=BOUNCE_PATTERN
    )
    # Make sure the bounced track on group 1 emits on a port we can hear.
    # The bounce should already have inherited USB0 from source, but the
    # captured velocity/note layers may be empty if RecEvent picked the
    # wrong layer link — force-routing here is belt and suspenders.
    board.track_config(track=DST_VERIFY_TRACK, midi_port=MidiPort.USB0, channel=0)

    # Mute source (group 0) so only the bounced track plays during capture.
    # The harness has no direct mute helper, but pressing MUTE then GP1 in
    # the MUTE page toggles track 0's mute.
    board.page_set(Page.MUTE)
    time.sleep(0.05)
    board.press(Button.GP(1))   # mute track 0
    time.sleep(0.05)
    try:
        capture_t0 = board.capture_start()
        board.press(Button.PLAY)
        time.sleep(PLAY_SECONDS)
        board.press(Button.STOP)
        time.sleep(0.2)
        notes = [
            e for e in board.capture_notes(since=capture_t0)
            if e.is_on and e.channel == 0
        ]
        assert len(notes) > 0, (
            f"bounced pattern produced no notes on track {DST_VERIFY_TRACK} — "
            f"the capture window may have missed the active measure"
        )
    finally:
        # Unmute track 0 (board fixture also does this on next test).
        board.page_set(Page.MUTE)
        time.sleep(0.05)
        board.press(Button.GP(1))
