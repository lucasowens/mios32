# G2 (part 1) — the plane toggle, proven by migrating Robotize (two-faced)

**Date:** 2026-07-05 · **User steer:** "second page in general" + a uniform toggle so
two-faced units (Robotize params ↔ Robotize Loop) flip back and forth; **drop** the
split-view right-half idea (a bespoke face gets a FULL plane, not a persistent half).
Chosen path: **straight to Robotize** as the proving ground.

## The plane model (the reusable G2 piece)
- A rack row may declare a **2nd plane**. A plane is either a **dial bank** or a
  **bespoke face** (full surface). Reached by a **uniform ‹/› toggle**; muscle memory is
  the flip, only content differs. GP row belongs to whichever plane you're on.
- **Minimal-churn implementation (build-less):** don't wrap every row in a `planes[]`
  array yet — add an OPTIONAL 2nd plane to `proc_row_t`:
  `{ const proc_param_t *params2; u8 n_params2; u8 face2; }` (NULL/0 = single-plane).
  Only Robotize sets them. Global `u8 ui_proc_plane` (0/1), reset to 0 on focus change.
  `SlotParams()` returns plane 0 or plane 1. ‹/› toggles when a 2nd plane exists, else
  swallowed. LCD shows a `1/2` plane cue. Generalise to `planes[]` only when a 3rd plane
  is actually needed.
- **`face2` (bespoke id)** drives the custom GP-row / GP-button / readout branches —
  keyed on the descriptor's face id, not a per-slot compare. A step toward G2's custom
  hook without full function pointers yet.

## Occupancy generalisation (Robotize forced it)
Add `u8 enable_cc` to `proc_row_t`. RowState enabled = occupied &&
(disable_mask ? !(occ&mask) : enable_cc ? cc!=0 : 1). Double-tap: flip disable bit, else
toggle enable_cc, else SlotReset. Robotize: occ_cc = `ROBOTIZE_PROBABILITY` (occupied =
prob>0), enable_cc = `ROBOTIZE_ACTIVE`. Echo/Groove/LFO unchanged (disable_mask path).

## Robotize row — two planes (first playable cut)
**Plane A — OPERATE ("how much chaos"), 6 dials, all 0..31:**
Prob (headline; engages — sets `robotize_active=1` and, on 0→on, **seeds the per-dim
RANGES** note=5/vel=32/len=32/oct=1 if still 0, so the probability dials have something to
act on), Note, Vel, Len, Oct, Skip (each = that dimension's *probability*). Double-tap =
toggle active (live A/B, keeps config).

**Plane B — LOOP ("lock it in"), bespoke face `PROC_FACE_ROBOLOOP`:**
- **GP row = the 16 bar-anchors.** Lit = in the current loop window, flashing = the
  playing anchor; **tap = reroll that slot** (`SEQ_ROBOTIZE_RerollBar`) — the molding tool.
- **Dials:** Cyc (`loop_cycles` 0..16, 0=loop off — the loop headline), Pal
  (`palette_length` 1..16), Start (0..15), Rot (0..15), + two **ACTION** dials:
  **Rsd** (push = `SEQ_ROBOTIZE_Reseed`) and **Frz** (push = `SEQ_ROBOTIZE_FreezeQuantized`
  last Cyc bars). New `PROC_KIND_ACTION`: turning = no-op, **push = execute** (the one place
  encoder-push isn't snap-to-default; justified — an action has no value to snap).
- Right screen: loop status (cycles / playing anchor).

## Keep the old pages (like Groove/LFO)
Leave `seq_ui_fx_robotize` + `seq_ui_robomold` for the deep config. The grammar row is the
live headline + loop faces. **Deferred (flag):** per-dimension ranges (seeded, tweak on the
old page), the exotic probabilities (sustain/nofx/echo/duplicate + per-dim probs), the
16-step mask, sync-to-master, freeze-quantized-vs-immediate choice, MASTER-SYNC. A 3rd
CONFIG plane can pull these in later — which is exactly why the toggle exists.

## Gesture note
‹/› is §3.5's reserved "page the banks" pair and idle on the PROC page. Verify it's not
doing something global there before claiming it. By-ear call.

## By-ear GO gate
Focus Robotize → dial Prob up (hear it randomize) → sweep Note/Vel/Len → **‹/› to LOOP** →
watch the anchor playhead, tap bars to reroll, push Rsd/Frz to lock in a take → **‹/› back**.
One unit, two faces, one toggle. GO ⇒ the plane model is validated; fold to §3.5 + §9 +
memory; the rest of G2 (formatter/defaults registry, planes[] generalisation, CONFIG plane)
follows.
