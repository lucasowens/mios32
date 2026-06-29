"""Diagnostic: the PASSIVE pattern-switch freeze probe — measure-first for the save/switch
hiccup (residual audible + visual catch on a running cross-group / dirty switch).

It does NOT change the engine: it only reads the existing +4 emission and +2 UI service-gap
probes across a window you open, plus the forward-delay margin subsystem state. So it measures
the freeze of WHATEVER switch lands in the window:

  * MANUAL (default)  — you switch patterns on the unit via GP buttons while it clocks a slave;
                        this is the faithful by-ear test (your real trigger). LISTEN, then read.
  * AUTO (--auto)     — the harness marks the outgoing group dirty, requests a synched switch
                        (CMD_PATTERN_CHANGE) while playing, waits out the boundary, and reads.
                        Repeatable number, no hands on the box.

What the numbers mean:
  max_gap  = peak +4 EMISSION service gap, in ISR ticks = the AUDIBLE freeze (clock/notes).
             ~290ms write ≈ ~260 ticks @140BPM/384ppqn; a few ticks ≈ inaudible.
  ui_gap   = peak +2 UI service gap = the visual (LED/LCD/button) freeze.
  measured_ms = the stale/65.5ms-saturating stopwatch value the margin is sized from (Amplifier 1).
  pre_ticks   = pre-fire window, clamped to 95/96 of a beat — cannot cover a ~290ms write (Amplifier 2).
  margin_ms / grid16ths = effective forward-delay margin and floor-clamped switch grid.

Usage:
    cd tests && .venv/bin/python diag_switch.py            # manual / by-ear
    cd tests && .venv/bin/python diag_switch.py --auto      # harness-driven, repeatable
    cd tests && .venv/bin/python diag_switch.py --auto --group 0 --bank 0 --pattern 1 --grid 5
"""

import argparse
import time

from harness import Board

PPQN = 384
REF_BPMS = (140, 160, 180)
TICKS_PER_BAR = 4 * PPQN  # 4/4


def gap_ms(ticks: int, bpm: int) -> float:
    # one ISR tick = 60000 / (bpm * PPQN) ms
    return ticks * 60_000.0 / (bpm * PPQN)


def fmt(d: dict) -> str:
    gap = d["max_gap"]
    ui = d["ui_gap"]
    lines = [
        f"  window length              : {d['wall_ticks']:>6} ISR ticks",
        f"  peak EMISSION (+4) gap     : {gap:>6} ISR ticks   <- the AUDIBLE freeze (clock/notes)",
        f"  emission freeze fraction   : {d['freeze_fraction']*100:>6.1f} %   (gap / window; ~0 = clock stayed alive)",
        f"  peak UI (+2) gap           : {ui:>6} ISR ticks   <- the VISUAL freeze (LED/LCD/buttons)",
        f"  ui freeze fraction         : {d['ui_freeze_fraction']*100:>6.1f} %",
        "",
        "  forward-delay margin subsystem (why the stall isn't hidden):",
        f"    measured_ms (Amp 1)      : {d['measured_ms']:>6}   <- stopwatch saturates at 65; 65 = pinned/garbage",
        f"    margin_ms                : {d['margin_ms']:>6}",
        f"    pre_ticks (Amp 2)        : {d['pre_ticks']:>6}   <- clamped <=95; pre-fire window cap",
        f"    grid16ths (eff. grid)    : {d['grid16ths']:>6}   <- floor-clamped switch-quantize grid",
        "",
        "  what the emission freeze sounds like, at reference tempos:",
    ]
    for bpm in REF_BPMS:
        lines.append(
            f"    {bpm:>3} BPM: emission froze ~{gap_ms(gap, bpm):>6.0f} ms"
            f"   (ui froze ~{gap_ms(ui, bpm):>6.0f} ms)"
        )
    # plain-language verdict
    if gap <= 2:
        verdict = "emission stayed on its tick — the +4 path is NOT the residual (look elsewhere)."
    elif gap_ms(gap, 140) >= 100:
        verdict = ("CONFIRMED: emission was starved for a write-sized window. The +4 critical-"
                   "section root is real — the interrupts-ON Handler + drift-gate pair is licensed.")
    else:
        verdict = ("partial stall — emission slipped but less than a full write; size the fix "
                   "against this and re-run after each change.")
    lines += ["", f"  >>> {verdict}"]
    return "\n".join(lines) + "\n"


def auto_window(board: Board, group: int, bank: int, pattern: int, grid: int) -> dict:
    """Harness-driven: dirty the outgoing group, request a synched switch while playing,
    wait out the boundary, read the probe."""
    g_idx, g_meas = board.switch_quantize_get()
    if grid is not None and g_idx != grid:
        board.switch_quantize_set(grid)
        print(f"  set switch-quantize grid -> {grid}")
    if not board.tick_query()["running"]:
        print("  starting transport...")
        board.transport(True)
        time.sleep(0.5)

    board.dirty_set(group, True)  # force the FEARLESS writeback on switch-away
    start_tick = board.tick_query()["bpm_tick"]
    board.switch_perf_arm()
    board.pattern_change(group, bank, pattern)  # queues a synched (deferred) switch
    # wait until the boundary fired + the +4 I/O completed (>= 2 bars of advance)
    deadline = time.monotonic() + 12.0
    while time.monotonic() < deadline:
        if board.tick_query()["bpm_tick"] - start_tick >= 2 * TICKS_PER_BAR:
            break
        time.sleep(0.05)
    return board.switch_perf_read()


def main() -> None:
    ap = argparse.ArgumentParser(description="passive pattern-switch freeze probe")
    ap.add_argument("--auto", action="store_true", help="harness-driven repeatable switch")
    ap.add_argument("--group", type=int, default=0, help="group to switch (0-3)")
    ap.add_argument("--bank", type=int, default=0, help="target bank (0-7)")
    ap.add_argument("--pattern", type=int, default=1, help="target pattern (must differ from current)")
    ap.add_argument("--grid", type=int, default=5, help="switch-quantize grid index (5 = 1 bar)")
    args = ap.parse_args()

    with Board() as board:
        print("PING:", board.ping())

        if args.auto:
            print(
                f"\nAUTO: dirty G{args.group+1}, request synched switch -> bank {args.bank} "
                f"pattern {args.pattern+1}, measure the +4/+2 freeze.\n"
                "Run again with the same args after each fix to watch the gap shrink. Ctrl-C to quit.\n"
            )
            n = 0
            while True:
                try:
                    input(f">>> auto switch {n + 1}: press Enter to run <<< ")
                    r = auto_window(board, args.group, args.bank, args.pattern, args.grid)
                except (EOFError, KeyboardInterrupt):
                    print("\nbye")
                    return
                if not r["running"]:
                    print("  (transport stopped — numbers meaningless)\n")
                    continue
                print("\nswitch freeze window (auto):\n" + fmt(r))
                n += 1
            return

        # manual / by-ear (gesture-agnostic: the probe measures WHATEVER you do in the window —
        # a pattern switch, a capture-while-playing, a save, etc.)
        print(
            "\nMANUAL / by-ear. Clock a hardware slave from this unit, PLAY at gig tempo, then for\n"
            "each window: press Enter to ARM, perform the GESTURE you're testing on the unit\n"
            "(switch / CAPTURE-while-playing / save) — LISTEN for the clock lurch / note bunching —\n"
            "then press Enter to READ. Ctrl-C to quit.\n"
        )
        if not board.tick_query()["running"]:
            print("  Transport is STOPPED — start PLAY on the device first (the gap only accrues while running).\n")
        n = 0
        while True:
            try:
                input(f">>> window {n + 1}: press Enter to ARM <<< ")
                running = board.switch_perf_arm()
                if not running:
                    print("  (transport stopped — start PLAY, then retry)\n")
                    continue
                input(">>> now perform the gesture (switch / CAPTURE / save), LISTEN, then Enter to READ <<< ")
                r = board.switch_perf_read()
            except (EOFError, KeyboardInterrupt):
                print("\nbye")
                return
            print("\nswitch freeze window (manual):\n" + fmt(r))
            n += 1


if __name__ == "__main__":
    main()
