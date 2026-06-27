# Unified CAPTURE — one PATTERN-hold interface (flatten vs keep-generators)

**Status:** design agreed in conversation 2026-06-27; build not started. Supersedes the
gesture half of the just-committed recorder→slot work (`57dc55af`) — the *verbs* are reused.
**Home when done:** fold into design §9 + REFERENCE (capture verbs/gesture) + MANUAL, then retire.

---

## The model (two orthogonal axes, one interface)

The capture surface had two gestures doing overlapping things on a fuzzy axis. Re-derived
with the user, the real axes are:

- **WHAT you deposit** — *FLATTEN* (freeze the recorder: the last K bars of what actually
  played → static notes, generator stripped) vs *SAVE* (the **living** track: CC incl.
  generative bits + par/trg + the generator pool → recall **regenerates** it).
- **WHERE it lands** — always a **(pattern, track)** pair. Track = select-row (default =
  source track). Pattern = the step-row pattern grid; **defaults to the current pattern**,
  so leaving it alone = "this track, current pattern" and aiming another = "deposit into
  another pattern, preserving its other 3 tracks, without switching."

What each mode uniquely gives:
- **FLATTEN** is the only thing that freezes the *performance* (emission coin-flips / live
  keys / wander) — a normal save can't.
- **SAVE** (keep-gen, single track, preserve-others, no-switch) is what a normal whole-group
  pattern save can't: drop **one** living track into **another** pattern mid-jam without
  clobbering that pattern's other tracks or leaving the pattern you're playing.

## The interface — TRANSPOSE-page convention, entered from PATTERN

PATTERN is the home (it's what you're ultimately producing — a pattern — with either mode).
The layout steals the **transpose-page convention** (`seq_ui_trktran.c`): a short **labeled
item menu** on the LCD (selected item bracketed `>...<`), and the **16-step row is a
context value-picker for the SELECTED item** — exactly how transpose draws `-8…+7` across the
row for Octave/Semitone and you press a step to set it (`:270-295`). This unties the step-row
contention: bars and pattern each get the WHOLE row, but only when their item is selected, and
the bar **thermometer returns for free** (the Bars picker lights to the grabbable max).

```
Capture: >SAVE<   Trk:T3   Pat:A2   Bars:3        (cursor brackets the selected item)
step row = value-picker for the SELECTED item:
   MODE  selected → SAVE ⇄ FLATTEN
   Trk   selected → pick track 1..16   (default = source track; picks the group too)
   Pat   selected → pick pattern slot  → press COMMITS
   Bars  selected → pick K, lit to grabbable-max (thermometer)   (FLATTEN only)
 encoders also adjust the selected item; datawheel = a convenient bars knob in FLATTEN.
```

- **MODE item** = SAVE ⇄ FLATTEN, bracketed like transpose's `>Scale<` — labeled, visible.
- **Pat item**: the step-row press selects the slot AND **commits** (the chosen commit action).
  Group-letter defaults to current (aim another letter later if wanted).
- **Bars item** (FLATTEN only): step-row picker lit to grabbable-max — the thermometer, back.
- **Entry = TAP PATTERN to enter a latched Capture mode** (EXIT/PATTERN leaves) rather than
  hold-the-whole-time — the transpose-style select-then-pick interaction suits a latched mode.
  *(Confirm: latched vs momentary hold.)*
- **UTILITY-hold capture is removed** — UTILITY goes back to just opening the Utility page.

## Verb mapping (most code already exists)

- **FLATTEN** → `SEQ_CORE_CaptureSpanToSlotTrack(src, dst_track, bank, pattern, k)` — the
  recorder→slot verb already shipped in `57dc55af` (+ the geometry-restore fix). k = datawheel.
- **SAVE** → **NEW** `SEQ_CORE_CopyTrackLiveToSlot(src, dst_track, bank, pattern)` — copy the
  live track (full CC, par/trg, generator pool) into the slot's track, preserving the other 3,
  non-destructive to live. The ONLY genuinely new engine. Mirrors `CaptureToSlotTrack` minus
  the flatten (no `ResetGenerativeForBounce` / `GenClear`) plus the source generator copied in
  (`SEQ_GENERATOR_TrackSnapshot(src)` → `TrackRestore(dst_track in slot)`); relies on the
  gen-state-in-bank-files persistence (already shipped). Uses the shared `slottrk_*` snapshot
  (now geometry-safe after the `SlotTrkRestoreGeom` fix).
- The static `CaptureToSlotTrack` (render-mirror flatten) becomes unused by the gesture; keep
  it (testctrl) or retire it in stage 3.

## Stages (flash + by-ear each)

1. **SAVE engine** — `CopyTrackLiveToSlot` + HIL pin: deposit a generative track into a slot,
   recall it, assert the generator is present and the slot's other tracks survived. (Geometry
   fix already in the tree — fold its commit in here.)
2. **Unified Capture page (transpose convention)** — labeled item menu (MODE/Trk/Pat/Bars) +
   context step-row value-picker; Pat-press commits, Bars picker = thermometer, MODE toggles.
   Entered from PATTERN (latched, pending confirm). Dispatch SAVE→CopyTrackLiveToSlot,
   FLATTEN→CaptureSpanToSlotTrack.
3. **Remove UTILITY-hold capture** + docs (design §9/REFERENCE/MANUAL) + retire this plan.

## Open / deferred

- Cross-group-LETTER deposit (GP3-8) — defer; default-to-current covers "same track, another
  pattern."
- SAVE recall semantics: living = regenerates on recall (per-track-RNG deterministic). Confirm
  by ear whether it should replay the same wander or re-seed fresh.
- Commit deviates slightly from pure robotize (pattern-press commits the SD write, vs robotize's
  live-apply) — intended.
