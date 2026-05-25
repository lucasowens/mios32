"""Diagnostic: dump the MSP/handler-stack high-water snapshot from the running
firmware (phase D.0 measurement). Read once, exercise the sequencer manually
(play, edit, browse pages, load patterns), then read again — the high-water
number only ever grows.

Usage:
    cd tests && .venv/bin/python diag_msp.py
"""

from harness import Board


def fmt(d: dict) -> str:
    return (
        f"  peak MSP usage:       {d['peak_usage_bytes']:>6} B  "
        f"(= initial {d['paint_initial_depth']:>4} + grew {d['high_water_bytes']:>5})\n"
        f"  painted region:       {d['paint_extent_bytes']:>6} B  "
        f"[lo=0x{d['paint_lo']:08x}  hi=0x{d['paint_hi']:08x}]\n"
    )


def main() -> None:
    with Board() as board:
        print("PING:", board.ping())
        snap = board.msp_query()
        print("\nMSP snapshot:\n" + fmt(snap))
        print(
            "Exercise the SEQ manually (PLAY, encoders, page changes, pattern\n"
            "loads, etc.) for a minute or two, then press Enter for a re-read.\n"
        )
        input(">>> press Enter to re-read MSP <<< ")
        snap2 = board.msp_query()
        print("\nMSP snapshot (after exercise):\n" + fmt(snap2))
        delta = snap2["peak_usage_bytes"] - snap["peak_usage_bytes"]
        print(f"  delta peak usage:     {delta:+} B")
        free = snap2["paint_extent_bytes"] - snap2["high_water_bytes"]
        print(
            f"  unused painted tail:  {free:>6} B  "
            "(remaining headroom in painted region)\n"
        )


if __name__ == "__main__":
    main()
