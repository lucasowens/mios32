# Live-surface hardening — 2026-06-14

**Status:** in progress. Round picked after the 2026-06-14 state assessment (14-agent
review of all docs + fork code). Scope confirmed: **full round (Tiers 1–3)**.

## Why this round (and why now, mid-POC)

The save-model lineage (RECOMBINE → FEARLESS → PHRASES Stage B) put a lot of new
performance gesture surface on top of the engine, and the assessment found the live
gesture layer is the POC-fragile part. One finding is a **silent destructive-overwrite
footgun reachable by ordinary phrase use** — that is on a bundle's critical path (it can
ruin a live take), so a defensive pass is licensed under §2.7 before more music goes on
top. We also bank the assessment's gap/idea register so nothing is lost.

Discipline: verify each platform-internals claim against *this fork's* source as the
first step of each fix (CLAUDE.md). HIL tests are authored here; execution + by-ear GO
is the user's (hardware-bound).

## Tier 1 — live-set safety

1. **Phantom-pull fix** *(VERIFIED real)*
   - The PULL arm at `seq_ui.c` (sole-press → `pull_held_track = sel_button`) runs with
     no `sel_view` guard. In PHRASE view a phrase tap also arms the pull; the release
     does not disarm, so a later top-row GP press fires a real bar-aligned
     `SEQ_CORE_LoadTrackFromSlot` the user never intended. Only PATTERN-press or RESET
     clears it. Known bug-class: the PATTERN path already disarms with the comment
     "phantom pulls on later GP presses" (`seq_ui.c:1874`) — the PHRASE sibling path was
     never given the same disarm.
   - **Fix:** gate the PULL arm/commit on `sel_view == SEQ_UI_SELVIEW_*TRACKS*` (mirror
     the PATTERN-path disarm). Also closes the secondary phrase-vs-pull shadowing.
   - **Test:** extend `tests/apps/seq_v4/test_pull_gesture.py` — assert a phrase tap in
     PHRASE view does NOT arm a pull / a later GP press does not load a track.

2. **testctrl production footgun** *(VERIFIED: no `#ifdef`, in Makefile:79, called from
   app.c:155/265/883, reachable from every MIDI port behind a 6-byte header)*
   - **Fix:** add `SEQ_TESTCTRL_ENABLE`, **default ON** so the current HIL workflow is
     unchanged; wrap the `app.c` call sites (+ a Makefile target) so the *gig build*
     flips it off. Removes the footgun for release at near-zero churn now.
   - **Verify:** both flavors compile; HIL build still exposes the surface.

## Tier 2 — silent corruption / lost work

3. **Partial-capture occupancy** *(verify first)* — make a half-written phrase refuse
   recall instead of returning truncated bytes. Either validate all 4 group records at
   probe time or write a per-phrase "complete" marker last and check it on probe/recall.
   HIL round-trip test.
4. **Auto-undo vs pattern-load** *(verify first)* — clear/invalidate the one-deep undo
   snapshot when a pattern loads into that group, so UNDO can't clobber the loaded track.
5. **SHADE persistence** *(VERIFIED missing)* — `seq_ui_gravity.c:189` sets
   `ui_store_file_required=1` but installs no exit callback (unlike 10+ peer pages), so
   SHADE never reaches config. Default fix: add the exit callback (honor the comment).
   *Open micro-fork:* if SHADE should be boot-fresh performance state instead, fix the
   comment — brushes the §10 GRAVITY-persistence question.

## Tier 3 — capture the gaps + housekeeping

6. **File the register** — untracked gaps + LOW cluster from the assessment into design
   §10 / TODO_TRIAGE so nothing is lost.
7. **Doc housekeeping** — design front-matter ("design phase"→shipped) + stale §11
   glossary entries; REFERENCE masthead date + the *retired* stall TODO still listed open
   in §6; add a **MANUAL FREEZE section** (a performer can't currently learn to engage it
   — it's the repurposed METRONOME button); fix the stale testctrl GRIP comment; update
   the auto-memory "not pushed" note.
8. **Retire executed plans** — freeze-faithfulness, tension-workbench, pitch-chain-
   migration, save-model, ableton-rosetta. *First* confirm the orphan items are folded
   into the design doc: groove-phase caveat + multitimbral re-route (freeze plan), Torso
   T-1 TEMP gesture (save-model plan). Keep phrases-snapshot (Stage C unbuilt) and
   test-session-rig (Phase 2/3 backlog live).

## Deferred (filed, not built)

- **L1 transient-SD distinction** — stays §10 "build only if it bites."
- LOW IRQ-guard asymmetries / FREEZE-hold-stick-if-MENU / name-stamp-on-write-fail /
  bipolar clamp 64-vs-63 — filed into TODO_TRIAGE, fix opportunistically.

## The full gap/idea register (source: 2026-06-14 assessment)

Banked here so Tier 3.6 has a single source. Correctness risks ranked HIGH/MED/LOW;
ideas and open questions follow. (See the assessment for full prose.)

### Correctness risks
- HIGH — phantom destructive pull in PHRASE view → Tier 1.1.
- MED — partial-capture false-positive occupancy → Tier 2.3.
- MED — transient-SD probe failure stamps EMPTY over real records → deferred (§10 L1).
- MED — SHADE not persisted → Tier 2.5.
- MED — testctrl production footgun → Tier 1.2.
- MED — auto-undo not invalidated by pattern load → Tier 2.4.
- MED — secondary phrase-vs-pull shadowing → folded into Tier 1.1.
- MED — AUTOTEST fixtures doc-as-contract (only CONTENTS.md committed; baselines on one
  SD card) → test-session-rig Phase 2/3 (kept plan).
- LOW — SnapshotRead marks all groups dirty+loaded even if one PatternRead failed;
  `phrase_present_mask` updated outside IRQ-guard; FREEZE hold sticks if MENU grabbed
  mid-hold; FREEZE read unguarded vs UI RMW (1-tick-stale, self-corrects); capture
  name-stamp inherits A-group name on write-fail; GRAVITY bipolar clamp 64-vs-63;
  PhraseNameCommit relies on undocumented probe-on-every-load invariant.

### Ideas
- Phrase morphing — POSTURE (continuous knob, tiny RAM) vs NOTE-CONTENT (grid crossfade,
  RAM-gated). Steer: morph posture continuously, swap grid discretely.
- TERRAIN-HANDS — row-as-tension-meter + chord-hand select-row writing the bus notestack.
- THE TAPE — append-only quick-capture that never aims/overwrites/blocks (storage fork open).
- Robotize → render-stack processor — biggest remaining bounce-north-star piece.
- Small undo ring (2–3 deep) instead of one-deep-global.
- Validate-all-4-groups / per-phrase complete flag (→ Tier 2.3).
- Output-timing: emit 0xF8 clock from the TIM2 IRQ (highest-value jitter fix); zero-code
  mitigation = route clock + timing-critical tracks over DIN not USB.
- Librarian/curation page (maybe just a tape browser + promote verb).
- Depth-vs-grip split for GRAVITY (only if the ear asks).
- testctrl static-assert on the generator_query reply buffer bound (234B, hand-computed).

### Open questions still live (§10)
Tape storage mechanism · phrase/song format evolution (record-version bump vs new file)
· editable-waypoints recall · GRAVITY value persistence · §5.5 same-buffer concurrency
· STOP-mid-RESOLVE landing · drum-track pitch-chain migration · the EDIT-LCD-vs-tick
gate-read 3-in-8 puzzle (root cause never found).

### Note on PHRASES Stage C
Not a clean "finish the bundle": the *sparse-recall* half is unblocked, but the
*song-arrangement* half depends on the unresolved phrase/song format fork (§5).
