"""Pre-set-up rigs — configure the live device for a known scenario in seconds.

Manual track setup after every flash is the throughput killer for by-ear and HIL
testing (Tension Workbench plan §1: "setup friction is what kills ear-testing").
Each rig here is a builder `build_<name>(board, **opts)` that drives the harness
to leave the device ready to play. Run one from the CLI after a flash, or call it
from a pytest fixture for deterministic test setup.

    python -m harness.rigs --list
    python -m harness.rigs tension                  # set up the live device
    python -m harness.rigs tension --grip 120 --gravity -40
    python -m harness.rigs tension --save A:0:5     # ...and snapshot to SD slot

Add a rig: write `build_<name>(board, **opts)` returning a RigResult, then
register it in RIGS. Keep builders idempotent (start from board.reset()).
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field

from .board import Board, BoardNotFound
from .sysex import CC, Page


# A3 in the AUTOTEST session: every gate lit, track-0 par-layer-0 = Note (C-3
# default), 16 steps. The deterministic baseline the rig builders layer onto.
BASELINE_BANK = 0
BASELINE_PATTERN = 2

# A C-major run up then down with one chromatic spice (F#, step 11). Varied
# pitches across the bar so a GRAVITY sweep audibly reshapes different scale
# degrees / the chromatic note differently — pull collapses, push adds tension.
TENSION_MELODY = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 66, 65, 64, 62, 60]


@dataclass
class RigResult:
    """What a builder configured — returned for CLI summary + HIL assertions."""
    name: str
    lead_track: int
    melody: list[int] = field(default_factory=list)
    scale: int = 0
    root: int = 0
    grip: int = 0
    gravity: int = 0
    saved_to: tuple[int, int, int] | None = None
    notes: list[str] = field(default_factory=list)


def build_tension(
    board: Board,
    *,
    group: int = 0,
    scale: int = 0,        # seq_scale_table index; 0 = Major
    root: int = 0,         # 0..11; 0 = C
    grip: int = 110,       # per-track GRAVITY-field grip (0..127)
    gravity: int = 0,      # global dial start (0 = detent / pass-through)
    port: int | None = None,   # mios32 port value; None = leave the pattern's
    channel: int = 0,
    session: str | None = "AUTOTEST",  # clean single-layer baseline; None = current
    save: tuple[int, int, int] | None = None,  # (group, bank, pattern) to persist
    verbose: bool = True,
) -> RigResult:
    """The Tension Workbench rig: a single gripped melodic lead under the GRAVITY
    field, FTS off (POC rule), scale pinned, the cockpit page up. Press PLAY and
    sweep GP1 to hear pull/push; GP8 = RESOLVE. Library-shaped — a counterpoint
    voice / chord-source track are natural follow-on additions.

    Loads the AUTOTEST session by default so the baseline A3 is a clean SINGLE
    note layer — otherwise the lead is buried under whatever extra layers the
    active session's A3 carries, and the field's effect is masked (the 'nothing
    affects the sound' trap). Pass session=None to build on the current session."""
    lead = group * 4
    res = RigResult(name="tension", lead_track=lead, melody=list(TENSION_MELODY),
                    scale=scale, root=root, grip=grip, gravity=gravity)

    if session is not None:
        board.session_load(session)                    # clean single-layer A3 baseline
        res.notes.append(f"loaded session '{session}' (clean baseline)")
    board.reset()                                      # stop, page→edit, unmute, clear
    board.pattern_load(group, BASELINE_BANK, BASELINE_PATTERN)   # A3 → every gate, Note

    for step, note in enumerate(TENSION_MELODY):       # varied melodic line
        board.track_par_set(lead, 0, 0, step, note)

    board.set_force_scale(lead, False)                 # POC rule: FTS off on gripped
    board.cc_set(lead, CC.TENSION_GRIP, grip)          # hold the lead in the field
    board.global_scale_set(scale, root_selection=root + 1, keyb_root=0)  # pin key
    board.tension_set(gravity)                         # start at the detent

    if port is not None:
        board.track_config(lead, port, channel)

    board.page_set(Page.GRAVITY)                       # cockpit up
    board.ui_track_set(lead)                           # lead is the visible track

    if save is not None:
        ok = board.pattern_save(*save)
        res.saved_to = save
        res.notes.append(f"saved to group{save[0]} bank{save[1]} pat{save[2]}: "
                          + ("ok" if ok else "FAILED"))

    res.notes.append(f"lead track {lead}: 16-step C-major line, GRIP={grip}, FTS off")
    res.notes.append(f"scale idx {scale}, root {root}; GRAVITY={gravity} (detent=0)")
    res.notes.append("press PLAY, sweep GP1 (GRAVITY), GP8 = RESOLVE")
    if port is None:
        res.notes.append("note: track keeps the pattern's MIDI port — set your "
                         "monitoring port with --port if you hear nothing")

    if verbose:
        _print_result(res)
    return res


RIGS = {
    "tension": build_tension,
}


def apply(board: Board, name: str, **opts) -> RigResult:
    """Dispatch by rig name. Raises KeyError on an unknown rig."""
    if name not in RIGS:
        raise KeyError(f"unknown rig {name!r}; known: {', '.join(sorted(RIGS))}")
    return RIGS[name](board, **opts)


# --------------------------------------------------------------------------- CLI

def _print_result(res: RigResult) -> None:
    print(f"rig '{res.name}' ready:")
    for line in res.notes:
        print(f"  - {line}")


def _parse_slot(s: str) -> tuple[int, int, int]:
    """Parse 'G:B:P' (group letter A-D or 0-3, bank 0-7, pattern 0-127)."""
    parts = s.split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(f"--save must be GROUP:BANK:PATTERN, got {s!r}")
    g, b, p = parts
    group = "ABCD".index(g.upper()) if g.upper() in "ABCD" else int(g)
    return group, int(b), int(p)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Set up a pre-built rig on the live SEQ V4.")
    ap.add_argument("rig", nargs="?", help="rig name (see --list)")
    ap.add_argument("--list", action="store_true", help="list available rigs")
    ap.add_argument("--scale", type=int, default=0, help="seq_scale_table index (0=Major)")
    ap.add_argument("--root", type=int, default=0, help="root 0..11 (0=C)")
    ap.add_argument("--grip", type=int, default=110, help="per-track GRIP 0..127")
    ap.add_argument("--gravity", type=int, default=0, help="GRAVITY start -64..63 (0=detent)")
    # 0x11 = mios32 USB1, shown as "USB2" in the SEQ UI (off-by-one display) — the
    # user's monitored output. Override with another mios32 port value if needed.
    ap.add_argument("--port", type=lambda s: int(s, 0), default=0x11,
                    help="mios32 MIDI port for the lead (default 0x11 = SEQ 'USB2')")
    ap.add_argument("--channel", type=int, default=0, help="MIDI channel 0..15")
    ap.add_argument("--session", default="AUTOTEST",
                    help="session to load for a clean baseline ('none' = keep current)")
    ap.add_argument("--save", type=_parse_slot, default=None, help="persist to GROUP:BANK:PATTERN")
    args = ap.parse_args(argv)

    if args.list or not args.rig:
        print("available rigs:", ", ".join(sorted(RIGS)))
        return 0

    try:
        board = Board()
    except BoardNotFound as e:
        print(f"no board: {e}")
        return 1
    session = None if args.session.lower() == "none" else args.session
    try:
        apply(board, args.rig, scale=args.scale, root=args.root, grip=args.grip,
              gravity=args.gravity, port=args.port, channel=args.channel,
              session=session, save=args.save)
    except KeyError as e:
        print(e)
        return 2
    finally:
        board.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
