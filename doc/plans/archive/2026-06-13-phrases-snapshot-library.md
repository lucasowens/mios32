# PHRASES — the snapshot library ("a set is a path")

## Context

Next bundle after FEARLESS SWITCHING. The design (§9 2026-06-11, "an organism is a
phrase") promotes phrase mode to *the* scene system: navigate a sparse set of
whole-organism **waypoints** fearlessly, rig + living generators follow. Invariants
that must survive: **faithfulness (heard = saved)** and **deterministic
returnability**.

**Key discovery during planning (resolves the §10 "record-version bump vs new file"
fork):** the firmware has **no per-track provenance**, and auto-writeback is
**group-grain**. So the literal design sketch — a phrase = 16 heterogeneous
`{bank,pattern,section}` refs in an `MBSEQ_S.V4` record-version bump — *cannot be
faithful* without committing material anyway, and would drift exactly the way Hapax
does (the failure the design wants to dodge). User confirmed the pivot to the **new
file / snapshot** option.

**The pivot:** a phrase = a **whole-organism committed snapshot** in a sentinel
phrase bank, *generalizing CHECKPOINT/REVERT from one anchor to N named slots*.
Faithful (par+trg+CC+**generators** all round-trip via the V4 ext tag), drift-free,
no `MBSEQ_S.V4` churn, and reuses the code shipped yesterday. Track-grain
recombination ("Tuesday's kick + that bass") is unchanged — it happens **live via the
RECOMBINE pull**, *then* you snapshot the result as a phrase. Selective/sparse recall
is a near-free enable-mask in a later stage.

Capture = 4× `PatternWrite` (= CHECKPOINT). Recall = 4× `PatternRead` batched +
census fan (= REVERT). **Same stall class REVERT already passed by ear** — not the
16-scattered-read path that was feared.

Discipline note: user chose the heavy/complete path over scout-first, so this is
staged with the **by-ear GO gate at the end of Stage A** (smallest playable loop:
capture a few phrases, navigate them live) before any UI polish.

## Architecture

- **Storage:** new sentinel bank `SEQ_FILE_B_PHRASE_BANK 0xfd` (mirrors
  `ANCHOR_BANK 0xfe`), file `SESSIONS/<session>/MBSEQ_PH.V4`. A 64-pattern bank holds
  **16 phrases × 4 group-records**: phrase N → patterns `4N .. 4N+3`. Reuses the bank
  record serializer wholesale → generators ride the V4 ext tag for free; stays
  outside every `for(bank<NUM_BANKS)` loop → never auto-loaded/saved/navigable,
  survives session writeback untouched (the proven anchor properties).
- **Capture** `SEQ_PATTERN_PhraseCapture(u8 n)` = `Checkpoint()` parameterized:
  lazy-create+open `MBSEQ_PH.V4`, write groups 0..3 → patterns `4N+group` ascending.
  Reads live only, leaves `seq_pattern_dirty` untouched.
- **Recall** `SEQ_PATTERN_PhraseRecall(u8 n)` = `Revert()` parameterized: re-open vs
  current session (refuse if phrase absent — no stale read), forward-delay if running,
  ONE `MUTEX_SDCARD_TAKE`+critical over 4× `PatternRead(PHRASE_BANK, 4N+g, g, 0)`
  (NOT `SEQ_PATTERN_Load` — keep `seq_pattern[group]` pointing at the real working
  slot), then the 16-track census fan (forced render + sustain-cancel + PC/bank send +
  `ManualSynchToMeasure(0xffff)`), then set all groups dirty+loaded LAST (the
  inversion: next switch writes the recalled state into the working slots).
- **Occupancy** `SEQ_PATTERN_PhrasePresent(u8 n)` — phrase exists if its records are
  written. Cheapest: a 16-bit RAM occupancy mask, seeded once on session load by
  probing the phrase bank (or lazily: a phrase becomes "present" after first capture
  this session). Plus a `last_recalled_phrase` byte for the "current" LED.

## Stages

### Stage A — Hear the set (by-ear GO gate)
1. `seq_file_b.c` / `.h`: add `PHRASE_BANK 0xfd`, `seq_file_phr_info`, `MBSEQ_PH.V4`
   in `SEQ_FILE_B_InfoPtr` + `SEQ_FILE_B_BuildPath` (≈10 lines, mirror the anchor).
   **Verify at build:** `SEQ_FILE_B_Create` makes a full 64-pattern bank for the
   sentinel path (needed so 16 phrases fit) — confirm against source.
2. `seq_pattern.c` / `.h`: `SEQ_PATTERN_PhraseCapture(n)`, `PhraseRecall(n)`,
   `PhrasePresent(n)` by generalizing the existing `Checkpoint`/`Revert`/
   `AnchorPresent` (factor a shared helper taking `(bank, base_pattern)` so anchor =
   `(0xfe,0)` and phrase N = `(0xfd,4N)` — avoids divergent copies).
3. Gesture (provisional, tune with hardware in hand per FEARLESS precedent): wire onto
   the existing **PHRASE select-view** (`SEQ_UI_SEL_VIEW_PHRASE`, held-modifier,
   row-mode-ownership clean). **Tap GP n = recall phrase n** (replaces the current
   song-pos fetch on that view); **long-hold GP n ≥1s = capture to phrase n**
   (destructive verb gets the deliberate hold, mirroring CHECKPOINT-tap/REVERT-hold
   and SELECT+CLEAR). Refuse recall of an empty phrase (flash). Note: this overloads
   the PHRASE view, which today drives `SEQ_SONG_PosSet`; classic song-step editing
   stays on the SONG page untouched. Coexistence/naming is a deliberate choice, not an
   accident — flag for by-ear confirmation.
4. testctrl verbs `PHRASE_CAPTURE` / `PHRASE_RECALL` / `PHRASE_PRESENT` (+ status
   codes distinguishing I/O-fail from empty-refuse, like REVERT's 0x01 vs 0x03);
   `board.py` wrappers; HIL pins in `test_phrases.py`: byte-faithful
   capture→jam→recall incl generators; recall sets all-dirty + is bar-aligned; refuse
   empty; capture leaves the 4 user banks untouched; phrase survives a session
   writeback. Use a `genv4`-style fixture (V4-sized records) for gen-faithful pins.
5. **GO/NO-GO by ear:** build a small set of organism-phrases (recombine live with
   RECOMBINE pulls if wanted, then capture), navigate them live — fearless,
   rig+generators follow, organism comes back alive. Does *a set is a path* feel like
   the instrument? If NO-GO, the bundle reshapes here before any UI investment.

### Stage B — Perform it (two-face + state LEDs)

> **STATUS 2026-06-13 (SHIPPED + by-ear GO, HIL 143/143; design §9 2026-06-13):** cross-session
> occupancy probe DONE — occupancy re-seeds from disk on session load
> (`SEQ_PATTERN_ProbePhrasesOnLoad`; probe-by-content with capture-time EMPTY markers, no format
> change). Also: recall "never lose work" — phrase recall writes back dirty groups first (phrases
> stay immutable; nudge recoverable via pattern-switch).
>
> **STATUS 2026-06-14 — STAGE B COMPLETE (SHIPPED + by-ear/by-eye GO, HIL 148/148; design §9
> 2026-06-14).** The three remaining items DONE:
> - **drift/drift LED** — `phrase_drift` (per-group, distinct from `seq_pattern_dirty`), set at the
>   `DirtySetTrack` chokepoint but GATED to EXCLUDE the generator's ambient auto-mutate
>   (`seq_generator_in_automutate` around `SEQ_GENERATOR_Tick`'s write) — **user decision: drift = MY
>   edits, not wandering**. Cleared at recall/capture/probe/reset tails (after CC-replay).
>   `SEQ_PATTERN_PhraseDrifted()` winks the current waypoint amber↔green on `ui_cursor_flash`.
>   Resolves the design-§10 "drifted-since-recall signal" thread (now unblocks the editable-waypoints
>   pivot — still a choice).
> - **naming** — full keypad (`SEQ_UI_KeyPad_*`) in a global modal over the PHRASE view; persisted
>   FREE in the base (group-0) record name (`SEQ_FILE_B_PhraseWriteName` + the occupancy probe extended
>   to re-seed names); `SEQ_PATTERN_PhraseName`/`PhraseNameCommit`; blank ⇒ shows the number.
>   Provisional gesture: hold-capture opens the namer (decouple later).
> - **capture-confirmation** — enriched to `PHn <name>` / `Phrase N`; capture also sets the current LED.
>
> Verbs folded onto `CMD_PHRASE_META 0x7f` (last free 7-bit opcode). **Only Stage C remains.**

- **Two-face recall:** FREEZE-held = frozen tape (clear the recalled tracks'
  generators on recall — reuse FEARLESS Stage-B `SEQ_GENERATOR_TrackClear` path);
  tap = posture (default — generators resume engaged, already what Revert does).
- **Row LEDs** on the PHRASE view: occupied / current (`last_recalled_phrase`) /
  dirty (any group dirty since last capture/recall). Honor the brightness/blink
  conventions; no free-floating global state.
- Phrase **naming** (reuse the per-pattern 20-char name in the phrase bank record);
  capture confirmation message.

### Stage C — Recombine & arrange (fast-follow, post-GO)
- **Per-phrase enable mask** → sparse recall ("swap lead+bass, keep my drums"):
  recall loads only enabled tracks of the snapshot. Tiny phrase-meta block (RAM +
  small session file, or piggyback an unused header field).
- Optional: classic song-mode steps reference phrase indices (the arrangement layer —
  Tempo/Loop/Mutes/jumps survive as-is), wiring "a set is a path" into song mode.

## Inherited gotchas (from FEARLESS — apply, don't relearn)
- 8.3 / FatFs `_USE_LFN=0`: file base ≤8 chars → `MBSEQ_PH` is fine.
- Re-open vs **current session** before every phrase op (cached valid bit from a prior
  session reads the wrong file).
- Recall must NOT use `SEQ_PATTERN_Load` (would repoint the working slot at the phrase
  bank); read straight into live RAM via `PatternRead`, like REVERT.
- Set `pattern_loaded`/`dirty` **last**, after the census fan.
- Accepted POC cost (parity with CHECKPOINT/writeback): a mid-op SD failure leaves a
  partial phrase; surface as negative status; atomic temp+rename if it ever bites.
- Sentinel must stay OUTSIDE `0..NUM_BANKS-1` so `SaveAllBanks`/`LoadAll`/UI-nav skip
  it (the reason it's a sentinel, not a `NUM_BANKS` bump).

## Critical files
- `core/seq_file_b.c`, `core/seq_file_b.h` — sentinel phrase bank plumbing.
- `core/seq_pattern.c`, `core/seq_pattern.h` — capture/recall/present (generalize
  `Checkpoint`/`Revert`/`AnchorPresent`).
- `core/seq_ui.c` (`SEQ_UI_SEL_VIEW_PHRASE` handler ~`2252`, `SEQ_UI_Button_Phrase`),
  `core/seq_ui_song.c` — gesture + LEDs.
- `core/seq_ui_seq.c` / testctrl handler + `testing/.../board.py`,
  `testing/.../test_phrases.py` — harness.
- Reuse: `SEQ_FILE_B_PatternWrite/Read`, `SEQ_CORE_RenderTrack`,
  `CancelSustainedNotes`, `SEQ_LAYER_SendPCBankValues`, `ManualSynchToMeasure`,
  `SEQ_GENERATOR_TrackClear` — all already exercised by Checkpoint/Revert.

## Docs (on execution, per CLAUDE.md)
- Create `doc/plans/2026-06-13-phrases-snapshot-library.md` (this plan, durable copy).
- On GO: fold into design doc §9 (resolve the §10 phrase-format fork = "new file /
  snapshot, generalizes CHECKPOINT"), REFERENCE (phrase bank + verbs), MANUAL
  ("Phrases / the set is a path").

## Verification
- Build via the canonical bash-source-me path; flash via MIOS Studio (reference
  memory `reference-build-and-flash`).
- HIL: `test_phrases.py` green + full suite no-regression (FEARLESS = 135/135 floor).
- By-ear GO gate at end of Stage A (above) is the real acceptance test; Stages B/C
  each get their own by-ear pass. SD sessions are disposable (overwrite freely; keep
  AUTOTEST A1–A3).
