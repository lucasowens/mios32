# Handoff — Freeze faithfulness (done) + the next rounds

**Date:** 2026-06-07 · **Status of this round:** SHIPPED, by-ear confirmed, HIL 75/75.
Scaffolding doc — the durable decisions live in design doc **§9 "Freeze faithfulness"**;
codebase facts in **REFERENCE §3 (Capture / bounce)**. Delete/archive this once the
deferred items below are executed.

---

## Where we landed (read this first if picking up cold)

A live report — *drum beat + only Groove, frozen via the PATTERN-hold gesture, played back
"groove off, pattern wrong, almost too fast"* — turned out to be a **faithfulness** bug, not
a groove bug. The fix and its model:

**The model (the thing to keep in your head).** A capture has **two axes**:
- **Generation axis** — generators, randomness, robotize, echo, LFO, direction, transpose,
  per-step Probability/Nth, bus mode, morph. A freeze *commits/resets* these. Correct.
- **Shaping axis** — DETERMINISTIC, emission-time effects (groove first). Never baked into
  the captured `OutputActive`, so re-applying the CC on playback reproduces the sound
  *exactly*. These must be **PRESERVED**, not reset.

The bug was the code conflating the two. Two fixes shipped:
1. `SEQ_CC_ResetGenerativeForBounce` (seq_cc.c) no longer zeroes groove.
2. `SEQ_CORE_CaptureToSlotTrack` (the PATTERN-hold gesture verb) + `SEQ_CORE_CaptureToTrack`
   now copy the source's **full 0x00..0x7f CC config** (mirroring `PASTE_CLR_ALL` in
   `seq_ui_util.c`), not the lower-48-only "avoid garbage" subset. The lower-48 left
   length (0x4d), clkdiv (0x4c), groove (0x52/0x53) and trigger-asg (0x60..0x68) at the
   destination slot's defaults → "too fast / wrong gates / no groove".

`SEQ_CORE_CaptureToSlot` (testctrl in-place verb) was already faithful (whole-`tcc`
snapshot) — which is why the first groove HIL test passed while the real gesture stayed
broken. Lesson: **the testctrl path and the UI gesture path are different verbs — test the
gesture verb.** A path-completeness audit caught this.

**HIL pins:** `tests/apps/seq_v4/test_capture_to_slot.py` →
`test_capture_to_slot_preserves_groove` (in-place verb) and
`test_capture_to_slot_track_inherits_full_config` (gesture verb; pins length + groove).

---

## The user's workflow (from the 2026-06-07 planning discussion)

The user wants freeze to be **faithful** ("what I heard") AND predictable across all four
destination cells — they do **all four moves**:
1. **Bank a variation for later** — same group, save-only (don't disturb playback).
2. **Commit & keep sculpting** — in-place, same track.
3. **Layer against the live track** — same group, different track, audible live.
4. **Stage in a spare group** — cross-group, auto-load.

They chose the **§5 "two faces, picked at recall"** model (recording face = faithful tape;
posture face = regenerate). Scope was deliberately staged: **"faithful first, listen, then
more."** This round did *faithful*. The rest is below.

---

## Deferred — next rounds, in priority order

### 1. Finish the shaping-axis pass (the direct continuation; highest value)
Groove was the first carve-out. Other emission-time **deterministic** effects are still
stripped on freeze and will surprise the user the same way. Per-effect classification is in
design §9 and was produced by the 2026-06-07 audit. Order:

- **FORCE_SCALE — ✅ DONE 2026-06-07 (code + HIL pins; BY-EAR PENDING the flash).** Chose the
  **BAKE** path (Option B), NOT the per-bit-mask preserve sketched below: preserving the flag
  would re-snap live against the *global* scale, so a later key change would silently re-pitch
  the frozen copy. Instead `SEQ_CORE_BakeForceScale` (seq_core.c) snaps the captured non-drum
  Note par-layers into the heard pitch — `noteLimit(forceScale(transpose(raw)))` — and the flag
  stays reset. Called in all 3 freeze verbs before `ResetGenerativeForBounce`.
  - **Two gotchas a two-pass adversarial review caught (fixed):** (1) `SEQ_PAR_Get` reads the
    output *mirror*, `SEQ_PAR_Set` writes the *source* — a just-captured destination's mirror is
    stale, so the bake reads+writes `seq_par_layer_value` directly. (2) emission applies the note
    *limit* after the snap, so the bake reproduces it too (limits are reset).
  - **Scope:** drum + Chord-layer force-scale NOT baked (unbakeable per-step; documented). Folded
    in transpose+limit only as part of computing the force-scaled note's heard pitch.
  - **HIL:** `tests/apps/seq_v4/test_capture_force_scale.py` (gesture + in-place + snap→limit) +
    new `CMD_GLOBAL_SCALE_SET` testctrl verb / `board.global_scale_set`. **FLASH + run `-k
    force_scale` + listen** — that's the only remaining gate on this item.
- **transpose_semi/oct (for NON-force-scaled tracks)** — deterministic in Normal playmode
  (coupled to playmode, which is reset to Normal anyway → safe to preserve OR bake like
  force-scale). Must NOT reproduce in Transpose/Arp playmode (live keyboard input). NOTE: the
  force-scale bake already reproduces transpose for force-scaled tracks; this item extends it to
  the rest.
- **echo / LFO / direction** — deterministic but harder (echo is *additive* events not in
  the tape; LFO is phase-stateful; direction reorders steps and needs loop/length, which are
  preserved). Lower priority; echo Rnd1/Rnd2 modes must stay reset.

**Stay RESET (random/generative — re-applying diverges):** robotize, humanize, probability,
random_gate/value, echo Rnd modes, morph (its global `morph_value` isn't captured).

Each one: classify (preserve-as-CC vs bake-into-notes — bake when the effect reads mutable global
state), add a gesture-path HIL pin (via `board.capture_to_slot_track`, not just `board.bounce` —
different verbs), **confirm by ear.**

### 2. Unified four-move destination gesture
Today the PATTERN-hold gesture only writes **SD slots** (`CaptureToSlotTrack`). So:
- "Commit & keep sculpting" (in-place, live) is the *separate* GP8 verb, not the gesture.
- "Layer against the live track" (same group, live) has **no gesture** — `CaptureToTrack`
  (live-RAM landing) is engine-only, off the panel.
The redesign: one gesture, behavior determined by where you aim — *"it lands where you point
and you hear it there"* (live-RAM for the currently-loaded pattern's tracks; SD slot for a
stored pattern; auto-load for a spare group). See the table in the planning discussion. This
is a real UI + verb-routing change; design it in plan mode with the user.

### 3. The §5 two-faces posture persistence (the big one)
Recording face (faithful tape) is now solid. The **posture face** (re-engage generators on a
recalled capture) needs the **v3 pattern format** (carry processor-stack + generator state —
loop/anchor/per-step lock/mutation-multiplier). Spec is in design §6 ("Pattern format
extends to v3"); gated behind §8 spine work. Recall-picks-face is the load vs load+FREEZE
gesture (§5.5).

### 4. Smaller / safety
- **No UNDO for a slot overwrite.** Freezing onto a stored slot's track is irreversible on
  SD. Less acute now (faithfulness makes freeze-in-place sonically transparent), but still a
  data-loss footgun for a *generative* pattern (the posture is committed). Snapshot the dst
  slot's track before the read-modify-write to add UNDO.
- **Groove phase caveat (AR-3, optional).** With `sync_to_track=0` (global ref_step) and a
  track length NOT aligned to the measure, a frozen copy can re-groove with a different phase
  than was heard. Cleaner guarantee: force `groove_style.sync_to_track=1` on the capture
  destination so the frozen loop grooves against its own (preserved) step indices. The user's
  measure-aligned drum case reproduces fine without this; revisit if an unaligned-length
  groove sounds off by ear.
- **Multitimbral canvas routing.** The full-config copy now carries the source's MIDI
  port/channel onto the frozen copy (faithful). For a *multitimbral* canvas (different synth
  per track) you re-route by hand. If that becomes annoying, add a carve-out that keeps the
  dst track's port/channel on a cross-group stage.

---

## Key anchors (verify against source; line numbers drift)

- `SEQ_CC_ResetGenerativeForBounce` — `core/seq_cc.c` (the axis split; groove carve-out).
- `SEQ_CORE_CaptureToSlotTrack`, `SEQ_CORE_CaptureToTrack`, `SEQ_CORE_CaptureToSlot`,
  `SEQ_CORE_CaptureTrackOutput` — `core/seq_core.c`.
- Groove at emission: `SEQ_GROOVE_DelayGet` (timing → `t->timestamp_next_step`),
  `SEQ_GROOVE_Event` (vel/len) in `core/seq_core.c` tick path; tables in `core/seq_groove.c`.
- The faithful cross-track copy reference: `PASTE_Track` / `PASTE_CLR_ALL` in
  `core/seq_ui_util.c`.
- CC map: `core/seq_cc.h` (LENGTH=0x4d, CLK_DIVIDER=0x4c, GROOVE=0x52/0x53, ASG_*=0x60..0x68,
  lay_const=0x00..0x2f).
- The PATTERN-hold gesture: `SEQ_UI_Button_GP` in `core/seq_ui.c`.

## Build / test / verify
- Build: `cd apps/sequencers/midibox_seq_v4 && bash -c 'source ../../../source_me_MBHP_CORE_STM32F4 && make'` → `project_build/project.hex`. Flash via MIOS Studio (manual).
- HIL: `cd tests && bash -c 'source .venv/bin/activate && pytest -q'` (75/75 at the groove round; +3 force-scale pins added this round, collect-green, **hardware run pending the flash**). Targeted: `pytest -k "groove or full_config or force_scale"`.
- For any shaping-axis addition: add a **gesture-path** pin (via `board.capture_to_slot_track`), not just the in-place `board.bounce` — they are different verbs.
- The real proof is always **by ear** (music-first discipline).
