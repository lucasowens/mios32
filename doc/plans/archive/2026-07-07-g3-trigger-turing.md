# G3 — the trigger Turing machine (extends seq_generator.c, independent key-space)

**Date:** 2026-07-07 · **Follows:** G2 PitchGen rack-ification.
**The actual gap**, named two turns ago: GENERATE's five types (Eucl/CA/Poly/Sub/Lsys) are
static one-shot fills; nothing writes triggers *live*. This builds a genuine 2nd Turing
engine reusing PitchGen's proven mechanics (lock/rate/depth/anchor/roll/bounce), now writing
0/1 into a trigger layer instead of a note value into a par-layer.

## Why extend seq_generator.c instead of a new file (recon finding)
`seq_generator.c`'s pool is wired into UNDO (`SEQ_CORE_JournalArm`), FEARLESS SWITCHING,
the CAPTURE ring, and slot save/restore — ~10 call sites in `seq_core.c`, all via
`SEQ_GENERATOR_TrackSnapshot`/`TrackRestore`/`TrackClear`/`TrackEngagedCount`. A standalone
pool would need all of that re-wired or would silently leave "ghost" engaged generators
after an UNDO/switch. Extending the existing pool inherits all of it for free.

## Simultaneous pitch+trigger on ONE track (user's call, full build)
A single melodic (normal) track always resolves generator instrument index 0 for BOTH
kinds — so a shared `(track,instrument)` key space would make pitch-gen and trigger-gen
mutually exclusive on exactly the highest-value case (one melodic track, decoupled
pitch+rhythm). Fix: a **second, independent index table** (`pool_index_trg`), same shared
physical `pool[64]`. Requires "Trg"-twin public functions for the (track,instrument)-keyed
lookups; the pointer-based helpers (`LockGet/LockSet/MultGet/MultSet`) stay untouched since
callers already hold the resolved pointer.

## Struct changes (seq_generator.h) — size-neutral
- Rename the existing alignment-pad `reserved[1]` → **`trg_layer_p1`**: `0` = PITCH mode
  (unchanged, every existing slot defaults here via memset); `N>0` = TRIGGER mode, targets
  trigger-layer `N-1` — same "0=unassigned,index+1" convention `seq_trg_assignments_t`
  already uses elsewhere. Same byte, same position, same struct size.
- `range_min` doubles as **density** (0..127 on-probability) in TRIGGER mode. `range_max`
  and `contour_shape` go **unused** in TRIGGER mode — no analogue for a boolean (a coin
  flip has no distribution shape); an honest, considered-and-rejected mapping, not an
  oversight. New `SEQ_GENERATOR_DEFAULT_DENSITY = 64` (~50%).
- `loop[]`/`locks[]`/`anchor[]` are already generic `u8` arrays — a trigger slot stores 0/1
  in the same bytes a pitch slot stores note values in. `mult[]` exists but is unused by
  the rack UI in v1 (same as PitchGen's STEPS face — MULT stays stock-page-depth-only, and
  there's no stock page for TrigGen at all, so it's just dormant for now).

## Mutation semantics for a boolean register (deliberate divergence from pitch)
- `mutation_depth >= 127`: **reroll** = independent Bernoulli draw at `density` odds
  (mirrors pitch's `>=127` full-reroll threshold exactly).
- `mutation_depth` 1..126: **flip** (toggle 0↔1) — no graduated "how far" is possible for
  a 2-state value, unlike pitch's continuous ±depth window. A coarser dial than pitch's,
  flagged not hidden.
- `mutation_depth == 0`: unchanged (existing early-out in `mutate_loop`, already
  mode-agnostic — free).
- Both draws share the slot's own xorshift stream (`g->seed`), preserving the same
  deterministic/seekable/re-simulatable discipline pitch generators already have.

## Function-level changes in seq_generator.c
- `mutate_loop` / `roll_loop` / `seed_loop` / `write_loop_to`: one `if (g->trg_layer_p1)`
  branch each. `write_loop_to`'s trigger branch calls `SEQ_TRG_Set` per step (bounded by
  `SEQ_TRG_NumStepsGet`, tiling the 64-step loop the same way pitch tiles against
  `SEQ_PAR_NumStepsGet`) instead of `SEQ_PAR_Set`.
- `Get/IsEngaged/Disengage/Bounce/Anchor/Snap/LockToggle`: refactored into a shared static
  core parameterized by which index-table pointer to consult, with two thin public
  wrappers each (existing name = pitch, `Trg`-prefixed = trigger) — zero behavior change
  for the existing pitch wrapper, confirmed by matching the original bodies exactly.
- `Roll(track)`: gains a mode filter (`trg_layer_p1 != 0`) — **a real correctness fix**,
  since it currently walks the whole pool by track membership only; once trigger-mode
  slots share the pool, an unfiltered Roll would also reroll them when the PitchGen row's
  Roll action fires. `TrgRoll(track)` is the trigger-only twin.
- `TrackClear/TrackSnapshot/TrackRestore/TrackEngagedCount`: extended to walk **both**
  index tables — otherwise UNDO/FEARLESS SWITCHING/CAPTURE would silently not cover
  trigger-mode slots, reintroducing the exact "ghost generator" risk extending the pool
  was meant to avoid. `SlotSet` branches on `src->trg_layer_p1` to pick which key space
  and which validation path (trigger-layer bounds vs. par-layer/Note-layer collapse).
- New `SEQ_GENERATOR_EngageTrigger(track, instrument, trg_layer, density)` — parallels
  `Engage`, but validates/targets a trigger layer instead of a Note par-layer.

## The rack row — TrigGen (reuses `PROC_ROW_GENERATOR`, disambiguated by params-pointer
like every other row-identity check in this grammar)
Target instrument resolution is IDENTICAL to PitchGen's (`SEQ_UI_PROC_GenInstr` — drum
cursor / normal=0), reused as-is. Target LAYER resolves the track's assigned **Gate**
trigger layer (`trg_assignments.gate`), mirroring PitchGen's choice to target the
semantically-loaded Note layer rather than an arbitrary one.

**B-row double-tap = ENGAGE⟷DISENGAGE** (same reasoning as PitchGen — no dial has clean
0-means-off semantics here either).

**Plane A OPERATE** (4 cells): `Dens` (density, shown as %), `Rate`, `Dpth`, `Roll`
(ACTION). No Contour (no analogue). Dials no-op + dash pre-ENGAGE, same contract as
PitchGen.

**Plane B STEPS** (4 cells, same shape as PitchGen's): `Win` (shared window-select state
with PitchGen — an accepted simplification, not independent memory per row), `Anc`/`Snp`/
`Bnc` (ACTIONS, new ids — action ids are global to the module, so PitchGen's and TrigGen's
Roll/Anchor/Snap/Bounce need distinct ids to dispatch to the right accessor family). GP
row = LOCK toggle for the window, same paintable-shape idiom.

## By-ear GO gate
Assign a Gate trigger layer on a track (stock TRKEVNT config, unchanged) → focus **TrigGen**
→ double-tap ENGAGE → the rhythm should start generating/wandering at ~Density fullness →
sweep Rate/Dpth → push Roll → Up/Down to STEPS → lock a couple of steps → sweep Rate again
(locked steps hold) → Anc/Snp/Bnc same as PitchGen. **Then the actual point:** engage
PitchGen too, on the same melodic track — both should run independently, pitch and rhythm
decoupled, exactly the "hold the groove, evolve the melody" case. GO ⇒ fold to §9 + memory.
