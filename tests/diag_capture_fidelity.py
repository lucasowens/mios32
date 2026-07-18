"""Diagnostic: the capture-fidelity DIFF — a deterministic replacement for by-ear.

Snapshots a source track (CCs + par-lane assignments + lane contents + gates),
performs a capture to a scratch dst, snapshots the dst, and prints a
what-travelled / what-reset table. Run it mid-jam whenever a captured copy
"sounds different" — the table says concretely WHICH dial or lane didn't make
the trip, and whether that matches the two-family law (2026-07-18 cont. 7):

  FLATTEN (stopped grab / capture_to_track): deterministic emission shapers
  (echo/LFO/direction/Delay/Roll/Nth lanes) should be PRESERVED.
  TAPE (while-playing grab): the same shapers should read RESET (they are baked
  into the recorded notes); CC/PB/PC/AT lanes should still travel (mirror copy).

Usage (device flashed + connected; transport state picks the engine):
    cd tests && .venv/bin/python diag_capture_fidelity.py [src] [dst] [k]

Defaults: src = current visible track, dst = 2, k = 1. The dst track is
OVERWRITTEN (RAM only — pattern on disk untouched; UNDO on the box restores).
Paste the output into the session log for analysis.
"""

import sys
import time

from harness import Board, CC

# The CCs the law splits on (name -> cc). Grouped for the report.
SHAPER_CCS = {
    "echo.repeats": 0x70, "echo.delay": 0x71, "echo.velocity": 0x72,
    "echo.fb_vel": 0x73, "echo.fb_note": 0x74, "echo.fb_gate": 0x75, "echo.fb_ticks": 0x76,
    "lfo.waveform": 0x30, "lfo.amplitude": 0x31, "lfo.phase": 0x32,
    "lfo.steps": 0x33, "lfo.steps_rst": 0x34, "lfo.flags": 0x35,
    "lfo.cc": 0x36, "lfo.cc_offset": 0x37, "lfo.cc_ppqn": 0x38,
    "dir.mode": 0x48, "dir.steps_replay": 0x49, "dir.steps_fwd": 0x4A,
    "dir.steps_jmpbck": 0x4B, "dir.steps_repeat": 0x5C, "dir.steps_skip": 0x5D,
    "groove.value": 0x52, "groove.style": 0x53,
    "mode.playmode": 0x40, "mode.flags": 0x41,
    "transpose.semi": 0x50, "transpose.oct": 0x51,
    "limit.lower": 0x43, "limit.upper": 0x44,
    "humanize.value": 0x56, "humanize.mode": 0x57,
    "morph.mode": 0x54,
    "robotize.active": 0x82, "robotize.prob": 0x83,
    "chordmask.strength": 0x96, "tension.grip": 0x9A,
    "arp.mode": 0x9D, "arp.bus": 0x9E,
    "voice.spread": 0x9F, "voice.inv": 0xA0, "voice.strum": 0xA1,
    "slice.grid": 0xA4, "slice.seed": 0xA5, "slice.strength": 0xA6,
}

PAR_TYPE_NAMES = {
    0: "None", 1: "Note", 2: "Chord1", 3: "Velocity", 4: "Length", 5: "CC",
    6: "PitchBend", 7: "Probability", 8: "Delay", 9: "Roll", 10: "Roll2",
    11: "ProgChange", 12: "Nth1", 13: "Nth2", 14: "Chord2", 15: "Aftertouch",
    16: "Root", 17: "Scale", 18: "Chord3", 19: "Ctrl", 20: "Waypoint",
    21: "VSprd", 22: "VInv", 23: "VStrm", 24: "VTilt", 25: "SliceOrd",
}

EVENT_MODES = {0: "Note", 1: "Chord", 2: "CC", 3: "Drum", 4: "Combined"}


def snapshot(board: Board, track: int) -> dict:
    snap = {"ccs": {}, "lanes": [], "gates": []}
    for name, cc in SHAPER_CCS.items():
        try:
            snap["ccs"][name] = board.cc_get(track, cc)
        except Exception:
            snap["ccs"][name] = None
    snap["event_mode"] = board.cc_get(track, CC.EVENT_MODE)
    is_drum = snap["event_mode"] == 3
    n_layers = 4 if is_drum else 16
    n_instr = 16 if is_drum else 1
    length = board.cc_get(track, 0x4D) + 1
    steps = min(length, 16)
    snap["length"] = length
    for layer in range(n_layers):
        asg = board.cc_get(track, (0x58 if is_drum else 0x00) + layer)
        if asg == 0:
            continue
        vals = []
        for instr in range(min(n_instr, 4)):  # first 4 drums keep the dump readable
            try:
                vals.append([board.track_par_get(track, layer, instr, s) for s in range(steps)])
            except Exception:
                vals.append(None)
        snap["lanes"].append((layer, asg, vals))
    for instr in range(min(n_instr, 4)):
        try:
            b, _ = board.trg_byte_get(track, 0, instr, 0, 2)
            snap["gates"].append((instr, b[0], b[1]))
        except Exception:
            pass
    return snap


def report(src_s: dict, dst_s: dict) -> None:
    print(f"\n  event_mode src={EVENT_MODES.get(src_s['event_mode'])} "
          f"dst={EVENT_MODES.get(dst_s['event_mode'])}   "
          f"length src={src_s['length']} dst={dst_s['length']}")

    print("\n  -- shaper CCs (src -> dst) --")
    preserved, reset, changed = [], [], []
    for name, sv in sorted(src_s["ccs"].items()):
        dv = dst_s["ccs"].get(name)
        if sv is None or dv is None:
            continue
        if sv == dv:
            if sv != 0:
                preserved.append(f"{name}={sv}")
        elif dv == 0 or (name == "voice.strum" and dv == 64):
            if sv != 0:
                reset.append(f"{name} {sv}->0")
        else:
            changed.append(f"{name} {sv}->{dv}")
    print("  PRESERVED :", ", ".join(preserved) or "(none armed)")
    print("  RESET     :", ", ".join(reset) or "(none)")
    print("  CHANGED   :", ", ".join(changed) or "(none)")

    print("\n  -- par lanes (assignment: first-16-step values, instr 0..3) --")
    src_lanes = {l: (a, v) for l, a, v in src_s["lanes"]}
    dst_lanes = {l: (a, v) for l, a, v in dst_s["lanes"]}
    for layer in sorted(set(src_lanes) | set(dst_lanes)):
        sa, sv = src_lanes.get(layer, (0, None))
        da, dv = dst_lanes.get(layer, (0, None))
        mark = "==" if (sa == da and sv == dv) else "!="
        print(f"  L{layer} src={PAR_TYPE_NAMES.get(sa, sa):<10} dst={PAR_TYPE_NAMES.get(da, da):<10} {mark}")
        if mark == "!=" and sv and dv:
            for i, (srow, drow) in enumerate(zip(sv, dv)):
                if srow != drow:
                    print(f"      instr {i} src {srow}")
                    print(f"      instr {i} dst {drow}")

    print("\n  -- gates (trg layer 0, bytes 0-1 per instr) --")
    for (si, s0, s1), (di, d0, d1) in zip(src_s["gates"], dst_s["gates"]):
        mark = "==" if (s0, s1) == (d0, d1) else "!="
        print(f"  instr {si}: src {s0:#04x} {s1:#04x}  dst {d0:#04x} {d1:#04x}  {mark}")


def main() -> None:
    src = int(sys.argv[1]) if len(sys.argv) > 1 else None
    dst = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    k = int(sys.argv[3]) if len(sys.argv) > 3 else 1

    with Board() as board:
        if src is None:
            src = board.ui_track_get()
        running = False
        try:
            running = bool(board.tick_query().get("running", 0))
        except Exception:
            pass
        family = "TAPE (while-playing grab)" if running else "RE-SIM/FLATTEN (stopped)"
        print(f"capture-fidelity diff: src=T{src+1} dst=T{dst+1} k={k}  [{family}]")
        print("snapshotting source ...")
        src_snap = snapshot(board, src)

        status = board.capture_span(src, k, dst)
        if status != 0x01:
            print(f"capture REFUSED: status {hex(status)} "
                  "(see CMD_CAPTURE_SPAN codes; -13/0x1D = chord source, use Pattern-capture)")
            return
        time.sleep(0.2)
        print("snapshotting deposit ...")
        dst_snap = snapshot(board, dst)
        report(src_snap, dst_snap)
        print("\nLaw check: TAPE grabs should show the echo/lfo/dir group under RESET;")
        print("stopped/flatten grabs should show them under PRESERVED. Anything under")
        print("CHANGED, or a '!=' lane you didn't expect, is the concrete divergence —")
        print("paste this whole output into the session.")


if __name__ == "__main__":
    main()
