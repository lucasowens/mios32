# As-heard windowing for note grabs (deferred CAPTURE item #3)

**Status:** building. Created 2026-06-28. Follows the unified-CAPTURE commit (eb6019e1).
**Home when done:** fold into design §9 (build narrative) + §10 (close the deferred note) +
REFERENCE (the CAPTURE window phase) + MANUAL (the Phase toggle), then retire this plan.

---

## The deferred item (verbatim, design §9 2026-06-28)

> **Deferred (none block):** #3 "as-heard" windowing for note grabs (still loop-aligned;
> phase-offset = mid-run-restart edge)

## What it is

A while-PLAYING note grab today is **loop-aligned**: the tape window keeps only *completed*
loops, ending at the last loop downbeat (`SEQ_CORE_CaptureSpanTape`, `seq_core.c`):

```c
win_end   = loops_done * P;        // snaps back to the last loop downbeat
win_start = win_end - k * P;       // (Approach-A non-aligned branch; the whole-measure
                                   //  branch snaps to bar_start[] markers — same idea)
```

So a grab mid-loop gives you "the last k *completed* loops" — not the last k bars you just
heard. The code even flags the gap: it assumes the track started phase-aligned at tick 0
("a mid-run synch/restart offset is the documented edge, deferred") — the **mid-run-restart
edge** in the deferred note.

**As-heard** ends the window at the playhead instead:

```c
win_end   = now;                   // the playhead, exactly what just sounded
win_start = now - k * P;
```

You keep exactly the last k bars as heard; the deposited loop's step 0 = the moment you
grabbed, so it **restarts mid-run** (phase-shifted off the source downbeat). It is strictly
*simpler* than the loop-aligned code — purely relative to `now`, it never computes a loop
boundary, so it sidesteps the mid-run-restart assumption entirely. **Tape-path (PLAYING)
only**; STOPPED re-sim has no playhead and stays loop-aligned (GRID).

## Decision (AskUserQuestion 2026-06-28)

**Selectable Phase mode GRID/HEARD, default GRID, tape-path only.** Keeps loop-aligned as a
material (the §2 "constraints are materials" rule) and leaves every existing grab untouched
until you reach for HEARD. Phase interacts with Fit:FILL/LOOP — FILL tiles grid-locked
(downbeat matters → GRID), LOOP drifts free (HEARD is natural) — so GRID keeps real value.

## Why no snap / no smear (the key correctness argument)

With `win_start = now - k*P` (P = spm*tps) and a source note emitted at absolute tick T on
the source step grid, its dst step = `floor((T - win_start)/tps)`. Writing T = g*tps (clean
tick-0 grid, no groove), the residual `(T - win_start) mod tps = (-now) mod tps = δ` is the
**same constant δ < tps for every note**, so `floor()` recovers each integer step index
exactly: `dst[d] = src[(d + ceil(win_start/tps)) mod spm]`. The whole capture is one global
phase-shift of δ < one step, inaudible on the dst's own grid. **No step-snapping needed.**

`rotation = ceil(win_start/tps) mod spm` is the observable: 0 for GRID (win_start on a
downbeat), the playhead step for HEARD.

## Thermometer (MaxK) needs no phase param

HEARD's `win_start = now - k*P >= GRID's (loops_done-k)*P` (since `now >= loops_done*P`), so
HEARD reaches **less** far back than GRID for the same k — the -10 eviction guard is at least
as easily satisfied, and the grabbable count is the same magnitude. MaxK stays an upper
bound; the grab's own `k*P <= now` + eviction guards are the real safety.

## Build steps

1. **Engine (seq_core.c / .h):**
   - `SEQ_CORE_CAP_PHASE_GRID 0` / `SEQ_CORE_CAP_PHASE_HEARD 1`.
   - `SEQ_CORE_CaptureSpanTape(src, dst, k, phase)` — HEARD branch (above), guarded
     `k*P <= now`, `k <= RING_BARS-1`. Record `win_start/win_end/tps` statics for HIL.
   - `SEQ_CORE_CaptureSpan(src, dst, k, phase)` → tape gets phase; re-sim unchanged.
   - `SEQ_CORE_CaptureSpanToSlotTrack(…, fit_mode, phase)` → passes phase to the internal
     scratch CaptureSpan (seq_core.c:3020).
   - Accessors `SEQ_CORE_CaptureSpanWinStart/WinEnd/Tps()`.
2. **UI (seq_ui_capture.c):** `cap_phase` (reset GRID on Enter), GP2 encoder toggle (CW→HEARD,
   CCW→GRID), LCD-R readout (`Ph:GRID/HEARD`, `--` for Save). Pass `cap_phase` in Commit.
3. **Legacy gestures (seq_ui.c:689, :836):** pass `SEQ_CORE_CAP_PHASE_GRID` (unchanged).
4. **testctrl:** `CMD_CAPTURE_TO_SLOT_TRACK` 7th byte = phase; `CMD_CAPTURE_SPAN` payload[4] =
   phase; SPAN reply gains win_start(5×7) + win_end(5×7) + tps(2×7) for the rotation pin.
5. **Harness (board.py / sysex.py):** `phase=` kwargs; parse the extended SPAN reply.
6. **HIL (test_capture_as_heard.py):** static forward line, transport-run, wait depth≥2.
   - GRID control: win_start loop-aligned, dst[0]==src[0] (already pinned by while_playing).
   - HEARD: read playhead T0; grab; assert `win_end >= T0` (proves as-heard, GRID would snap
     back mid-loop); `rotation = ceil(win_start/tps) % spm`; assert
     `dst[s] == src[(s+rotation) % spm]` over all 2*spm steps (note-for-note phase pin).

## Adversarial review (2026-06-28, 4 lenses → verify; 27 findings, 1 real defect)

The capture-timing review (the §2 #8 load-bearing pattern) confirmed claims #1–#3 by independent
trace + simulation (rotation formula correct, no seam drop/collision, no under/overflow, all call
sites migrated, byte layout exact, tests non-vacuous). **One real defect caught + FIXED:**

- **SYNCH_TO_MEASURE + foreign clkdiv window bug.** HEARD computed `P = spm*tps`, but for a synch
  track `CaptureLoopSteps` reports `spm = gspm` while `tps` stays the track's own `step_length`,
  so an 8th-note synch track got `P = gspm*192 = 2×` its real one-measure audible loop → wrong
  span + phase (GRID is immune; it reads real `bar_start` markers). **Fix:** for SYNCH_TO_MEASURE,
  `P = gspm*96` (the global measure tick length, = the re-sim's `B`, = GRID's marker span). The
  "reaches no further back than GRID" invariant holds again. Default + non-synch paths unaffected.

The other 3 "real" verdicts were downgraded to nits (test framing/robustness, not bugs). Acted on
the worthwhile ones: rotation-uniqueness guard tightened to `len(set(src)) == SPM`, output-mirror
caveat noted, and a slot-track `phase=1` pin added (covers the 7th-byte packing + the
`CaptureSpanToSlotTrack(...,phase)` threading the engine pins didn't reach).

## Open / deferred

- HEARD + FILL tiling: an off-downbeat capture tiled grid-locked puts the source downbeat
  mid-canvas. Acceptable (LOOP is the natural HEARD fit); revisit by ear.
- HEARD + chord path: `CaptureChordWindow` copies the source chord par loop directly (no
  tape, no `now`) → it ignores phase (always loop-aligned). Document; melodic-only first cut.
- Groove/port-delay on the window seam (already a documented tape edge) is unchanged.
