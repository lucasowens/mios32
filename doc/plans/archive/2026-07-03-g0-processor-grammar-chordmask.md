# G0 — The processor grammar, proven on ChordMask

**Status:** ✅ EXECUTED — SHIPPED with a by-ear **GO**, committed main `35cfd4b7` (2026-07-03).
All 6 build steps landed as written (the only deviations, both improvements: the "double-tap =
enable/bypass" step resolved to *toggle the ChordMask playmode* — its slot presence IS the playmode
— and `FWD_MIDI` is pinned ON in the **config reader**, not just at init, so an old SD config can't
strand it OFF). The decision + rationale now live in the design doc **§9 (2026-07-03)**; **G1 is
licensed**. This plan is scaffolding — kept for provenance; the durable home is the design doc.
Design home: UX study `2026-07-02-ux-study-fluid-flow.md` §3.5 (the operating model) + §4.11
(ChordMask). This plan executed the **G0** roadmap bundle.

**North-star (settled by-ear 2026-07-03):** the **Elektron model** — a *generic* parameter-page
interface (the same encoders remap across pages) with tasteful per-context flourishes that never
replace the generic base. Our restatement: **OPERATE (uniform) + CONFIG banks + CUSTOM (bespoke,
opt-in)** under the guard rail *"custom is an extra on top of OPERATE, never the only way in."*

**Goal of G0:** the thinnest vertical slice that operates **one real processor (ChordMask)**
entirely through the grammar — *select it on the B-row, shape it on the GP encoders, watch it on
the GP row* — so we can decide **by hand and ear** whether the whole grammar is worth rolling out.
This slice is the **GO/NO-GO for the entire operating model.**

---

## What already exists (verified in source, 2026-07-03) — G0 is mostly UI

The internal rack is **already real** — invariant 1 needs no work:

- `seq_processor_stack[SEQ_CORE_NUM_TRACKS][4]` of `seq_processor_slot_t` (`seq_core.c:179`,
  `seq_core.h:263`). Fixed slot map: **PITCH=0, CHORDMASK=1, TENSION=2, LIMIT=3**
  (`seq_core.c:1358`).
- Each slot already carries the **generic descriptor fields**: `id`, `enabled` (bypass),
  **`strength` (0..127, 0 = pass-through — the constraints-as-materials dial, already there)**,
  `bus`, `drum_mask` (`seq_core.h:263-270`).
- The renderer walks the stack and dispatches `switch(p->id)` (`SEQ_CORE_RenderTrack`,
  `seq_core.c:1597`/`:1671`), skipping `NONE`/disabled slots. A chord_mask-bearing track is
  auto-dirtied each tick because its input (bus chord) is live.
- ChordMask params today live in `tcc` (`chordmask_strength/bus/drum_l/h`) and are pushed to the
  slot by **`SEQ_CORE_ChordMaskSlotSync`** (`seq_core.c:1716`), triggered from the CC chokepoint
  **`SEQ_CC_Set`** (`seq_cc.c:496-528`, `sync_chordmask`). The 12-PC snap target is derived from
  the source **bus** notestack (`chord_mask_snap` / `chord_mask_render_range`, `seq_core.c:1049`).

**Consequence — the golden rule for this slice:** the grammar's encoders must **write through
`SEQ_CC_Set(track, SEQ_CC_CHORDMASK_*, val)`**, NOT poke the slot directly. That path already
updates `tcc`, re-syncs the slot, and raises render-dirty — so tcc, slot, and cache stay coherent
and nothing fights the existing sync. (Poking `seq_processor_stack` directly would be overwritten
on the next sync.)

The sel-view mechanism also exists (`seq_ui_sel_view` enum `seq_ui.h:182`; the `switch` in
`SEQ_UI_Button_DirectTrack`, `seq_ui.c:2515`). Adding `PROC` is one enum value + one case.

---

## Scope

**In (G0):**
1. A new **`SEQ_UI_SEL_VIEW_PROC`** — B-row becomes the track's 4-slot rack.
2. **OPERATE plane** — focusing a slot maps its params onto the **GP encoders** (write via
   `SEQ_CC_Set`). ChordMask: enc1 = strength, enc2 = source bus. Encoder-push = snap-to-default.
3. **CUSTOM plane (ChordMask only)** — the **12-PC mask painted on the GP button row**
   (display-only for G0: light the pitch classes in the live bus chord).
4. LCD labels for the focused processor (reuse `SEQ_CC_LABELS_Get` + shared `SEQ_LCD_Print*`).
5. A **provisional** PROC entry gesture (reversible; permanent home deferred — see Open choices).

**Out (later bundles):**
- CONFIG banks / `‹/›` paging (ChordMask has ≤ a handful of params — no overflow yet).
- Hand-*playable* mask (bus-derived-with-override) — §6.11, decided by ear after display works.
- TPD dashboard headline — that's B3; G0 uses the LCD.
- Migrating the other slots (Tension/Pitch/Limit) — that's **G1**, licensed only if G0 GOes.
- Freeing/renaming a permanent PROC button, SONG/PHRASE, tempo, etc. — separate bundles.

---

## Build steps

1. **`PROC` sel-view.** Add `SEQ_UI_SEL_VIEW_PROC` to the enum (`seq_ui.h`). Add its case to the
   B-row dispatch (`SEQ_UI_Button_DirectTrack`, `seq_ui.c:2515`): keys 1–4 = slots 0–3; tap =
   focus (`ui_focused_proc_slot`), double-tap = toggle `enabled` **via `SEQ_CC_Set`** so the sync
   fires. Keys 5–16 dark for now (room for a longer rack / an "add processor" affordance later).
2. **B-row LEDs (rack readout).** In the mapping-3 select-LED block: slot occupied (`id != NONE`)
   = green; focused = bright/amber; enabled vs bypassed distinguished (e.g. dim green = bypassed).
   Uses the native `select_leds_green/red` path (same as the keyboard work).
3. **Encoder OPERATE routing.** In `SEQ_UI_Encoder_Handler` global intercept (same site as the
   PHRASE morph-ride / keyboard-scroll, `seq_ui.c:3545-3565`): when `sel_view == PROC` and a slot
   is focused, map `encoder-1` → that processor's param list and call
   `SEQ_CC_Set(track, SEQ_CC_CHORDMASK_STRENGTH|_BUS, newval)`. Encoder **push** (the GP button
   edge) → snap-to-default (strength→0, bus→0) via the same setter.
4. **GP-row mask paint (CUSTOM).** Compute the live PC-set from the focused ChordMask's source bus
   (reuse the bus-chord read that feeds `chord_mask_snap`), light GP-row keys for those pitch
   classes (chromatic 1..12). Read-only in G0. This is invariant 4 + the first CUSTOM surface.
5. **LCD.** Focused-processor line: name (`SEQ_CC_LABELS_Get`), strength (`%3d`/bar), bus, and
   the mask as note names — reusing shared formatters.
6. **PROC home = the LIVE button** (decided 2026-07-03 — a clean steal). Repoint `SEQ_UI_Button_Live`
   (`seq_ui.c:1326`, `BUTTON_LIVE`/`LED_LIVE` = `M6C 1`) to **latch `sel_view = PROC`** —
   *latched, not held*: tap to enter processor mode, dwell (sculpt on the encoders), tap out
   (LIVE again, or any other sel-view button). Its dedicated LED becomes the "in PROC mode" lamp
   — a lamp the other sel-view buttons lack. Relocate the displaced function: pin
   `seq_record_options.FWD_MIDI` **on** as a permanent default (user never disables it; verified
   only two readers — `seq_blm.c` save/force/restore, `seq_core.c` step-record echo — both fine
   with always-on) and, if ever wanted back, park the toggle on the OPTIONS/record-options page.
7. **Build + flash + by-hand/ear session.**

---

## Plane model in G0

- **OPERATE** — enc1 strength, enc2 bus; GP row shows the mask; always available. ✔
- **CONFIG** — none needed yet (ChordMask fits one bank). The `‹/›` pager is stubbed, not wired.
- **CUSTOM** — the 12-PC mask paint *is* ChordMask's first custom surface. Toggle-into-custom is
  trivial here (the mask is always shown alongside OPERATE), so the **uniform CUSTOM toggle**
  gesture (§6.13) isn't forced until a processor needs a *separate* custom screen. G0 proves the
  concept; the toggle's exact gesture is a G1 decision.

---

## Open choices (decide at the bench, not on paper)

- **Permanent PROC button — DECIDED: the LIVE button** (`M6C 1`, with its own LED). Steal it;
  relocate `FWD_MIDI` to a permanent-on default. PROC is *latched* (tap-in/dwell/tap-out), unlike
  the held sel-views — the first payoff of the "sticky sel-view" idea. Minor: LIVE sits in the
  transport cluster, not with the other sel-view buttons — acceptable for a dwell-in mode.
- **Encoder fallback** (§6.12): what the 16 encoders mean when no slot is focused — current-page
  meaning, or the focused track's default dials. G0 can leave them page-default until a slot is
  focused.
- **Mask: display-only vs playable** (§6.11): G0 ships **display-only**; decide playability by ear.

---

## By-ear GO / NO-GO (the whole point)

**GO if:** selecting ChordMask on the B-row and sculpting strength/bus on the encoders — while the
12-PC mask reads back on the GP row — feels *more fluid and more legible than today's Track Mode
overlay*, and feels like playing an instrument rather than editing a menu. → license **G1** (migrate
Tension/Pitch/Limit and the FX processors onto the same grammar; collapse the page-scatter).

**NO-GO if:** the encoder-bank operation feels worse than the current page, or the rack selection
doesn't earn the B-row real estate. → cheap lesson; revisit the two bets before investing further.

**What G0 unlocks:** if it GOes, every other processor is "fill in the descriptor + point the
encoders at its params" — the dozen FX/mode/generator pages collapse into one learned movement.
```
