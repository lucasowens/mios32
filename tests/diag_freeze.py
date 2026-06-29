"""Diagnostic: SD-GESTURE FREEZE PROFILER — one pass that ranks every SD-writing gesture by
how long it actually starves the +4 emission task (audible clock/notes) and the +2 UI task
(LED/LCD/buttons), WHILE PLAYING. Answers "which gesture is the real hiccup?" before we commit
to a fix.

It reuses the passive CMD_SWITCH_PERF probe (arm → trigger → settle → read), which is gesture-
agnostic, so the same instrument measures a pattern switch, a save, a slot/track capture, a
phrase (whole-organism) capture, and a checkpoint. Each gesture is run --repeats times and the
MEDIAN freeze is reported (robust to the occasional once-per-second background SD check landing
in a window).

  emission gap = peak +4 service gap = the AUDIBLE freeze (clock/notes).
  ui gap       = peak +2 service gap = the VISUAL freeze (LED/LCD/buttons).
  ~290ms write ≈ ~260 ISR ticks @140BPM/384ppqn; ~1.16s (4-group) ≈ ~1040 ticks.

!!! WRITES TO SD on the ACTIVE session (save/capture/checkpoint overwrite the dst slots and the
    anchor). Run on a SCRATCH session, not your gig set (device SD is disposable; keep AUTOTEST).

Usage:
    cd tests && .venv/bin/python diag_freeze.py
    cd tests && .venv/bin/python diag_freeze.py --bpm 174 --repeats 5 --dst-bank 7 --dst-pattern 7
"""

import argparse
import statistics
import time

from harness import Board

PPQN = 384
TICKS_PER_BAR = 4 * PPQN


def gap_ms(ticks: int, bpm: int) -> float:
    return ticks * 60_000.0 / (bpm * PPQN)


def measure(board: Board, trigger, settle: float, repeats: int):
    """Run trigger() `repeats` times, each wrapped in arm/read. Returns (samples, err) where
    samples is a list of (emission_gap, ui_gap, ok)."""
    samples = []
    for _ in range(repeats):
        board.switch_perf_arm()
        try:
            ok = trigger()
        except Exception as e:  # refusal / out-of-range / timeout — record, don't abort the pass
            return None, str(e)
        time.sleep(settle)
        r = board.switch_perf_read()
        samples.append((r["max_gap"], r["ui_gap"], ok))
    return samples, None


def main() -> None:
    ap = argparse.ArgumentParser(description="SD-gesture freeze profiler")
    ap.add_argument("--bpm", type=int, default=140, help="tempo for the tick→ms conversion")
    ap.add_argument("--repeats", type=int, default=3, help="runs per gesture (median reported)")
    ap.add_argument("--settle", type=float, default=0.8, help="seconds to wait after each trigger")
    ap.add_argument("--group", type=int, default=0, help="group for switch/save (0-3)")
    ap.add_argument("--bank", type=int, default=0, help="bank for the switch source slots (0-7)")
    ap.add_argument("--pat-a", type=int, default=0, help="switch toggles between pat-a and pat-b")
    ap.add_argument("--pat-b", type=int, default=1, help="(both must exist with content)")
    ap.add_argument("--src-track", type=int, default=0, help="capture source track (0-15)")
    ap.add_argument("--dst-track", type=int, default=0, help="capture dest track in the slot (0-15)")
    ap.add_argument("--dst-bank", type=int, default=7, help="dest bank for save/capture (scratch)")
    ap.add_argument("--dst-pattern", type=int, default=7, help="dest pattern for save/capture (scratch)")
    ap.add_argument("--phrase", type=int, default=0, help="phrase slot for phrase capture")
    ap.add_argument("--cap-k", type=int, default=4, help="live-span capture length in loops (stress: crank it)")
    args = ap.parse_args()

    with Board() as board:
        print("PING:", board.ping())
        if not board.tick_query()["running"]:
            print("  transport stopped — starting PLAY (the gap only accrues while running)...")
            board.transport(True)
            time.sleep(0.6)
        if not board.tick_query()["running"]:
            print("  !! could not start transport; aborting (start PLAY on the device).")
            return
        # let the recorder/ring accrue a couple bars for the live-span capture
        time.sleep(1.0)
        print(
            f"\nProfiling SD gestures @ {args.bpm} BPM ref, {args.repeats}x each (median).\n"
            "WRITES to SD on the active session — scratch session recommended.\n"
        )

        sw = {"cur": args.pat_a}

        def switch(dirty: bool) -> bool:
            nxt = args.pat_b if sw["cur"] == args.pat_a else args.pat_a
            if dirty:
                board.dirty_set(args.group, True)
            ok = board.pattern_change(args.group, args.bank, nxt)
            sw["cur"] = nxt
            return ok

        gestures = [
            ("switch (clean, read-only)", lambda: switch(False)),
            ("switch (dirty, +writeback)", lambda: switch(True)),
            ("save (1 group)", lambda: board.pattern_save(args.group, args.dst_bank, args.dst_pattern)),
            ("slot capture (static)", lambda: board.capture_to_slot_track(
                args.src_track, args.dst_track, args.dst_bank, args.dst_pattern, k=0)),
            (f"slot capture (live span k={args.cap_k})", lambda: board.capture_to_slot_track(
                args.src_track, args.dst_track, args.dst_bank, args.dst_pattern, k=args.cap_k)),
            ("phrase capture (4 groups)", lambda: board.phrase_capture(args.phrase)),
            ("checkpoint (anchor, 4 grp)", lambda: board.checkpoint()),
        ]

        rows = []
        for name, trig in gestures:
            print(f"  measuring: {name} ...", flush=True)
            samples, err = measure(board, trig, args.settle, args.repeats)
            if err is not None:
                rows.append((name, None, None, False, err))
                print(f"      -> skipped: {err}")
                continue
            em = statistics.median(s[0] for s in samples)
            ui = statistics.median(s[1] for s in samples)
            ok = all(s[2] for s in samples)
            rows.append((name, em, ui, ok, None))

        # ranked table, worst audible freeze first
        rows_done = [r for r in rows if r[1] is not None]
        rows_skip = [r for r in rows if r[1] is None]
        rows_done.sort(key=lambda r: r[1], reverse=True)

        print("\n" + "=" * 78)
        print(f"  SD-GESTURE FREEZE RANKING (median of {args.repeats}, worst audible first, @ {args.bpm} BPM)")
        print("=" * 78)
        print(f"  {'gesture':<28} {'EMISSION (+4)':>16} {'UI (+2)':>16}")
        print(f"  {'(audible / visible)':<28} {'ticks / ms':>16} {'ticks / ms':>16}")
        print("  " + "-" * 74)
        for name, em, ui, ok, _ in rows_done:
            flag = "" if ok else "  (gesture reported NOT-ok)"
            print(
                f"  {name:<28} {em:>6} /{gap_ms(em, args.bpm):>6.0f}ms "
                f"{ui:>6} /{gap_ms(ui, args.bpm):>6.0f}ms{flag}"
            )
        for name, *_rest, err in rows_skip:
            print(f"  {name:<28} {'— skipped —':>16}   ({err})")
        print("  " + "-" * 74)

        worst = rows_done[0] if rows_done else None
        if worst:
            wn, wem = worst[0], worst[1]
            print(
                f"\n  >>> Worst audible freeze: '{wn}' ~{gap_ms(wem, args.bpm):.0f}ms "
                f"({wem} ticks).\n"
                "      Anything >~30-40ms is a perceptible clock stumble for a slaved device.\n"
                "      The fix family (interrupts-ON Handler / move the SD write off the live task /\n"
                "      async writeback) targets exactly these — start with whichever tops this chart,\n"
                "      then confirm by ear and re-run to watch it shrink."
            )
        print()


if __name__ == "__main__":
    main()
