# Play-readiness safety net (bundle plan) — 2026-06-23

> Scaffolding. Archive/retire once executed into the design doc / code.
> Durable home for decisions = `doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md` §8/§9/§10.

## Why this bundle, why now

The capture-while-performing reliability arc is closed (recall-freeze cured 2026-06-22;
control-surface + LCD hangs fixed 2026-06-23, HIL 197/197). Both flat-map *walls* on the
reliability side are down. What remains on the §8 emergent queue is no longer reliability —
it's **play-readiness**: the capture·edit·process·capture·edit loop (the MVP, §8) is not
safely *performable* without a recovery net and legible modes.

User picked **"Safety net"** (2026-06-23) over self-bus / play-and-re-rank / push-first.
The bundle is §8 queue items **#2 (visible modes) + #4 (hold-polarity) + #3 (panic UNDO/REDO)**.

**Key decision (2026-06-23, by AskUserQuestion): "Make REVERT undoable."** REVERT snapshots
the live state right before it runs, so a mis-fired REVERT is recoverable via the panic net.
This **folds #4 into #3** — there is no separate "asymmetric hold" guard; the net covers it.
So the bundle collapses to **two phases**.

## Source map (verified against this fork's HEAD, 2026-06-23)

### Visible modes (#2)
- `seq_ui_sel_view` (`seq_ui.h`, enum `seq_ui_sel_view_t`, 9 states NONE/BOOKMARKS/STEPS/
  TRACKS/PAR/TRG/INS/MUTE/PHRASE; def `seq_ui.c`). Today: per-button select LEDs light
  *while the button is held* (`SEQ_UI_LED_Handler` region, `seq_ui.c`), reset to NONE on page
  change. **No LCD label, nothing persistent.**
- FREEZE: `seq_core_state.FREEZE:1` (`seq_core.h`), toggled in `SEQ_UI_Button_Freeze`
  (`seq_ui.c`), routed from `seq_hwcfg_button.metronome`. Today: METRONOME LED mirrors it
  (`SEQ_LED_PinSet(seq_hwcfg_led.metronome, ...)`) + a transient 1000ms "FROZEN/live" message.
  **No persistent on-screen indicator.**
- LCD: `SEQ_UI_LCD_Handler` (`seq_ui.c`) owns the global frame; each page has its own
  `LCD_Handler`. Need a spot that does not collide with page output.

### Unified UNDO/REDO (#3) — the three real one-deeps to consolidate
(2 more "undo-like" things exist but are NOT user one-deeps: capture-to-slot's internal
snapshot/restore is transient book-keeping; CHECKPOINT/REVERT is Tier-2 SD-backed and stays
separate per §10 — but its *pre-state* is what #4 wants captured.)

| # | Mechanism | Scope | Store | Size | Arm | Restore |
|---|-----------|-------|-------|------|-----|---------|
| 1 | Track undo | one track full | `track_undo` CCM (`seq_core.c`, `seq_core_track_undo_t`) | ~2.4 KB | destructive track verbs (PULL/copy/paste/clear) | `SEQ_CORE_TrackUndoRestore`, SELECT+CLEAR |
| 2 | Generator ENGAGE | one track par-buffer | `undo_slot` CCM (`seq_generator.c`, `seq_generator_undo_t`) | ~1 KB | first ENGAGE on a track | `SEQ_GENERATOR_Undo`, GP2 |
| 3 | Utility copy/paste/clear | one track full | `undo_*` buffers main RAM (`seq_ui_util.c`, `UNDO_ENABLED`) | ~1.5 KB | COPY/PASTE/CLEAR + layer edits | `UNDO_Track`, GP8 |

No REDO exists anywhere in the codebase.

### Hold-polarity (#4)
- `PHRASE_CAPTURE_HOLD_MS 1000` (`seq_ui.c`) — PHRASE-view GP: tap=recall(safe) / hold=capture(create).
- `ANCHOR_REVERT_HOLD_MS 1000` (`seq_ui.c`) — SELECT+BOOKMARK: tap=CHECKPOINT(bless,safe) /
  hold=REVERT(**destroy**). `SEQ_UI_Button_Bookmark` (`seq_ui.c`); guard = anchor-present
  check only, no recovery. Hazard: over-hold while blessing → wipes the jam.
- **Fix (folded into Phase 2):** before `SEQ_PATTERN_Revert()` runs, snapshot live → journal.

## The §10(a2) settled spec (the contract for Phase 2)

A small global **action journal**: each *deliberate* gesture pushes `{scope, before, after}`
onto a shallow ring. UNDO restores `before`; REDO re-applies `after`; any new deliberate
gesture clears the redo arm. **Reuse the existing snapshot stores** rather than invent new
ones — `track_undo`, `undo_slot`, the utility buffers consolidate behind one store / one
dispatcher. **Gate pushes on `!seq_generator_in_automutate`** (same flag as `phrase_drift`)
so ambient wander never lands on the stack. One UNDO + one REDO gesture (provisional / by-ear).
RAM: shallow, largely *reuses* the ~5 KB the three one-deeps already cost; keep off scarce CCM
if it grows. HIL: edit→UNDO→REDO byte-exact round-trip; injected generator wander between UNDO
and REDO must NOT pollute or invalidate the stack.

## Plan

### Phase 1 — Visible modes (#2)  — **DROPPED 2026-06-23 (user: "skip to Phase 2")**
Recon overturned the premise on the user's actual rig. The `lso` hwcfg has
`BLM8X8_DOUT_GP_MAPPING 3` → `selbuttons_available` true, so the dedicated select-view LEDs
(`LED_TRACK_SEL` / `LED_PAR_LAYER_SEL` / `LED_PHRASE` / …) are wired and light per `sel_view`;
`LED_METRONOME` is wired so FREEZE already has a persistent LED (lit = FROZEN); tapping PHRASE
*latches* the recall view (`TAKE_OVER_SEL_VIEW`) so `LED_PHRASE` stays lit on the recall row.
The modes are **not invisible on this rig** — only the off-axis co-location of FREEZE vs. the
recall row is a (small) gap. Not worth polish now. Revisit FREEZE-in-recall-LCD only if it
bugs by ear.

### Phase 2 — Unified UNDO/REDO net (#3 + #4)  [by-ear + HIL — the real net]

**The reachable-gesture fact:** on `lso`, `BUTTON_UNDO 0 0` is **unmapped**; `BUTTON_CLEAR`
is mapped → **SELECT+CLEAR is the live undo gesture today** (and `SEQ_UI_Button_Undo`'s
track→utility undo chain is dead on this rig). There is no physical UNDO button.

**Architecture — one RAM action journal (main RAM, keep off scarce CCM):**
```
typedef enum { JRNL_EMPTY, JRNL_UNDOABLE, JRNL_REDOABLE } journal_state_t;
typedef enum { JRNL_TRACK, JRNL_ORGANISM } journal_scope_t;     // ORGANISM = REVERT (2b)
struct {
  journal_state_t state;
  journal_scope_t scope;
  u8 track;                       // TRACK scope
  seq_core_track_undo_t before;   // ~2 KB  (reuse the existing struct shape)
  seq_core_track_undo_t after;    // ~2 KB  (captured lazily at UNDO time)
} action_journal;                  // ~4 KB main RAM
```
- **Lazy `after` (no per-gesture commit hook):** a deliberate gesture only **arms** — snapshot
  `before` ← live, `state = UNDOABLE`. **UNDO:** snapshot `after` ← live, restore `before` →
  live, `state = REDOABLE`. **REDO:** restore `after` → live, `state = UNDOABLE`. A new arm
  overwrites `before` and resets to `UNDOABLE` (redo arm cleared). One UNDO + one REDO deep.
- **Restore engine:** refactor `SEQ_CORE_TrackUndoRestore()` to take a
  `seq_core_track_undo_t *` so it restores from either `before` or `after` (it already does the
  full careful restore: CC replay, geometry init, bulk par/trg, generator restore, render,
  sustain cancel, PC/bank send, sync-to-measure, morph invalidate). `track_undo` becomes
  `action_journal.before`.
- **Wander cannot pollute:** only deliberate verbs (task context) call arm; automutate never
  does. (The `!seq_generator_in_automutate` invariant is structural here, not an explicit gate —
  assert it in the HIL pin.)

**Stage 2a — RAM track-grain journal + REDO + global gestures** (the heart):
1. Build `action_journal` + the lazy-after engine + the parameterized restore.
2. Repoint the deliberate **track-grain** arm sites to `SEQ_CORE_JournalArm(JRNL_TRACK, track)`:
   PULL (`SEQ_CORE_LoadTrackFromSlot`), utility copy/paste/clear (`seq_ui_util.c`), generator
   first-ENGAGE (`seq_generator.c`), capture-to-**track** (`SEQ_CORE_CaptureToTrack`, no undo
   today). Reclaim the generator `undo_slot` + utility `undo_*` buffers.
3. **UNDO gesture = SELECT+CLEAR** (keep muscle memory; route it to `JournalUndo`).
   **REDO gesture = provisional** (pick a free mapped combo during build; by-feel retune later).
4. **Behavior change to validate by ear:** generator undo shifts from "undo the whole
   engage-session" (old first-engage-arm) → "undo the last deliberate gesture" (one-deep per
   gesture). More uniform; CHECKPOINT/REVERT remains the whole-organism bail.
5. HIL: edit→UNDO→REDO byte-exact round-trip; injected generator wander between UNDO and REDO
   must NOT pollute/invalidate the journal (assert state survives a wander tick).

**Stage 2b — REVERT-undoable (#4) + capture-to-slot** (the SD/organism scope):
6. **REVERT-undoable:** before `SEQ_PATTERN_Revert()` reads the CHECKPOINT anchor (slot 0),
   `SnapshotWrite` the live organism to a **pre-revert** slot in the same `ANCHOR_BANK`
   (e.g. base_pattern 4, mirroring the phrase `4*n` layout). Journal scope = `JRNL_ORGANISM`;
   UNDO `SnapshotRead`s the pre-revert slot, REDO re-reads the checkpoint (slot 0). The extra
   ~1 s pre-write is acceptable for a rare panic gesture (control-surface stays live per the
   2026-06-23 fix). REVERT stays Tier-2/SD; only its dispatch joins the Tier-1 gesture.
7. **capture-to-slot** (SD-victim `kind`): defer unless cheap — capture-to-track (RAM) already
   covers the common live case in 2a.
8. By-ear: jam → REVERT (or capture) → UNDO restores the jam → REDO re-applies.

**RAM accounting (measure at build):** +~4 KB main (journal before+after) − ~1.5 KB main
(utility buffers) − ~2.4 KB CCM (`track_undo` → main) − ~1 KB CCM (generator `undo_slot`).
Net: CCM *freed* ~3.4 KB; main +~2.5 KB. Re-measure the build-diff; main has ~9 KB free.

---

## Stage 2a — AS BUILT (2026-06-23, compiles clean, awaiting flash)

Built and compiling (0 warnings). **Measured RAM:** main free **6088 B** (~5.9 KB; was ~9 KB
— the journal lives in main per the "keep off CCM" guidance), CCM free **12160 B** (~11.9 KB;
was ~8.6 KB — `track_undo` + generator `undo_slot` reclaimed). The morph-flag (`PHRASE_MORPH=0`,
+6.5 KB main) and moving `after`→CCM remain levers if main gets tight.

**Engine** (`seq_core.c`): `action_journal {state, before, after}` in main RAM; `journal_snap`
/ `journal_restore` (parameterized from the old `track_undo` body); `SEQ_CORE_JournalArm /
Undo / Redo / Invalidate / InfoGet`. **Lazy `after`**: undo snapshots live→after then restores
before; redo restores after. `SEQ_CORE_TrackUndoSnapLive/Restore` kept as wrappers (Restore =
one-shot rollback for the pull's SD-fail path; the user undo is JournalUndo).

**Consolidated arms** (all → `SEQ_CORE_JournalArm`): pull (`SEQ_CORE_TrackUndoSnapLive`),
utility (`SEQ_UI_UTIL_UndoUpdate`), generator ENGAGE (top of `SEQ_GENERATOR_Engage`, before any
mutation), capture-to-track. Old stores deleted. Undo entry points (`SEQ_GENERATOR_Undo`,
`UNDO_Track`) → `SEQ_CORE_JournalUndo`. Invalidate on disk-load (via `SEQ_GENERATOR_UndoInvalidate`
→ `SEQ_CORE_JournalInvalidate`) and on harness reset.

**Gesture:** SELECT+CLEAR is the **toggle** — UNDOABLE→undo→REDOABLE→redo→UNDOABLE. EMPTY →
"nothing to undo", never clears (safety invariant intact). The (unmapped) UNDO button mirrors it.

**Behavior changes to validate by ear** (flagged):
1. **SELECT+CLEAR is now a toggle** — a second press *redoes* (was a harmless no-op). The
   never-destructively-clears safety still holds structurally.
2. **One shared one-deep** across pull/utility/generator/capture — the most recent gesture is
   the undoable one (resolves the old pull-vs-edit arbitration gap; you can't separately undo a
   pull *and* a later generator engage — only the latest).
3. **Generator UNDO** now restores the full pre-ENGAGE track (incl. removing the seeded gen) via
   the journal, instead of the old par-only + disengage-loop. Same observable effect.

**HIL** (`test_undo_redo.py` new + realigned `test_pull_gesture` / `test_track_load`):
byte-exact undo→redo (capture gesture), SELECT+CLEAR toggle, EMPTY-never-clears, wander-between-
undo-and-redo non-pollution. New verb `CMD_TRACK_REDO 0x47`; query middle field repurposed
kind→journal-state.

### Stage 2a — by-ear GO + adversarial review + fixes (2026-06-23)

**By-ear GO** (user, on the first flash): "working great, gesture feels good" — SELECT+CLEAR
toggle confirmed, no separate REDO combo wanted.

**Adversarial review** (5-lens workflow, 30 agents, refute-by-default verify): 22 confirmed
findings (3 refuted). Caught FOUR real issues the green suite missed, all now fixed:
- **A (must)** — the *real* live CAPTURE gesture `SEQ_CORE_CaptureSpan` (UTILITY-hold grab) wasn't
  armed; I'd armed only the testctrl-only `SEQ_CORE_CaptureToTrack`. Worse, no invalidate either,
  so a post-capture SELECT+CLEAR would revert the *prior* gesture and discard the grab. **Fixed:**
  arm `SEQ_CORE_JournalArm(dst)` in both `CaptureSpanReSim` + `CaptureSpanTape`, after all refusal
  returns, before `CaptureSpanPrepDst`.
- **B (must)** — the ENGAGE arm sat at the top of `SEQ_GENERATOR_Engage`, before the alloc check,
  so a pool-full (-1) or idempotent re-engage clobbered a valid prior undo. **Fixed:** arm only on
  the new-allocation-success path (after `alloc_slot()`, before `g` is marked in_use/track-set so
  the snapshot excludes the new gen) — the pre-consolidation invariant.
- **C (must)** — journal placed in main RAM on a *backwards* premise; main is the MSP-gated tight
  region (6088 B free) and CCM had 2× headroom. **Fixed:** `CCM_SECTION` (named type — the attr is
  silently dropped on an anonymous `struct{}`). Verified in the ELF: **main 6088→10768 B free**,
  CCM 12160→7480.
- **D (must)** — `SEQ_CORE_JournalRedo` restored `after` without snapshotting live first, so
  undo→hand-edit→redo silently destroyed the edit. **Fixed:** symmetric — snapshot `before`←live
  first, making SELECT+CLEAR a true reversible 2-way swap (nothing is ever silently lost).
- **Cleanup:** 3 stale comments fixed; orphaned `SEQ_CORE_TrackUndoInfoGet` + `kind` field +
  `SEQ_CORE_TRACK_UNDO_KIND_LIVE` + dead `UNDO_ENABLED` deleted; **>4-generator arm guard** added
  (refuse-arm-leave-EMPTY, mirrors the capture-ring overflow guard) so a restore can't silently
  delete generators 5..16.
- **Refuted (3):** "redo clobbers un-journaled edits = bug" (it's blessed §10(a2) semantics, and D
  makes it reversible anyway); "GP8/GP2 misleading message = regression" (pre-existing/incorrect);
  "UNDO_ENABLED dead = bug" (cosmetic, cleaned anyway).
- **Accepted (documented, not fixed):** the unguarded task-context `journal_snap` (a +4 wander can
  tear the `after`/`before` lazy snapshot) — pre-existing contract, self-healing (restore re-renders
  from the generator loop), matches the "reproducible & musical, not bit-identical" standard.

**5 new HIL pins** closed the gaps: symmetric-redo-preserves-edit, generator-engage-undo-redo
byte-exact, capture-span-gesture-undoable (the pin that would've caught A), pattern-load-invalidate,
cross-gesture one-deep clobber. (Pool-full/re-engage *negative* arming is structurally guaranteed
by the arm placement + review-verified; not driven by HIL — GP1-second-press is ROLL not re-engage.)

**Full HIL 206/206 green** after re-flash. **Stage 2a COMPLETE** — ready to commit.

## Discipline notes
- POC code disposable; nothing stays backward-compatible.
- Ship Phase 1 standalone (build less, listen sooner). Don't gate it behind Phase 2.
- RAM: Phase 1 ~0; Phase 2 should be net-neutral-to-negative (collapsing 3 stores). Re-measure
  the build-diff; CCM is the scarce region (§A5, ~9 KB).
- Fold outcomes into design §8 (queue items resolved), §9 (decisions), §10(a2) (mark built),
  glossary (UNDO/REDO), REFERENCE (symbols/verbs), MANUAL (the gesture).
