# Pitch-chain migration (Track 2) — plan (2026-06-10)

> **✅ EXECUTED + by-ear GO — 2026-06-10.** All four stages built, flashed,
> **HIL 108/108** (96 baseline + 12 new pins); by-ear confirmed same day
> ("works so well"). The durable record lives in: design doc §3
> (born-as-processors: bake program CLOSED), §8 Track 2 (SHIPPED banner), §9
> (2026-06-10 Track 2 BUILT decisions + accepted edges), §10 (edges closed; new
> drum-pitch open question); REFERENCE doc "Track 2 — stack-resident pitch
> chain" (symbols, fences, gotchas); MANUAL_FORK "The Pitch Chain" (+ POC-rule
> retirement in the GRAVITY notes); the code + `test_pitch_chain.py` (12 pins) +
> reshaped `test_capture_force_scale.py`. Two latent Track-1 bugs fixed en
> route (stranded RESOLVE flag; preset-import/capture-restore slot desync — the
> second found by adversarial review pre-flash, along with 8 other real fixes
> across two review passes). This plan is spent scaffolding; safe to archive.

**Status: EXECUTED.** Original gating context (historical): licensed by the
Tension Workbench by-ear GO (2026-06-10, design doc §8). Direction confirmed
with the user 2026-06-10: **Arp playmode is fenced at emission** (legacy chain
untouched for arp tracks; migrate-when-touched, the chord-mask precedent).
Staged checkpoints like Track 1.

Durable homes this plan feeds: design doc §3 (born-as-processors — the legacy-effect
list shrinks), §8 Track 2 (SHIPPED banner when done), §9 (decisions), §10 (Track 2
edges close), REFERENCE doc (chain ordering facts), MANUAL_FORK (POC-rule retirement).

---

## 1. Why (recap)

Transpose → force-scale → limit run at **emission** ([seq_core.c:2581](../../apps/sequencers/midibox_seq_v4/core/seq_core.c),
2654, 2662): invisible to `OutputActive`, so bounce needs `SEQ_CORE_BakeForceScale` —
per-effect bake archaeology the architecture was supposed to end. Migrating the chain
into the render stack makes bounce faithful **by construction** and:

- **Retires the POC rule** (FTS off on gripped tracks): FTS runs *upstream* of
  TENSION in the stack, so a RUB/SLIP push survives instead of being re-snapped.
- **Fixes planed capture**: today's bake reproduces only static Normal-mode transpose
  (`BakeForceScale` sets `inc=0` for any other playmode) — a captured bus-planed
  track silently loses its planing. Post-migration, capture-as-heard works for
  Transpose-playmode tracks. This is the headline musical win.
- Stack ordering becomes **explicit** (§9 wanted FTS-vs-chord-mask order to be a
  musical choice, not incidental).

`SEQ_CC_ResetGenerativeForBounce` already resets playmode/FORCE_SCALE/transpose/
limits/bus on capture — the capture side needs **no new code**; bake is just deleted.

## 2. Target architecture

**Slot homes (order = musical chain, explicit):**

| slot | processor | sync source (tcc stays persistent truth) |
|---|---|---|
| 0 | **PITCH** (transpose + FTS, fused — entangled: snap-after-transpose keeps planing in-scale) | playmode, transpose_oct/semi, trkmode_flags.FORCE_SCALE, busasg, link_par_layer_scale/root |
| 1 | CHORD_MASK (moves from 0) | unchanged |
| 2 | TENSION (moves from 1) | unchanged |
| 3 | **LIMIT** (final range fold) | limit_lower/upper |

New IDs: `SEQ_PROCESSOR_ID_PITCH=3`, `SEQ_PROCESSOR_ID_LIMIT=4`. Order
PITCH → CHORD_MASK → TENSION → LIMIT: tidy/plane first, chord snap wins over scale,
tension pushes last (and survives), limit folds the result. This is a deliberate
behavior change for FTS+chord-mask combos (today FTS re-snaps *after* the mask at
emission); pin the new truth.

**PITCH processor semantics (mirrors `SEQ_CORE_Transpose` + emission FTS):**
- Normal/ChordMask playmode: static oct/semi (signed −8..+7, ≥8-wrap encoding).
- Transpose playmode: + bus offset via `SEQ_MIDI_IN_TransposerNoteGet(bus, HOLD,
  FIRST_NOTE)` − 0x3c; no key held → write **rest (0)** to the Note byte (today:
  velocity=0 at emission — same audible result, now bounce-visible).
- Global transpose (`seq_core_global_transpose_enabled`): bus offset on note events
  for non-Transpose playmodes, as today.
- Arp playmode: PITCH slot **disabled**; the legacy emission chain (transpose + FTS +
  limit, gated) runs as today. Fence, don't migrate.
- **Drum event mode: fenced too** (Stage-A review finding) — a 0 Note byte is NOT a
  rest on drum tracks (lay_const fallback still plays), so mirror-rest silence and
  the skip-0 idiom are both wrong there; transposer-no-key would fire default notes
  instead of silence. Legacy emission chain keeps drum tracks until drum pitch
  semantics get a design pass (§10 candidate).
- FTS: if FORCE_SCALE, per-step scale/root (mirror `SEQ_CORE_FTS_GetScaleAndRoot`,
  reading Scale/Root layers from the render par_buf — no processor mutates them),
  `SEQ_SCALE_NoteValueGet` snap after transpose.
- CC event mode: shift CC + PitchBend layer values only (the `is_cc` path).
  ProgramChange/Aftertouch are excluded although legacy "shifted" them: their wire
  bytes come from evnt1 while the legacy shift wrote evnt2 (don't-care) — it never
  reached the wire, so shifting stored values would invent audible program changes.
- Note layers only otherwise; 0 = rest, skipped. Known semantic edge: "transpose
  note 0 (C-2) and play it" is unrepresentable in the mirror — accepted loss.

**LIMIT processor:** `SEQ_CORE_TrimNote` octave-fold between lower/upper with the
existing resolution rules (either-set → active, upper-0 → 127, lower>upper swaps).

**Dirty model:**
- Per-tick implicit dirty (the chord-mask mechanism) extends to **live-varying**
  pitch slots only: playmode==Transpose, global transpose enabled, or FORCE_SCALE
  with root following the keyboard. Static transpose/FTS/limit tracks render on
  events only (slot-sync touch on CC writes; `SEQ_PAR_Set` auto-dirty on layer
  edits; new dirty hooks at global scale/root/keyb-root change sites — SHADE,
  OPT page, MIDI-in scale changer, testctrl).
- PITCH/LIMIT slots enabled only when non-neutral (otherwise every track would
  full-render every tick). Watch item: each live-varying track adds a full
  per-tick render — fine at POC scale; §A2 render-cache is the future fix.

**Emission after migration:** read mirror note → groove/humanize/LFO (timing/vel) →
schedule. Transpose/FTS/limit calls gated to `playmode==Arpeggiator` only.

**Emission note-mutator carve-outs** (effects that mutate *pitch* after the stack;
as built in Stage C):
- **Echo** keeps its per-repeat FTS re-snap (emission-native, already
  bounce-invisible; feedback-shifted repeats stay in-scale). Edge: repeats of a
  TENSION-pushed note on an FTS track resolve into scale — the push survives the
  primary, the echoes decay into the terrain. Accepted (arguably musical).
- **Humanize-note / LFO-note**: one narrow re-snap after the PreFX block, **iff
  FORCE_SCALE && the mutator actually moved the note** (pass-through otherwise —
  never re-corrects stack output). Edge of an edge: a humanized note on a *pushed*
  (RUB) FTS track gets re-snapped — accepted.
- **Robotize needs NO carve-out**: this fork's robotize already walks scale
  degrees itself under FORCE_SCALE (`SEQ_SCALE_WalkScale`).

**Deleted (Stage D, done):** `SEQ_CORE_BakeForceScale` + its three call sites
(CaptureToSlot / CaptureToTrack / CaptureToSlotTrack) + prototype. Capture =
memcpy of OutputActive + existing generative-CC reset, nothing else. The arp
remainder (snap-the-encodings) was meaningless — arp capture was never
pitch-faithful with or without the bake.

**LIMIT processor details (Stage B, as built):** honors the per-step no_fx TRG
layer (read from the trg buffer being built — emission parity); the nth-trigger
bar-variant of no_fx is bar-dependent and cannot exist in a render — accepted
edge (nth still gates the emission FX). `ASG_NO_FX` writes re-render via the
LIMIT sync. ProcessorBounce also untangles limit/tension/pitch posture now.

## 3. Stages (each: build → HIL green → next; flash + by-ear at the end)

- **Stage A — PITCH processor + slot re-home.** IDs, slot homes 0..3, pitch
  render fn, slot-sync bridge + SEQ_CC_Set triggers, emission transpose+FTS gated
  arp-only, implicit-dirty extension, scale-change dirty hooks. Limit stays at
  emission this stage (still last in chain — consistent intermediate).
  Pins: static-transpose parity; planing parity (bus note injection, the tension
  tests' bus path); **planing-stays-in-scale** (the entanglement pin); neutral-tcc
  byte-identical pass-through; FTS per-step root/scale override parity;
  **FTS+GRIP coexistence** (push survives on a FORCE_SCALE track — POC rule
  retired); CC-mode transpose parity; chord-mask/tension regression after re-home.
- **Stage B — LIMIT processor.** Slot 3, emission limit gated arp-only.
  Pins: fold parity (lower/upper/swap/unset-upper), limit-after-tension ordering.
- **Stage C — mutator carve-outs.** Humanize/robotize conditional re-snap; echo
  re-snap verified against per-step scale/root. Pins: humanized note lands
  in-scale on FTS track; pushed note untouched when mutator doesn't fire.
- **Stage D — delete the bake.** Remove `SEQ_CORE_BakeForceScale` + call site;
  reshape the existing bake pins into mirror-faithfulness pins (same observable:
  captured == heard); **new pin: capture a planed+FTS groove → faithful** (the
  thing bake never did); full regression green.
- **By-ear (workflow level):** rig → plane a groove from the bus while FTS on
  (stays in-scale, capture it, recall — same place?); gravity push on the same
  FTS track (push survives? release still cadences?). Soft checks, not a §8-scale
  GO/NO-GO — Track 2 is licensed infrastructure; the test is "nothing got worse,
  two new things work".

## 4. Edges & risks (updated post Stage-A adversarial review)

- Arp + drum fences: gated calls only — fenced tracks' chain code untouched.
- **Stage-A interim bake**: `SEQ_CORE_BakeForceScale` still runs at capture until
  Stage D; for non-arp tracks it must now bake ONLY the emission-side limit (the
  mirror already holds transpose+FTS — re-applying would double-transpose and
  re-snap a surviving TENSION push). Arp keeps the full legacy bake. Done.
- **`SEQ_CORE_ProcessorBounce` untangle**: PITCH/TENSION are doubly-bound like
  chord-mask — bounce now also resets transpose/FORCE_SCALE/Transpose-playmode and
  tension_grip so a re-armed slot can't re-apply onto the frozen source. Done.
- **Transposer-release glide hang**: mirror rests produce zero events, so the legacy
  velocity-0 glide-release path can't run — a dedicated no-events branch at emission
  releases the held glide at the step boundary (source rests still glide through,
  legacy parity). Done.
- PitchBend under transpose: wire MSB matches legacy; LSB now derives from the
  shifted value (legacy kept the unshifted LSB) — inaudible, accepted.
- CC/PB dedupe (`cc_last_value`/`pb_last_value`) now compares post-pitch mirror
  values: a constant CC lane under a moving transposer re-sends on plane changes —
  improvement, but a wire-level change to know about.
- Glide dedup reads `p->note` post-mirror — same final value, no change needed
  (verified in A).

**Stage B/C/D adversarial review findings (all fixed pre-flash):**
- Track-preset import (`SEQ_FILE_T_Read`) and the `CaptureToSlotTrack` restore
  write tcc directly (no `SEQ_CC_Set`) → slots stayed desynced → imported/restored
  transpose/FTS/limit played silently raw. Both sites now re-run all four slot
  syncs + dirty.
- Mutated-note limit gap: humanize/LFO-moved notes escaped the window (legacy
  folded last). The Stage-C carve-out now re-folds after the re-snap.
- OOB trg read in the LIMIT no_fx escape (4-bit assignment can exceed allocated
  layers) — bounds-guarded, SEQ_TRG_Get parity.
- `lay_const` writes also re-sync LIMIT (layer retype changed which layers fold).
- **STOP mid-RESOLVE = land at 0 now** (Boundary, not bare Cancel) — consistent
  with stopped-RESOLVE's instant snap; freeze-at-mid-ramp is the by-ear
  alternative if landing feels wrong.
- Morph-blend off-scale pass-through is real (NOT "in-scale by construction") —
  a 50% blend of in-scale notes can sit off-scale where legacy emission FTS
  folded it; accepted edge, decide by ear.
- Transposer-note pickup latency: implicit dirty runs in the same tick prologue
  before emission — same-tick freshness, the chord-mask precedent (verify by ear).
- Slot re-home: anything pinning slot indices (HIL, testctrl, ProcessorBounce)
  must be swept in Stage A.
- `SEQ_LIVE` path untouched (live notes never enter the render buffers; same
  primitives, same tcc truth — consistency preserved).
