"""Diagnostic: the all-16 force-dirty render-cost probe, read live while you play
(play-readiness #5). chord_mask / tension(GRAVITY) / live-pitch slots force a full
per-track re-render every tick (memcpy 1024+256 + a per-step processor sweep), bypassing
the sweep/quiet fast path — and that render runs in the +4 emission task's tick prologue.
This reads how heavy it actually gets on YOUR patterns, so the question "can the box drive
N live-input processors at gig tempo without the clock slipping?" is answered by ear + number
before #6 (trigger generators) is designed around the leftover CPU.

Usage:
    cd tests && .venv/bin/python diag_render.py

Then, on the device: load your set, engage GRAVITY/GRIP (or chord_mask) on as many tracks as
you perform with, set your gig tempo, and PLAY. Each Enter resets the probe, opens a window
while you play, and prints the peak render cost + whether emission stayed on its tick.
"""

from harness import Board

PPQN = 384
REF_BPMS = (140, 160, 180)


def fmt(d: dict) -> str:
    lines = [
        f"  tracks force-dirty (last tick) : {d['last_dirty']:>3}   "
        "(how many tracks re-rendered every tick — your live-input processor count)",
        f"  render passes in window        : {d['tick_count']:>6}",
        f"  peak ONE-TICK render (all dirty): {d['max_tick_us']:>6} us   <- competes with emission for the tick",
        f"  peak single-track render       : {d['max_track_us']:>6} us",
        f"  render duty cycle              : {d['duty']*100:>6.2f} %   (render CPU fraction over the window)",
        f"  window length                  : {d['elapsed_ms']:>6} ms",
        f"  peak emission (+4) gap         : {d['emission_gap']:>6} ISR ticks   "
        "(~1 = clock kept up; large = render starved the audio)",
        f"  peak UI (+2) gap               : {d['ui_gap']:>6} ISR ticks   "
        "(~1 = control surface alive; LARGE = UI going dark <-- the on-device freeze)",
        "",
        "  one-tick render vs the tick budget (headroom = how much of a tick render eats):",
    ]
    for bpm in REF_BPMS:
        period = 60_000_000 / (bpm * PPQN)
        frac = (d["max_tick_us"] / period) if period else 0.0
        flag = "  <-- TIGHT" if frac > 0.5 else ""
        lines.append(
            f"    {bpm:>3} BPM: tick={period:>5.0f} us  render={frac*100:>5.1f}% of the tick{flag}"
        )
    return "\n".join(lines) + "\n"


def main() -> None:
    with Board() as board:
        print("PING:", board.ping())
        if not board.tick_query()["running"]:
            print(
                "\nTransport is STOPPED. The probe only accumulates while the clock runs —\n"
                "start PLAY on the device (and engage your processors) before taking a window.\n"
            )
        print(
            "\nEngage GRAVITY/GRIP (or chord_mask) on your performing tracks and PLAY at your\n"
            "gig tempo. Each window: press Enter to reset+start, play for a few seconds, then\n"
            "press Enter again to read. Ctrl-C to quit.\n"
        )
        n = 0
        while True:
            try:
                input(f">>> window {n + 1}: press Enter to RESET + open the window <<< ")
                board.render_perf_reset()
                input(">>> play for a few seconds, then press Enter to READ <<< ")
                r = board.render_perf()
            except (EOFError, KeyboardInterrupt):
                print("\nbye")
                return
            if not r["running"]:
                print("  (transport was stopped — numbers are meaningless; start PLAY and retry)\n")
                continue
            print("\nrender-cost window:\n" + fmt(r))
            n += 1


if __name__ == "__main__":
    main()
