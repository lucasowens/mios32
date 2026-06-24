"""Capture-while-performing must not freeze the sequencer clock (2026-06-22).

Capturing a whole-organism phrase WHILE PLAYING is fundamental to this instrument,
and a phrase capture (4 group-record SD writes) was observed to freeze the clock
~1.16s — a showstopper live. This pin measures the freeze on-device so a fix can be
proven (test goes RED now, GREEN after the fix), not hand-waved.

WHY NOT just time bpm_tick: bpm_tick is incremented in the HIGHEST-priority HW-timer
ISR (SEQ_BPM_Timer_Master), so SEQ_BPM_TickGet() keeps counting straight through a
clock-task stall — it cannot detect the freeze. The freeze is the +4 emission task
(SEQ_CORE_Handler) being starved while a +3 task runs the SD busy-wait. So the firmware
measures the gap between emission-task services (CMD_CAPTURE_PERF / SEQ_CORE_ServiceMaxGapGet):
  freeze_fraction = max_gap / wall_ticks
    ~1.0  -> the clock was DEAD for essentially the whole capture (the showstopper)
    ~0.0  -> the emission task kept running throughout (the goal)

The probe fires the capture on the firmware's +3 SysEx task — the SAME priority as the
physical UTILITY capture gesture — so it faithfully reproduces the live freeze.

Runs in real time (the capture itself is ~1s), so it starts the real transport and waits
for the clock to actually advance before probing.
"""

import time

import pytest

# A healthy clock services emission every tick or two; a full freeze makes the gap == the
# whole capture. Anything past this fraction means the clock measurably stalled. Generous
# margin: post-fix is expected near 0, current firmware near 1.0.
MAX_FREEZE_FRACTION = 0.20

# The +2 UI task (LED/LCD/button scan) is BELOW the +3 capture, so a non-yielding SD write
# starves it for the whole capture (ui_freeze ~1.0 = the ~640 ms control-surface hang). Once
# the SD completion-poll yields the CPU, the UI task runs during the card-program waits and
# this collapses toward ~0. The +2 task is scheduled less crisply than +4 emission, so a more
# generous threshold than MAX_FREEZE_FRACTION — still a vast margin between ~1.0 and ~0.01.
MAX_UI_FREEZE_FRACTION = 0.30

PHRASE_SLOT = 1  # MBSEQ_PH.V4 sentinel file — disposable, never an AUTOTEST bank


def _wait_clock_advancing(board, min_ticks: int = 48, timeout: float = 10.0) -> int:
    """Wait until the running transport's bpm_tick has advanced by >= min_ticks from the
    first reading (proves the clock is genuinely ticking before we probe). Returns the
    tick delta observed; raises on timeout."""
    deadline = time.monotonic() + timeout
    t0 = board.tick_query()["bpm_tick"]
    while time.monotonic() < deadline:
        delta = board.tick_query()["bpm_tick"] - t0
        if delta >= min_ticks:
            return delta
        time.sleep(0.1)
    raise AssertionError("transport not advancing bpm_tick — clock never started?")


@pytest.mark.hardware
@pytest.mark.timeout(30)  # capture is ~1s+ and on a frozen clock the round-trip is slow
def test_capture_while_playing_does_not_freeze_clock(board):
    """Whole-organism phrase capture, fired WHILE the transport runs, must not starve the
    emission task. Measures the peak emission-service gap during the capture vs the capture
    duration. RED on current firmware (freeze ~= the whole capture); GREEN after the SD
    write learns to yield."""
    board.reset()
    try:
        assert board.transport(start=True) or True  # start may report running on the next tick
        _wait_clock_advancing(board)

        wall_secs_0 = time.monotonic()
        r = board.capture_perf(PHRASE_SLOT)
        wall_secs = time.monotonic() - wall_secs_0
    finally:
        board.transport(start=False)

    assert r["ok"], f"phrase capture failed: {r}"
    assert r["running"], f"transport not running at probe time — gap is meaningless: {r}"
    assert r["wall_ticks"] > 0, f"no clock advance measured across the capture: {r}"

    diag = (
        f"capture-while-playing freeze probe:\n"
        f"  capture wall time   = {wall_secs*1000:.0f} ms (python round-trip)\n"
        f"  wall_ticks          = {r['wall_ticks']} ISR ticks (capture duration, clock keeps counting)\n"
        f"  max_gap             = {r['max_gap']} ISR ticks (peak emission-task starvation)\n"
        f"  freeze_fraction     = {r['freeze_fraction']:.3f}  (max_gap / wall_ticks)\n"
        f"  threshold           = {MAX_FREEZE_FRACTION}"
    )
    print("\n" + diag)

    assert r["freeze_fraction"] <= MAX_FREEZE_FRACTION, (
        "clock froze during capture-while-performing — emission task was starved for "
        f"{r['freeze_fraction']*100:.0f}% of the capture.\n" + diag
    )


@pytest.mark.hardware
@pytest.mark.timeout(30)
def test_capture_while_playing_keeps_control_surface_live(board):
    """The control surface (LEDs/LCD/buttons = the +2 UI task) must stay live during a
    phrase capture, not just the audible clock. The capture runs at +3; without a yield in
    the SD completion poll it starves the lower-prio +2 task for the whole write (~640 ms
    of dead LEDs/buttons). RED on a non-yielding firmware (ui_freeze ~= the whole capture);
    GREEN once MIOS32_SDCARD_WAIT_HOOK lets the busy-wait sleep so the UI task runs."""
    board.reset()
    try:
        assert board.transport(start=True) or True
        _wait_clock_advancing(board)
        r = board.capture_perf(PHRASE_SLOT)
    finally:
        board.transport(start=False)

    assert r["ok"], f"phrase capture failed: {r}"
    assert r["running"], f"transport not running at probe time — gap is meaningless: {r}"
    assert r["wall_ticks"] > 0, f"no clock advance measured across the capture: {r}"

    diag = (
        f"capture-while-playing control-surface probe:\n"
        f"  wall_ticks          = {r['wall_ticks']} ISR ticks (capture duration)\n"
        f"  ui_gap              = {r['ui_gap']} ISR ticks (peak +2 UI-task starvation)\n"
        f"  ui_freeze_fraction  = {r['ui_freeze_fraction']:.3f}  (ui_gap / wall_ticks)\n"
        f"  threshold           = {MAX_UI_FREEZE_FRACTION}"
    )
    print("\n" + diag)

    assert r["ui_freeze_fraction"] <= MAX_UI_FREEZE_FRACTION, (
        "control surface hung during capture-while-performing — the +2 UI task was starved "
        f"for {r['ui_freeze_fraction']*100:.0f}% of the capture (dead LEDs/LCD/buttons).\n" + diag
    )
