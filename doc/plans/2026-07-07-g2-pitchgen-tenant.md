# G2 (part 2) — PitchGen onto the grammar (the rack's first GENERATOR row)

**Date:** 2026-07-07 · **Follows:** G2 part 1 (plane toggle + Robotize).
**Context:** a "what's next" design conversation surfaced that the dynamic pitch Turing
machine the user was picturing **already exists and ships** — `seq_generator.c`, a 64-step
self-mutating loop with lock/depth/contour/anchor/roll/bounce, live on drum + normal tracks
via its own dedicated page (`seq_ui_trkpitchgen.c`). The real gap is a **trigger** Turing
machine (doesn't exist — GENERATE's 5 types are all static one-shot fills). User's call:
rack-ify PitchGen FIRST — proves the rack can host a truly continuous, self-mutating tenant
(not just emission FX/config-copy), and shapes the slot the trigger machine will want later.

## Why this doesn't fit the existing rowkinds
`PROC_ROW_STACK` reads a render-stack slot; `PROC_ROW_EMISSION` reads a CC via
`{occ_cc, disable_mask|enable_cc}`. PitchGen's state lives in a **pool slot**
(`SEQ_GENERATOR_Get(track, instr)` → pointer or NULL) with its own alloc/free lifecycle
(ENGAGE/DISENGAGE/BOUNCE) — not a plain CC. New rowkind: **`PROC_ROW_GENERATOR`**.
Occupancy = slot allocated (`g != NULL`); enabled = `SEQ_GENERATOR_IsEngaged`; strength =
`mutation_rate` (imperfect but workable — Rate=0 while engaged is "frozen," not silent, but
it's the closest "how alive" proxy for the wink cue).

## Target resolution (duplicated from `seq_ui_trkpitchgen.c`, ~10 lines, no cross-file coupling)
- `GenInstr(track)`: drum track → `ui_selected_instrument` (drum cursor); normal → `0`.
- `GenParLayer(track)`: cursor-aware — the cursor's Note layer if it is one, else the
  track's linked Note layer.

## The row — two planes
**Plane A — OPERATE** (6 cells): `Lo`/`Hi` (range_min/max, 1..127), `Rate` (mutation_rate
0..127), `Dpth` (mutation_depth 0..127), `Cont` (contour, cycles Uni/Lo/Hi/Tri), `Roll`
(ACTION — one-shot reroll unlocked steps). Dials **no-op pre-ENGAGE** (mirrors the stock
page's own contract: "no allocated slot ⇒ nothing to tune yet") — display prints dashes,
not 0, when `g == NULL`.

**B-row double-tap = ENGAGE ⟷ DISENGAGE** (not turn-a-headline-dial-up — Rate=0 is a valid
*engaged* state here, unlike Groove/LFO/Robotize's kind-0-means-off, so there's no clean
headline proxy for occupancy). Matches the row's existing bypass idiom generalized to
"toggle the engine," and mirrors the stock page's GP1 semantics minus ROLL (which moves to
its own ACTION dial, freeing the toggle to be a clean binary). ENGAGE failure messages
(-1 pool full / -2 bad track / -3 no Note layer assigned) surface via `SEQ_UI_Msg`, same
text as the stock page.

**Plane B — STEPS** (`PROC_FACE_PITCHGEN_STEPS`, 4 cells): `Win` (0..3, which 16-step
quarter of the 64-step loop the GP row shows — UI-only static, no CC, mirrors Groove's
Lane selector), `Anc`/`Snp`/`Bnc` (ACTIONS — Anchor / Snap-to-anchor / Bounce-freeze-and-
free-slot). **GP row = LOCK toggle for the current window** (tap = `SEQ_GENERATOR_LockToggle`
at `window*16+i`; lit = locked) — the paintable-shape idiom, 4th time now (ChordMask mask /
Groove step-shape / Robotize bar-anchors / this).

**Deferred (flag, don't build):** per-step MULT on the GP row (a 4-state cycle would need
a 2nd Lane-style selector on top of Lock — real depth, not needed to prove the row works);
generator UNDO (the track-wide pre-engage snapshot restore — already covered by the
existing global UNDO/REDO safety net, redundant to duplicate here).

## New descriptor pieces (additive, no regressions to existing rows)
- `proc_rowkind_t` += `PROC_ROW_GENERATOR`.
- `proc_pkind_t` += `GEN_RANGE_LO/HI`, `GEN_RATE`, `GEN_DEPTH`, `GEN_CONTOUR`, `GEN_WINDOW`.
- `PROC_ACT_*` += `GEN_ROLL`, `GEN_ANCHOR`, `GEN_SNAP`, `GEN_BOUNCE` (reuses the
  `PROC_KIND_ACTION` push-to-execute mechanism Robotize introduced).
- `proc_face_t` += `PROC_FACE_PITCHGEN_STEPS`.
- `RowState`, the B-row double-tap, GP-button paint, GP-row LED, and the right-screen
  readout each gain one more `else if` — the same shape every prior tenant added.

## By-ear GO gate
Focus PitchGen on a melodic (or drum) track → double-tap to ENGAGE (hear the line start
wandering) → sweep Rate/Depth (chaos amount) → Lo/Hi (range) → Cont (distribution shape) →
push Roll (one-shot reroll) → Up/Down to STEPS → tap a few GP keys to LOCK steps → back to
OPERATE, sweep Rate again (locked steps hold, others still wander) → push Anc, wander more,
push Snp (hard return) → push Bnc (freeze to static, row goes dark) → double-tap to
re-ENGAGE (fresh line). Feels like the same rack movement as every other tenant, with a
genuinely different, continuously-alive character. GO ⇒ fold to §9 + memory; the trigger
Turing machine is next, now with a proven slot shape to target.
