# MBSEQ V4 — Decisions Log (dated chronology)

The dated session-by-session decision record of the generative-platform fork,
moved verbatim out of `MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md` §9 on 2026-07-11
(the design doc keeps the curated "decisions in force" groups plus a one-line
index of the blocks below).

**Convention:** append-only. A new session's decision block is APPENDED HERE
(same format: `**YYYY-MM-DD — title (status)**` + decision-sized bullets), and
ONE index line is added to design-doc §9's chronology index. Revise an old
entry only with a dated note, never by rewriting it.

---

**Tension Workbench (2026-06-09) — direction confirmed with the user; build gated on
a workflow-level GO/NO-GO by ear.** *(Convention note, also adopted this date: new §9
entries stay decision-sized; build narratives go to the reference doc — §9 had
drifted toward a build journal, burying the multi-session spine it exists to be.)*
- **§2.7 adopted: the unit of validation is a workflow, not a feature.** A dial
  without its companions under-reads by ear; a false NO-GO from an incomplete rig
  costs as much as unheard infrastructure. Corollary: infrastructure is licensed when
  on a bundle's critical path. (User explicitly licensed major rewrites, including
  rewrites that set up future features.)
- **Force-to-tension = the GRAVITY field; the Tension Workbench is the next build.**
  Bipolar render-stack processor over a live stability ladder (root / fifth /
  bus-chord / scale / chromatic-rub / slip); pull ends in drone collapse, push sweeps
  LEAN → RUB → SLIP; push *constrains* (structured wrongness), never randomizes; RUB
  tones neighbor chord tones so the release gesture is cadential. Global value +
  per-track GRIP (mirrors the FTS global pattern); RESOLVE = bar-quantized return;
  SHADE = brightness-ladder terrain. Subsumes FTS and chord-mask-at-max as knob
  regions. See §8 second build + the plan file for the full model.
- **Deterministic-by-construction processors.** New processors gate with
  `hash(track, instr, step, zone)`, not live RNG: same position = same notes =
  returnable states (§1), exact HIL assertions, and a shrinking "random shapers stay
  reset" freeze carve-out. The chord mask's per-render re-roll is **not** carried
  forward (migrate when next touched).
- **Born-as-processors rule (§3) + pitch-chain migration as Track 2.** New musical
  transforms are born in the render stack; emission-time effects are legacy (each one
  is invisible to `OutputActive` and forces bake code at bounce). Track 2, gated on
  the workbench GO: migrate transpose → force-scale → limit into the stack
  *together* (entangled — snapping after transpose is what keeps bus-planing
  in-scale), deleting `SEQ_CORE_BakeForceScale` rather than extending the bake
  program; implicit-dirty on transposer change mirrors chord-mask; TRKMODE/FTS UX
  via the phase-C slot-sync bridge; existing capture HIL = regression net.
- **ext-CC persistence fix promoted into the bundle.** The parked 0x96 bug breaks the
  workflow under test (a knob that resets on pattern recall kills
  sculpt→capture→return) — and source inspection 2026-06-09 shows it is **wider than
  recorded**: `SEQ_FILE_B_TRK_EXT_CC_LAST = 0x95` while the chord-mask CCs occupy
  **0x96–0x99** (strength, bus, drum-mask L/H) — all four reset on reload. Fix:
  extend the persisted ext-CC range to a clean boundary (e.g. 0x9F) behind a new ext
  tag (read path already dispatches on tag, old patterns stay loadable); GRIP's new
  CC lands inside the extended range. Independent of the larger v3 *format* work
  (processor/generator posture), which stays deferred.

**2026-06-10 — Track 2 BUILT (pitch-chain migration; HIL 108/108)**
- **Fences instead of total migration.** Three event classes keep the legacy
  emission chain behind a per-event `legacy_pitch` gate: Arpeggiator playmode
  (multi-arp cycles `t->arp_pos` per emission — runtime state a step-deterministic
  render can't reproduce; **user call 2026-06-10**, migrate-when-touched), Drum
  event mode (a 0 Note byte falls back to the per-drum lay_const note and still
  plays — mirror-rest silence is wrong there; fenced until drum pitch gets its own
  design pass), and Chord par layers (the par byte is a chord index; pitch only
  exists post-expansion).
- **Slot order is the musical statement:** PITCH(0) → CHORD_MASK(1) → TENSION(2) →
  LIMIT(3). Tidy/plane first, the chord wins over the scale, tension pushes last
  and survives, the limit folds the result. Deliberate behavior change vs the old
  emission order (FTS used to re-snap *after* the chord mask): **the POC rule
  ("FTS off on gripped tracks") is retired** — pinned at emission by HIL.
- **Capture-while-planed is faithful** — the migration's headline musical win. The
  deleted bake only ever reproduced static Normal-mode transpose; now the mirror
  holds plane+snap+fold and capture is a plain copy + the existing generative
  reset. The per-effect bake program is over.
- **Emission note-mutators get a narrow carve-out, not a migration.** Humanize-note
  / LFO-note re-snap + re-fold at emission IFF the mutator actually moved the note
  (stack output, including a push, passes through untouched). Robotize needed
  nothing — it already walks scale degrees under FORCE_SCALE. Echo keeps its
  per-repeat re-snap (pushed-note echoes resolve into the terrain — accepted,
  arguably musical).
- **Dirty model:** PITCH joins the per-tick implicit dirty only when live-varying
  (Transpose playmode / global transpose); static transpose/FTS/LIMIT render on
  events, with change-guarded `RenderDirtySetAll` hooks at every global
  scale/root/keyb-root write site. Direct-tcc writers (track-preset import,
  CaptureToSlotTrack restore) must re-run the four slot syncs — two such bypasses
  were found by review and fixed.
- **STOP lands an in-flight RESOLVE at 0** (boundary semantics, matching
  stopped-RESOLVE's instant snap). Fixes a latent Track-1 bug: the glide reaches 0
  before the downbeat by design, and stopping in that window stranded
  `tension_resolve_active`, silently zeroing GRAVITY at the next play's downbeat.
  By-ear alternative if landing feels wrong: freeze at mid-ramp.
- **Accepted edges** (each documented at the code site): transpose-note-0 ("play
  C-2") unrepresentable in the mirror; transposer-no-key writes rests
  (bounce-visible silence; a dedicated emission branch still releases held
  glides); morph blends already-pitched values (an in-scale blend can pass
  off-scale — decide by ear); nth-trigger no_fx can't escape the stack LIMIT
  (bar-dependent; the per-step no_fx TRG layer still does); PC/AT values are NOT
  shifted in CC mode (the legacy "shift" wrote a don't-care byte — never audible);
  CC/PB dedupe compares post-pitch values (constant CC lanes re-send on plane
  changes); edit-page note display always shows the heard pitch
  (PRINT_TRANSPOSED_NOTES re-apply removed — it would double-transpose).

**2026-06-11 — Save model, groups, phrases (direction reversals, user-confirmed;
mechanisms provisional — see `doc/plans/2026-06-11-save-model-groups-performing-curating.md`)**
- **The save model inverts** (reverses §6 "Save/recall (intentional)"). The working
  state always persists — auto-writeback of a dirty group on pattern switch;
  protection becomes the explicit act (**CHECKPOINT** = bless an anchor, **REVERT** =
  one gesture back). Rationale: manual-save's protection costs a gesture exactly when
  attention is zero, so the loss mode was the default path. User's call: lose-on-
  switch "leads to constant losses"; the PATTERN-hold model "is more useful because
  it's auto saved." **Recall = relaunch/regenerate survives unchanged.** Mechanism
  (writeback hook, checkpoint storage, stall-race precondition, gen-state tag) is
  design-ahead, not committed.
  *(SHIPPED as the FEARLESS SWITCHING bundle — by-ear hard GO 2026-06-13, HIL
  135/135. **Stage A — auto-writeback:** a per-group dirty bitmask set at the
  `SEQ_PAR_/TRG_/CC_Set` source-write chokepoints (the render mirror never
  passes through them, so per-tick rendering can't false-dirty), written back to
  the group's working slot before any switch (`SEQ_PATTERN_WritebackIfDirty` in
  the Handler + the Change immediate branch); switch margin 50→100 ms to cover
  the added write. The stall-race precondition was **retired, not built** — with
  the writeback firing at service time against `seq_pattern[group]`, a per-group
  request overwrite loses only an intermediate switch target, never the
  writeback decision. The by-ear pass found the bypass-writer class (preset
  import / clears / undo memcpys write source directly, skipping the chokepoints
  → played but not flagged dirty → discarded on switch); swept and fixed.
  **Stage B — gen-state tag V4:** the engaged organism round-trips byte-identical
  and resumes ENGAGED. **Stage C — CHECKPOINT/REVERT:** see the dedicated bullet
  below. As-built in REFERENCE; user manual "Fearless Switching".)*
- **Groups demote to shelving** (revises "Concurrency is group-granular" / §6 "a
  state = a group"). The performer-facing recall grain becomes the **track**
  (track-grain load fills the missing grain cell: track-save/group-save/group-load
  exist, track-load doesn't); groups survive as storage layout + the 4-group
  switching machinery, not as a mental-model object. Rationale: the group pain was
  located 2026-06-11 as *conceptual overhead*, and a six-box comparison (Hapax /
  Digitone II / Cirklon / Octatrack / Deluge / OXI One — plan doc §9) found no
  instrument that recalls in semantic-free groups: a grain boundary earns its cost
  only when it aligns with a musical concept.
- **An organism is a phrase** (revises "no new macro features" / §5 "macro rides
  upstream"). Phrase mode is promoted to the scene system: phrases reference
  track-grain states with two-face recall (tap = posture, FREEZE-held = tape); song
  mode does NOT grow linear DAW depth — its action vocabulary (Tempo, jumps, loops,
  mixer maps, mutes) survives as arrangement tooling. Candidate principle from the
  comparison: **phrases reference CHECKPOINTed states, not working slots** —
  assignment references drift under auto-persist (Hapax's documented weakness,
  solved Cirklon-style by anchoring to committed versions).
- **Invariants that survive any re-envisioning: faithfulness (heard = saved) and
  deterministic returnability.** User granted full license to rethink everything
  else; losing either guts the north star.
- **First bundle = RECOMBINE** (track-grain load + pull gesture; licenses the
  SD-overwrite undo keystone, which extends the shipped ENGAGE undo). Picked over
  fearless-switching (heavier preconditions: stall-race fix, gen-state tag), the
  tape (storage fork open), and terrain-hands (orthogonal, runnable anytime).
  *(SHIPPED + by-ear hard GO 2026-06-12 — verb `SEQ_CORE_LoadTrackFromSlot` +
  one-deep track undo (CCM, kind field reserves the push-side SD victim) +
  track-hold pull gesture + SELECT+CLEAR undo; HIL 117/117. The §5 hypothesis
  confirmed by use: the user's restated group pain — "being forced to choose 4
  tracks to move together… a decision I don't ever feel ready to make" — is a
  premature ensemble commitment, and the pull makes ensemble membership a
  performance decision. As-built facts in REFERENCE §3; user manual section
  "The Pull".)*
- **Refinement (same day, after a clips/scenes challenge from the user):
  organism-primary — no grid on the performance surface.** "Groups demote to
  shelving" sharpened: the performer-facing model is four nouns — the **organism**
  (live 16 tracks, sculpted), the **tape**, the **anchor** (checkpoint), the
  **waypoint** (phrase). *A set is a path, not a grid.* The clip-grid view of
  storage (the bank format re-projects exactly to 16 per-track columns × 64 named
  sections) belongs to the curation surface only — grid-shaped thinking is
  quarantined to the librarian, which decides the performing-vs-curating
  two-surface split in the same stroke. Track pull = **transfusion into the
  organism, not launch** — two-faced at pull time (tape or posture/spring; the
  spring pull has no session-view analog anywhere). Rationale: clips/scenes is
  grid-primary (the grid is the instrument, performing = navigating prepared
  material, states are dead); this design inverts it (the running state is the
  instrument, storage is its memory).
- **CHECKPOINT / REVERT confirmed as the protection verbs (same day).**
  Bless-the-anchor / one-gesture-back are committed performance-surface
  vocabulary. Open at mechanism design: checkpoint *storage* (parallel checkpoint
  bank vs record-pair — existing banks lack 2× slack for pairing) and checkpoint
  *grain* under organism-primary (group vs track vs whole-organism). Phrases pin
  CHECKPOINTed states (see refinement bullet).
  *(SHIPPED Stage C, by-ear GO 2026-06-13. **Grain = whole-organism** — one
  gesture blesses all 4 groups (incl. their living generators); **one-deep** —
  CHECKPOINT overwrites the blessed copy, REVERT returns to it. **Storage = an
  internal fifth "bank" `MBSEQ_AN.V4`**, lazy-created at first CHECKPOINT, reached
  by a sentinel bank index rather than bumping `SEQ_FILE_B_NUM_BANKS` (a bump
  would index `seq_pattern[4]` out of bounds in `SaveAllBanks` and leak the
  anchor into the bank UI). It reuses `SEQ_FILE_B_PatternWrite/Read` wholesale —
  so gen state rides the V4 tag for free — while staying out of every
  `for(bank<NUM_BANKS)` loop, so it is never auto-loaded/saved, not navigable,
  and survives a session writeback untouched. REVERT reads the 4 records straight
  into live RAM (**not** via `SEQ_PATTERN_Load`, which would repoint the working
  slot at the anchor) + forced render + sustain-cancel/PC fan + sets every group
  dirty (the inversion: the next switch writes the reverted state back). Gesture,
  decided with hardware in hand: **SELECT+BOOKMARK tap = CHECKPOINT, hold ≥1 s =
  REVERT** — the destructive verb gets the deliberate hold, mirroring
  SELECT+CLEAR=undo; on midiphy SAVE/UNDO map to no key. Accepted POC cost: a
  mid-op SD failure can leave a partial anchor (parity with the working-slot
  writeback's power-loss exposure); atomic temp+rename is the fix if it bites.)*
- **The tape supersedes §5.5 quick-capture (same day).** Discovery capture becomes
  an **append-only, session-scoped take list**: never aims, never overwrites,
  never blocks — which dissolves the quick-capture vs no-smart-defaults conflict
  (append-only destroys nothing). The `CAP_NNN` wrap-oldest scheme is withdrawn;
  bank-full refuse+flash survives only on *aimed* gestures. The tape is the
  performing→curating handoff artifact; the librarian audits it morning-after.
  Storage fork open (session file vs RAM+SD journal vs dedicated bank — leaning
  against the bank: it breaks the hard-wired bank↔group identity). Precedent: the
  Cirklon workscene (jam, SAVE appends a take — decades of field validation).
- **Second-row hardware verbs confirmed (direction, same day): the touchable
  tension meter and the chord hand.** Row-as-meter: mirror `tension_meter` to the
  16 LEDs (bipolar, detent between LED 8/9, fill outward); **press = set GRAVITY
  at that position** (the isolator-throw gesture; manual turns already abort
  RESOLVE and jump). Chord hand: a select-row chord/keyboard mode writing the bus
  notestack — closes the "cross-bus chord workflow (no UI yet)" gap; SHADE-aware
  degree mapping (the row always plays the current terrain). Both obey the
  row-mode ownership rule (page-scoped or held-modifier, never a free-floating
  global toggle). Build details (blink convention vs the pages-manual §7.1
  brightness driver; velocity-less accent modifier) decided at the workbench by
  ear — the TERRAIN-HANDS bundle.

**2026-06-13 — PHRASES cross-session occupancy + recall "never lose work" (SHIPPED,
by-ear GO 2026-06-13; HIL 143/143)** — two follow-ons to the Stage-A snapshot library,
both landed with NO phrase-file format change.
- **Cross-session probe.** Occupancy is RE-SEEDED from disk on session load
  (`SEQ_PATTERN_ProbePhrasesOnLoad` replaces the unconditional reset at the
  `SEQ_FILE_LoadAllFiles` hook), so a reloaded set lights its captured phrases and they
  recall again — before, the RAM-only mask zeroed on load and the library went dark until
  re-captured blind. **Probe-by-content**, made safe against the out-of-order-capture
  `f_lseek` gap (the `SEQ_FILE_B_Create` zero-fill is `#if 0`'d for FatFs, so skipped
  slots hold UNDEFINED bytes): capture stamps a recognizable EMPTY marker (`num_tracks=0`,
  `SEQ_FILE_B_PatternWriteEmpty`) into each gap slot below it (walking down, stopping at
  the nearest present phrase → ascending capture writes zero extra), and the probe
  (`SEQ_FILE_B_PhraseOccupancyProbe`, fsize-bounded) reads each phrase's base header —
  occupied iff `num_tracks ∈ [1, NUM_TRACKS_PER_GROUP]`. Single source of truth (the data
  IS the marker; no second store to desync); also closes the latent out-of-order garbage-gap
  bug. Chosen over a header occupancy word (offset-shift blast radius across all banks +
  the anchor) and a sidecar file (desync hazard).
- **Recall never-lose-work.** A live nudge on a recalled phrase is no longer silently
  discarded when you recall the next: phrase recall now `WritebackAllDirty` before the
  snapshot overwrites live (mirrors the pattern-switch `WritebackIfDirty`-before-`Load`;
  `SnapshotRead` gained a `writeback_dirty_first` flag — **REVERT passes 0**, keeping its
  deliberate discard-to-checkpoint). **Phrases stay IMMUTABLE** — recall restores the
  pristine committed snapshot; the nudge lands in the group's working slot (recoverable by
  pattern-switch), NOT on the phrase. The margin (`seq_core_pattern_switch_margin_ms`) now
  also covers the recall's all-groups writeback+read; bump if a running recall wobbles at
  the bar. Two independent adversarial reviews clean; pinned by
  `test_phrase_occupancy_survives_session_reload` + `test_phrase_recall_preserves_live_edit_to_working_slot`.

**2026-06-14 — PHRASES Stage B-rest: the drift signal + naming (SHIPPED, by-ear/by-eye GO
2026-06-14; HIL 148/148).** The three deferred Stage-B items, all landed with NO phrase-file
format change. Resolves the §10 "drifted-since-recall signal" thread (the signal now exists).
- **Drift signal (`phrase_drift`, per-group, seq_pattern.c).** The clean "edited since the last
  recall/capture" that `seq_pattern_dirty` *can't* be — recall's own inversion ORs all of
  `seq_pattern_dirty`, so it reads dirty the instant you recall. `phrase_drift` is set at the
  SAME source-write chokepoint (`SEQ_PATTERN_DirtySetTrack`) and cleared by the recall/capture
  acts (re-baseline to "on the waypoint") + the session-load probe + harness reset — the
  over-fire-then-normalize-at-tail discipline `seq_pattern_dirty` already uses (CC-replay during
  recall/Load trips it; the recall/capture tail clears it LAST, after the replay). **USER DECISION
  (drift = MY edits, not the living organism wandering):** the generator's ambient per-measure
  auto-mutate is GATED OUT via a new `seq_generator_in_automutate` flag scoped tightly around the
  auto-mutate write in `SEQ_GENERATOR_Tick` — `seq_pattern_dirty` still sets (the wandered
  organism must still write back), only `phrase_drift` skips. So the drift LED stays dark while
  generators merely wander (FREEZE off), and the drift-LED + FREEZE read as one "anchored vs
  adrift" story. Deliberate gestures (ROLL / Snap / ForceMutate / Engage), hands-on par/trg/CC
  edits, and group switches DO drift. (Considered & rejected: making `seq_pattern_dirty` itself
  clean — it's structurally the FEARLESS writeback bit; a separate mask is the right call.)
- **Drift LED.** The current (last-recalled) waypoint, already amber on the PHRASE select-view,
  **winks** when `SEQ_PATTERN_PhraseDrifted()` — its RED bit drops on `ui_cursor_flash` so it blinks
  amber↔green (green occupancy bit stays solid → never reads un-occupied). Deliberately subtle (the
  cursor flash is on only ~50ms/500ms); by-eye GO confirmed visible. A bolder 50/50 amber↔off blink
  was staged then reverted — the subtle version was GO'd; revisit if it's missed in a live set.
- **Capture now sets `last_recalled_phrase`** (you just committed there → that IS "where you are"),
  so the current-LED follows captures, not just recalls, and drift reads meaningfully post-capture.
- **Naming (full keypad).** Reuses the stock `SEQ_UI_KeyPad_*` editor in a GLOBAL modal
  (`phrase_name_edit`) layered over the PHRASE view: the waypoints live on the SELECT row, the keypad
  chars on the GP/step row + encoders, so they don't collide; the LCD is taken over by a gesture
  overlay (mirrors the PATTERN-capture / pull overlays). Storage is FREE — the name is the phrase's
  base (group-0) record name, persisted by a thin `SEQ_FILE_B_PhraseWriteName` (20-byte field write,
  no format change) and RE-SEEDED on session load by the occupancy probe (extended to fill a names
  array at zero extra I/O). Blank (all-spaces) ⇒ the UI shows the slot number; capture stamps the RAM
  name into the record so disk == RAM (a never-named slot stays blank, NOT the inherited A-group
  name) and preserves a name across re-capture. Rename-without-recapture = `SEQ_PATTERN_PhraseNameCommit`
  (EXIT or GP16 in the editor). **Provisional gesture (tuned by ear, per FEARLESS precedent):
  hold-capture drops straight into the namer** (the editor LCD IS the capture confirmation; EXIT keeps
  the current name). User GO'd it "well enough for now" — likely decoupled when the whole system comes
  together. Enriched confirmation: recall/empty show `PHn <name>` / `Phrase N`.
- **Harness.** `CMD_PHRASE_META` (`0x7f` — the LAST free 7-bit command byte) folds four sub-ops
  (drift query / name get / set / commit) behind a payload selector; `PAYLOAD_BUF_MAX` 16→24 to fit a
  20-char name-set. +5 pins (148/148): drift trips on edit, recall clears, capture clears, **ambient
  generator wander does NOT drift but a deliberate ForceMutate does** (driven through the real
  measure-wrap path, mirroring test_freeze — the load-bearing semantic pin), name round-trip across a
  session reload, rename-without-recapture. Build-path note: `make seq` emits `project.hex` in the
  **app dir**, not `project_build/`.

**Live-surface hardening — by-ear-safety pass (2026-06-14).** A defensive round after the
PHRASES bundle, picked because the state assessment found the live *gesture layer* was the
POC-fragile part. Five fixes + a build flag; each platform claim verified against source first;
no new musical surface.
- **Phantom-pull guard (the headline).** The RECOMBINE pull armed on any select-row press
  regardless of view; in PHRASE view a phrase capture-hold (the select button is down ≥1s)
  left the pull armed, so a GP press during that window fired a real bar-aligned
  `SEQ_CORE_LoadTrackFromSlot` into that track — a silent destructive overwrite reachable from
  ordinary phrase use. The arm AND the top-row commit are now gated on `sel_view == TRACKS`
  (the pull is meaningless in any other view), with a stale-hold disarm on leaving the view.
  Mirrors the pre-existing PATTERN-path disarm.
- **Partial-capture occupancy.** `SEQ_FILE_B_PhraseOccupancyProbe` trusted only the group-0
  header, so a capture that died mid-write lit as occupied and recalled truncated bytes. Now
  requires the LAST group's header too — snapshots write groups 0→3 in order, so a present
  last-group header proves the whole block committed. No format change (reuses an existing
  field).
- **Auto-undo vs load.** The one-deep generator auto-undo wasn't invalidated by a disk load,
  so ENGAGE→load-pattern→UNDO clobbered the freshly-loaded track. New
  `SEQ_GENERATOR_UndoInvalidate()` called at the `SEQ_PATTERN_Load` and `SnapshotRead` tails
  (BOUNCE still preserves the slot, by design — §3 live-safety net).
- **SHADE persistence.** The GRAVITY page set `ui_store_file_required` but installed no exit
  callback, so SHADE (the global scale) never reached the config file. Added the callback,
  matching every other store-file page.
- **testctrl footgun → compile flag.** The HIL SysEx control surface shipped unconditionally,
  reachable from every MIDI-in port behind a 6-byte header (could mutate banks/sessions/CCs/
  FREEZE mid-set). Now gated by `SEQ_TESTCTRL_ENABLE` (default ON, so the harness build is
  unchanged); the gig/release build is `make TESTCTRL=0`, which compiles it to no-op stubs,
  reclaiming ~7.7KB flash + 264B RAM. Flash the `TESTCTRL=0` firmware for any real performance.
- Low-severity cluster filed to TODO_TRIAGE ("Fork hardening backlog"); the L1 transient-SD
  distinction stays deferred (§10).

**2026-06-16 — POSTURE-MORPH (Loop A) + phrase-recall landing feel (SHIPPED, by-ear GO; HIL 159/159)**
The phrase-morphing bundle (queued in §10 since 2026-06-13) plus a recall-feel fix the user
hit while evaluating it. Both by-ear-confirmed, committed together.
- **POSTURE-MORPH (Loop A).** Per-group posture interpolation, live→target phrase's same-group
  slice, over the ext-CC posture block (0x80..0x9f: robotize / chord-mask / GRIP). Full §10
  "Phrase morphing" SHIPPED note has the detail + the two deliberate divergences from the
  original theory (ext-CC subset only; per-measure, not immediate). Scope decided by the user
  via AskUserQuestion: per-group / from-live→target / datawheel-fine + GP-bar-coarse. The
  arm-time A snapshot makes pos 0 a true reversible pass-through. SELECT+tap arms (controls are
  scoped to the page armed on — robust to `simplified_antilog_frontpanel`, which means the PHRASE
  button may not switch to SONG). Released on any out-of-band CC replace (recall/revert/switch/
  pull/UNDO/session-load). Adversarial review (two workflow passes) caught + fixed a cluster
  (cross-page control hijack, stale-A after recall, an arm/boundary TOCTOU, spurious drift at
  pos 0) — every fix is a no-op when disarmed, so the 149 baseline was structurally safe.
- **Phrase-recall landing feel.** Recall while playing used to **click** (immediate mid-bar
  sustain-cancel) and **groove-jump** (the `ManualSynchToMeasure` re-phase). New `RECALL_SEAMLESS`
  option (OPT menu, persisted in MBSEQ_C.V4): **QUANTIZE** (default — keep the bar-aligned
  restart, clean downbeat) vs **SEAMLESS** (no re-phase, groove continues). Both drop the
  note-cut. REVERT / stopped recall keep the immediate hard restore. Parameterized via
  `SnapshotRead` land-flags (`SEQ_SNAPSHOT_NO_CANCEL` / `_NO_RESYNC`); no deferral, no RAM hit.
- **The real timing glitch (platform fix).** Even seamless/quantize still glitched: phrase
  recall read the 4-group snapshot off SD inside `portENTER_CRITICAL` (interrupts OFF for the
  whole multi-ms read). Recall runs in `TASK_Hooks`; emission/clock in higher-priority
  `TASK_MIDI` — interrupts-off blocked `TASK_MIDI` mid-bar = the stall. Fix: read with
  interrupts ON (drop the critical section, keep the SD mutex), mirroring `SEQ_PATTERN_Load`
  (the clean pattern-change path) so `TASK_MIDI` keeps emitting through the read. Applies to
  every recall + REVERT.

**2026-06-19 — The three-tier "return" model + durable SET baseline + the generative freeze/bounce law (DECIDED; design-ahead, build queued)**
A design conversation to refine the save paths. No firmware this round — the model is
crystallized here; build order is a later GO.

- **The unifying frame: one verb family, "bless / return," at three scopes.** Undo,
  CHECKPOINT/REVERT, and the new SAVE-SET are the *same idea* — return to a previous or blessed
  point — at reflexive / organism / set grain:
  - **Tier 1 — UNDO/REDO** (reflexive): step back/forward the last *deliberate* gesture. RAM,
    shallow, one gesture pair.
  - **Tier 2 — CHECKPOINT/REVERT** (blessed organism): the 4-group safety anchor
    (`MBSEQ_AN.V4`, shipped FEARLESS Stage C).
  - **Tier 3 — SAVE-SET/RELOAD-SET** (blessed set): the durable whole-shelf baseline (new).

  All three operate on **deliberate gestures only** — ambient generator wander is never a
  return target (reuse the `seq_generator_in_automutate` gate that already drives
  `phrase_drift`). This is what makes "undo" coherent on a living instrument: you return your
  *edits*, not the organism breathing. The frame collapses §5.6 overlap #1 ("three commit
  paths, one idea") by naming the verb.

- **Durable SET baseline (the Digitone-2 split).** Today the fork always-persists —
  auto-writeback flushes every dirty group to its working slot, so reload = your live state and
  there is no explicit saved baseline. Decision: add a **SET** layer above the disposable
  **SESSION**. The 2026-06-11 inversion is *reframed, not reversed* — always-persist now means
  *"persist to the SCRATCH session"*; a SET is an explicitly-blessed copy. SAVE-SET blesses the
  whole shelf; RELOAD-SET returns to it (the deliberate "back to what I saved"). *(The
  "recall-relaunches" rider here is superseded — recall = select a static grab; §9 2026-06-21.
  The SET return is unaffected: it reloads static grabs either way.)*
  - **Scope:** a SET = banks `B1–B4` + config `C` (+ `G/M/S/BM`, cheap and faithful).
    **Phrases (`PH`) persist independently** — a growing durable library that survives
    RELOAD-SET (a captured waypoint is never lost; keeps the §10 L1 probe hazard dormant). The
    **anchor (`AN`) is excluded** (transient in-session undo). Global `GC` is already
    root-scoped — untouched.
  - **Boot:** resume the live scratch (unchanged); RELOAD-SET is the explicit baseline return.
    A `GC` flag (`boot_from_set`, default off) offers pure-DN2 power-cycle-to-baseline as an
    opt-in.
  - **Mechanism (verified this session so a later build doesn't re-derive):** a file-copy layer
    cloning `SEQ_FILE_CreateBackup` → `FILE_Copy` (with the corrected file list);
    `SEQ_FILE_LoadAllFiles` is the reload engine and already re-seeds phrase occupancy; atomic
    via a temp dir + `f_rename` (compiled, dir-capable). **SD-only, ~0 RAM** (reuses the
    existing 512 B `tmp_buffer`, never touches the ~9 KB CCM wall); non-live (low-priority
    `APP_SEQ_Task`, behind the LCD progress bar). Storage `/SETS/<name>/`, symmetric to a
    session dir. **Rejected:** a snapshot-bank baseline (`SnapshotWrite` captures only the 4
    *loaded* groups — fatal to the switched-away case) and an overlay/divergence scheme (adds
    logic to the fragile shared dirty plumbing).
  - **CHECKPOINT/REVERT stays — not merged.** Different grain + lifetime (in-session 4-group
    quick undo vs durable whole-shelf baseline); REVERT structurally cannot restore a
    switched-away pattern, only RELOAD-SET can. The set enters as CHECKPOINT's **durable-scope
    sibling**, so the surface *shrinks* (one named model) even as capability grows.

- **Unified UNDO/REDO (Tier 1).** Today undo is five bespoke one-deep mechanisms with four
  gestures (track = SELECT+CLEAR, generator = GP2, utility = GP8, organism = SELECT+BOOKMARK)
  and **no redo anywhere** — that scatter is the felt clunk. Decision: unify into one
  context-aware UNDO + one REDO over the last deliberate gesture, a shared shallow RAM store
  consolidating today's track/generator/utility one-deeps (~5 KB total today), redo added,
  pushes gated by the `automutate`/`drift` flag. A deep linear (DAW-style) stack is rejected —
  wrong altitude for an instrument whose state wanders; the blessed tiers are the deep return.

- **The generative freeze/bounce law.** Everything generative is born a **render-stack
  citizen**, so FREEZE means one thing (hold the wander) and BOUNCE means one thing (freeze the
  heard result into editable steps) uniformly. Robotize is the lone emission-time exception
  (queued §10, in FORCE_SCALE's bake-then-migrate lineage); new **trigger generators** must obey
  this from birth so they never become a second robotize.
  - **Where the law bends — temporal config modulation (named 2026-06-19).** Self-modulation
    splits. Its *note-grain* half (self-transpose / chord) obeys the law — render-stack, so
    BOUNCE freezes the static notes. Its *config-grain* centerpiece (self-routing CC: direction /
    length / groove / clock-div …) does **not** — it modulates *playback behavior over time*,
    not buffer content, so the heard result is a **timeline, not a buffer**. BOUNCE (a buffer
    snapshot at one config instant) structurally can't hold it (harder than robotize: traversal,
    not note values). The static result is still capturable — by **recording the emitted output
    over time** (the tape / MIDI-as-sample family, §4/§5.5), which lays it flat and keeps only
    the notes ("static output, not modulation"). So the rule generalizes: **BOUNCE freezes
    content transforms; the tape freezes time-varying behavior** — config-grain self-mod, like
    robotize, is a tape/record citizen, not a bounce one. **The resolution is the retroactive CAPTURE frame/ring
    + per-track-RNG determinism (§10)** — offline re-simulation (seed-frame) plus recorded inputs
    (bus + live) freezes the lived timeline, closing this gap. (Generators are pitch-only today;
    trigger generators do not yet exist.)

- **Build status:** all of the above is **DESIGN-AHEAD / build queued** — the SET layer, the
  unified UNDO/REDO, trigger generators, and self-modulation each carry an actionable §10 sketch
  for a later GO. This round wrote no firmware.

**Provisional — recorded but NOT committed (Part II); revisit after §8 GO/NO-GO**
- Processor catalog organized by layer type-class; one stack per (track, layer-class);
  strict stacking within a class; cross-track deferred (use Bus).
- Render-cache: source/output/stack tiers, quiet/editing/sweeping regimes, per-track
  only in v1, tick reads *output* via the existing `SEQ_PAR_*Get`/`SEQ_TRG_*Get`
  redirection, sweep-time lookahead = current step + small window.
- Robotize migrates to typed processors (pitch/vel/len).
- Conditional triggers as a gate-layer processor with per-step state; NEIGHBOR
  (cross-track) deferred to v2.
- HIL test plan (8 timing tests); RAM budget — **corrected, still provisional** (§A5).
- **Don't cut features speculatively for headroom** — wait for measured need. The
  earlier "remove MIDI File Player" cut is **withdrawn** (tick-loop surgery for ~1KB
  = poor ROI; find savings via CCM placement / slot-count instead). **CV/AOUT is
  actively used (full midiphy CV/Gate/Trigger rig) — never a cut candidate.**

**2026-06-20 — CAPTURE and BOUNCE are one verb: the unified retroactive FREEZE → anywhere (model decided; build slicing).**
The user's framing, after the precise-gate/multi-step work landed: CAPTURE (the retroactive grab off
the ring) and BOUNCE (the lossless freeze of the computed output) are *two means to one end* — freeze
whatever the organism is doing (**playing / generated / programmed**) into static material, in
**whatever track/pattern** you choose. They should be **one feature**. The reason they're separate
today is not philosophical — it's a single engine boundary:

- **Render stage** — programmed layers, the generator's *current loop*, and the render-stack
  transforms (FTS / limit / tension / transpose / chord-mask / the pitch chain). The full result
  lives in the rendered mirror **as par/trg layers** → losslessly copyable. **BOUNCE already grabs
  this** — all params + trigs + CC + config (`SEQ_CORE_CaptureTrackOutput`).
- **Emission stage** — random traversal *order*, robotize, roll-as-played, echo, probability/humanize
  coin-flips, **live MIDI keys**. These fire per-tick *after* render and **flatten into the played
  notes** — they are no longer layers, so the only faithful record is the note stream. **The ring /
  tape grabs this** (note/vel/length; a roll *collapses* to one quantized note — lossy by nature).

**Decided model — one verb: `FREEZE [last K bars of the ring] → [track, pattern], all params,
generators off`:**
- **Bounce is the lossless foundation, not capture.** Correction to the earlier "two tools, division
  of labor" framing (the §10 CAPTURE entry's "BOUNCE freezes transforms / CAPTURE freezes the
  timeline"): the note-stream is the *weaker* base — it collapses rolls/chords to single notes; bounce
  keeps every layer editable. The unified verb is **bounce, made retroactive by the ring, targetable
  to any track + pattern** (slot-bounce — `SEQ_CORE_CaptureToSlotTrack` — already writes to a pattern).
- **The emission residue is the only thing that still needs the note-stream — and it is not
  permanent.** Every emission effect that migrates into the render stack (robotize→render-processor is
  the named piece — §5.6 #4) moves from the note-only residue into the lossless bounce path. **When the
  migration completes, FREEZE = BOUNCE = one verb;** the note-stream survives only for the irreducible
  real-time input (live MIDI keys, true emission coin-flips) that cannot be expressed as layers at all.
  **Convergence, not duplication.**
- **Memory is not the obstacle.** The static layers (roll/chord/CC) don't vary per bar → captured
  once; only the per-bar notes vary, which the ring already holds.

This **reconciles** two prior decisions that look contradictory: 2026-05-30 ("delete the emission tape,
unify on bounce") concluded bounce *alone* suffices; the 2026-06-20 ring/tape proved an emission
residue bounce genuinely can't see (traversal order / robotize / live keys), so a tape came back. Both
are right — bounce is the lossless core; the tape is the **temporary** catch for not-yet-migrated
emission, shrinking to zero by render-stack migration.

**Shipped toward it (2026-06-20):** precise gate length + **multi-step length reconstruction** — a
hand-drawn >1-step note isn't one big length value, it's a length *chain* across steps (max a step's
length, carry it on the next), so the tape now rebuilds it: gated Gld start + carried Gld steps (note +
velocity repeated, gate off) + a fractional tail that terminates the sustain. The note-stream capture
is now faithful for melodic articulation including long notes (by-ear GO; HIL
`test_capture_precise_gate.py`). **Tape path only; the stopped re-sim sink is the same-helper
follow-on.** **Next slice = bounce-off-the-ring with dest = track + pattern** (lossless retroactive
freeze to any slot). Slice plan: `doc/plans/archive/2026-06-20-unified-freeze.md`.

**2026-06-21 — Strategy shift: validate the SYSTEM, not the fragment (DECIDED; §2 #8 added).**
A design review + a source-grounded system flat-map (`doc/plans/archive/2026-06-20-design-review-and-refine.md`,
`doc/plans/archive/2026-06-21-system-flatmap.md`). The builder's call: fragment-grain validation has
"very little meaning" — you can't tell if one function is musical without using it enough to
make music, and locally-correct pieces keep failing at assembly (clunk, won't compose, RAM/CPU
walls). Discipline is a tool to drive a result; it shifts when it stops working.
- **New validation grain = a whole performable set/path**, bounded to the §1 flow with the
  pieces already built; "performable and fun" replaces "musical" as the bar; build licensed by
  system-level play, not a feature catalog (§2 #8; §8 reframed; §2.7's grain extended).
- **Flat-map verdict: the box FITS but doesn't HOLD TOGETHER — playable, not yet performable.**
  Two structural walls live at map *overlaps* (invisible per-fragment): (1) the **recall freeze**
  = budget × dirty-mask × FREEZE-mode — the center of the flow freezes ~1.3 s, RAM escape dead;
  (2) **trigger gens don't fit** (~11.25 KB twin pool > both ~9 KB regions). The emergent,
  system-derived refinement queue (#1–#7) replaces the §8 feature ladder.
- **Verified factual correction:** main RAM is **~9 KB free, not ~33 KB** (the 2026-06-19
  re-measure mis-stated it by ~24 KB); the "~40 KB CCM-relocation lever" is **withdrawn** — both
  regions are ~9 KB and par/trg source is DMA-locked to main. §A5 + §10 levers corrected.
- **The dirty/writeback mask is reframed from "fragility locus" (§5.6) to the design's single
  point of failure** — every return-family op rides it; it gets a containment plan, not just a name.
- **Scope anchor — a FUSION instrument, not a generative one (user, 2026-06-21).** The generative
  layer is *additive*; it fuses with the traditional step sequencer + the live MIDI recorder into
  one shared *get-material → process → tweak → harvest → refine* loop (program / play / generate
  are co-equal sources). Generators/processors are prospecting tools for material the hand then
  harvests + refines — symbiosis between instrument and intention. Goal = **refine the whole
  synth**, not a generative corner. Folded into §1 (north star), §3 (source-agnostic buffer), §4
  (live stream = generated *or* played-in). Guards against the doc's drift toward generative-first.
- **Architecture recentered on CAPTURE — four homes; generators demoted; generation deferred
  (user, 2026-06-21).** The instrument is **MATERIAL / MOTION / CAPTURE / LIBRARY**, capture at
  the center (folded into §3). **MOTION is the generative heart** — the patchable modulation web
  (bus + self-bus + traversal + harmony render-stack); "modulation counts as generative." The
  shipped Turing generators + standalone chord-mask/tension are **demoted to examples/POCs to
  work from** (user: "what we have now are examples and POCs"). **Generation as its own engine =
  a separate domain, deferred.** Organizing principle: *arrange MOTION so CAPTURE is lossless.*
  The **MVP = the first full sequencer workflow** (§8) — capture · edit · process · capture · edit
  — leaning on modulation+capture, NOT generators; makes the recall-freeze cure, bounce-to-pattern,
  and self-bus (note-grain) load-bearing; resolves the review's spine-vs-instrument gap (the engine
  is two-noun; the instrument is four-home).
- **Return simplified to "grab and work" (user, 2026-06-21).** A saved state is a **static grab**
  (a capture off the live source), not a posture-you-re-enter-and-it-regenerates. **Return =
  select an earlier grab** (exact, identifiable by construction); the first grab is the beginning,
  grabs in order are the path/set. This is §4's sample discipline taken at its word, resolving the
  §4↔§5 tension in §4's favor. **Retired:** the "recall relaunches/regenerates / recall ≠ resume"
  law (it fought §4; SEAMLESS recall contradicted it) and "two faces are a recall gesture." The
  *living* return is a **performed move between grabs** — morph / soft-return / **reseed** (feed a
  grab back into the source → return-as-evolution) / grab-a-moment (the ring). Generativity lives
  in the *source* (now) + the moves between grabs, never in re-animating a saved state — so the
  resume/regenerate complexity is **deleted, not solved.** **The §3 recursion is untouched:** a
  grab is never a dead end — load it and run motion over it and it *becomes a source again*
  (source/grab = two states of one material, live↔frozen; the *process* step of
  capture·edit·process·capture). Folded into §1 / §3 / §5 (rewritten
  "Grabs and the source" + "Return = navigate your grabs") / §6 / §11. User: "yes 100%."
- **Freeze semantics nailed down (user, 2026-06-21).** **(a) Freeze always disengages** — the
  generator's *wander* turns OFF (restores the §3 spine "BOUNCE freezes + disengages"; the shipped
  same-group "save-only bounce keeps the gen running" was a deviation, removed). Destination (in
  place / new slot) only picks *where the static lands*, not whether the gen stops. **(b) Emission
  is CONFIG-COPIED across, not tape-baked (refined later same day).** Freeze copies the emission
  settings + seed; the **deterministic** emission re-applies identically on the full render
  material → **sounds the same** (faithfulness). The **groove model generalized** — and it beats
  tape-baking: **lossless** (the tape is a mono note-stream, rolls/chords collapse), **tweakable**
  (emission stays config on the grab, not fused into dead notes), **cheaper** (a seed). *This — not
  a tape-bake — is why robotize isn't migrated.* The **RING** (seed-frames) is the retroactive
  backbone (re-sim reconstructs the last K bars). **(b2) The TAPE shrinks to its irreducible job:
  LIVE MIDI input** (+ consumed external/bus) — the only thing with no config to copy. Dependency:
  humanize + coin-flips need the **emission-determinism follow-on** (§10) before they're
  config-copyable; until then the tape bridges them → after, tape = live-keys-only. **(c) The "keep
  running" cases are separate verbs:** **COPY** = fork the *live* source (gen on); **retroactive
  CAPTURE (ring)** = static snapshot without stopping (play-then-keep); **THAW** = just ENGAGE a gen
  over a grab (QoL, not a new verb). Build-time flag: changes the shipped same-group bounce **and
  drops most of the emission tape**. Folded into §3 / §5 (freeze section) / §10.

**2026-06-22 — Phrase mode + morphing reviewed under the capture-centric model (DECIDED; doc-only).**
A model review of phrase recall + phrase morphing against the 2026-06-21 capture-centric overhaul +
the flat-map. They split into **opposite** verdicts (the box handles recall; morph is the strained one):
- **Recall — the box handles it; the overhaul *unblocked* the cheap freeze cure.** The flat-map's
  #1 open by-ear question ("may un-captured generator wander be abandoned on phrase recall?") is
  **answered by the new model itself**: recall = select a *static* grab, and the living-return is a
  performed move *between* grabs — so un-captured wander is not precious at recall (if you wanted it,
  you'd have captured it; the ring is for play-then-keep). That makes **DRIFT-gated writeback**
  (≈1 line; `phrase_drift` already exists, unused by writeback) the **faithful** implementation of
  the new recall semantics, *not* a compromise — the "things revert" worry that reverted it
  (2026-06-19) lived inside the now-retired two-face model. **SHIPPED + by-ear GO 2026-06-22** —
  `SEQ_PATTERN_WritebackAllDrifted` (gated on `phrase_drift`); recall's pre-writeback in
  `SnapshotRead` calls it. Played on hardware: the ~1.3 s freeze is gone, abandoning un-captured
  wander feels right ("i like it, no freeze"). Removes the freeze from the center of the flow.
  (Resolves §10 phrase-recall-freeze + flat-map queue #1.)
- **Morphing — recorded-state morph becomes the canonical/primary morph; live phrase-posture morph
  is demoted to a by-ear CUT candidate (user, 2026-06-22).** §5 already held three models;
  **recorded-state morph** (bounce A then B, window across the seam — ~0 new RAM, "preferred,
  cheaper," reliable/repeatable) is now primary. The shipped **live phrase-posture morph** (~7.7 KB
  main RAM — the largest discretionary RAM lever — and the gesture that drives the *unmeasured*
  all-16 force-dirty CPU wall, flat-map Map A #3) is the **by-ear cut candidate.** The new model
  demotes it from *the* living-return mechanism to one of four performed returns (morph /
  soft-return / reseed / grab-a-moment); soft-return + reseed cover return-to-origin + evolution
  cheaply, and recorded-state gives the A→B crossfade. The only thing genuinely lost is continuous
  A→B blending of two *live* engines. **Cut SHIPPED as a compile flag** `SEQ_PHRASE_MORPH`
  (`make PHRASE_MORPH=0`, mirrors TESTCTRL; default ON so plain builds are unchanged). Build-verified
  both states: the cut reclaims a **measured 6680 B (~6.5 KB) main RAM** (free 9.06 → 15.59 KB; the
  "~7.7 KB" estimate corrected — buffers were main-RAM not CCM; flash −3592 B). The default stays ON
  until the cut is blessed by ear in a real set (the freeze-cure build the user GO'd was the
  `PHRASE_MORPH=0` hex, so the cut has at least one positive session — but missing-the-morph wasn't
  the thing under test). **CAVEAT:** windowing isn't built, so the cut build has *no* live continuous
  morph at all (hard cuts via recall + SWITCH-QUANTIZE still work) — building the recorded-state
  window gesture is the follow-on that makes "recorded-state primary" real. Updates §5 (Morphing) +
  §10 (phrase-morph keep/cut) + §5.6 #3 + §A5.
- **Doc hygiene:** §5.6 concept-map's "Phrase recall" row still described the retired two-face
  recall ("posture = regen; FREEZE+tap = frozen tape"); reconciled to the static-grab model (the
  shipped FREEZE switch still governs *whether the generator wanders after you land*, which is the
  source's state, not a face the recall gesture picks). §5 Morphing's "candidate, not built" tag on
  phrase-posture morph was also stale (it shipped 2026-06-16) — corrected.

**2026-06-22 (cont.) — by-ear morph-feel tuning + lean capture + the capture-while-performing
freeze named (DECIDED; morph kept, on hardware).** Played the morph-on build; outcome: **phrase +
phrase-morph are "the best they have been" — by-ear GO, committed to main.** The morph cut went the
OTHER way once it was felt working — **morph KEPT** (default on), the `PHRASE_MORPH=0` cut flag stays
as the RAM lever only.
- **Morph feel retuned by ear.** (a) The snap CCs (octave-transpose, groove-style — can't lerp) now
  flip at the **MIDPOINT** (`pos >= MAX/2`), not full throw, so the throw reads "bottom half = A,
  top half = B." (An octave-transpose test first read as a "hard switch with no interpolation" —
  it was the by-design snap; clarified, then moved to the midpoint.) (b) **Notes + gates now do a
  TENT — "flip then unflip":** each differing step shows B when its frozen random threshold is below
  the *proximity to the midpoint*, so ~50% are on B at the center (max scramble) and ALL re-cohere to
  A at both ends. So full throw = **B's posture / dynamics / transpose over your A notes + rhythm**;
  the center is the point of maximum note/rhythm disorder — a "departs and returns" character, not a
  land-on-B for the discrete content. Lerped dims (velocity / semitone / groove-amount / ext-CCs)
  still travel straight A→B. (`morph_prox` in `phrase_morph_apply`.)
- **Lean capture (naming opt-in).** Phrase capture no longer auto-opens the keypad or writes a name
  (dropped the capture-time `PhraseWriteName` *and* the EXIT-commit write). A bare capture = just the
  four group records. Naming is deferred to a future deliberate gesture. (Was: capture froze longer
  partly because naming added two SD writes on top of the four records.)
- **Capture-while-performing freeze = a NAMED fundamental requirement (fix DEFERRED).** User: capturing
  a whole-organism phrase *while playing* is fundamental, and the ~1.16 s clock freeze on capture is a
  showstopper. **Root cause (source-verified):** every SD sector write ends in a busy-wait poll for the
  card to finish programming (`mios32_sdcard.c:556`) and the app **never defines the DMA yield hook**
  (`MIOS32_SDCARD_TASK_SUSPEND_HOOK`), so a 4-record capture spins the CPU ~1.16 s with no yield. **The
  paradox:** preemption is on (`configUSE_PREEMPTION=1`), the clock task `SEQ_TASK_MIDI` takes only
  `MUTEX_MIDIOUT` (never the SD mutex), and the spin is interrupts-on — so the higher-priority clock
  task *should* preempt and keep running, yet it froze. The real trigger is a **device-level scheduling
  subtlety** (likely a task-priority/attribution detail) that needs on-device confirmation. **Fix path:**
  wire the SD task-yield (define the SUSPEND/RESUME hook + add a yield to the completion poll) so a long
  write releases the CPU and the clock advances between sectors — benefits *all* SD writes (capture,
  working-slot save, CHECKPOINT, recall-writeback). Deferred to commit the good morph state first;
  this is the next bundle. (Incremental save is NOT the cure here — a fresh slot's first capture is
  all-new data; the cure is the yield, then optionally chunk/RAM-stage for cross-tick consistency.)

**2026-06-23 — Capture-while-performing freeze: the premise was WRONG; lean capture already fixed it
(DIAGNOSED on-device + by-ear GO; RESOLVED).** Built the diagnostic first (a perf test, as planned),
and it overturned the 2026-06-22 (cont.) belief above. **The audible clock does NOT freeze on a
whole-organism phrase capture.** Measured on-device (new `CMD_CAPTURE_PERF` verb): during a ~640 ms
capture the emission task (`SEQ_CORE_Handler` in `TASK_MIDI`, +4) was starved for **1 ISR tick out of
570** — `freeze_fraction` 0.002. The paradox dissolves: emission is +4 and wakes every 1 ms via
`vTaskDelayUntil`; the capture runs at +3 (`SEQ_PATTERN_PhraseCapture` holds only `MUTEX_SDCARD`, never
`MUTEX_MIDIOUT`, and the SD busy-wait runs interrupts-on), so +4 simply **preempts** the spin and keeps
playing. (The *separate* `SEQ_CORE_CaptureSpan` ring-grab DOES hold `MUTEX_MIDIOUT` across the grab —
that one would freeze emission; it isn't the phrase-capture path.)
- **What the remembered "~1.16 s freeze" actually was:** the PRE-lean-capture path — a modal naming
  keypad popup + two extra name-writes (6 SD writes, plus a screen-hijacking modal). Lean capture
  (5638e97a, the morning before) removed both ⇒ ~640 ms, no popup. By-ear on the current build:
  **"feels fine."** What remains is a ~640 ms *control-surface* hang (the +2 UI task — LED/LCD/button
  scan — is starved by the +3 capture) while the music plays straight through; acceptable by ear now.
- **Test-design correction (load-bearing for any future probe):** `SEQ_BPM_TickGet()` is incremented in
  the HIGHEST-prio HW-timer ISR (`SEQ_BPM_Timer_Master`), so it keeps counting through any interrupts-on
  stall — **useless for freeze detection**. The real signal is the emission-task *service gap*
  (`bpm_tick` vs `bpm_tick_prefetched`); shipped as `SEQ_CORE_ServiceGapReset` / `SEQ_CORE_ServiceMaxGapGet`.
- **Fix-path correction (parks the old plan):** wiring `MIOS32_SDCARD_TASK_SUSPEND_HOOK` is mostly a red
  herring — it wraps only the 512-byte DMA payload (~0.28 ms), **not** the ~290 ms card-programming
  busy-wait poll (`mios32_sdcard.c:556`, which has no hook). The real lever, *if the ~640 ms
  control-surface hang ever needs shrinking*, is a yield **inside the completion poll** (scheduler-guarded
  via `xTaskGetSchedulerState()`), which benefits every SD write; note `MUTEX_SDCARD` serializes all SD
  users, so a poll-yield frees the clock/UI but not another SD writer. **Parked as future polish** — not
  built, because by-ear says the freeze is already gone.
- **Shipped:** `CMD_CAPTURE_PERF` + the service-gap tracker as a **permanent regression guard**
  (`tests/apps/seq_v4/test_capture_perf.py`) pinning "capture-while-playing keeps the audible clock
  running" — this realizes §8 timing-test (8) "SD-write isolation on quick-capture." HIL **196/196** (the
  run also caught + fixed 5 tests left stale by 5638e97a's by-ear changes: the morph **tent** re-coheres
  to A at full throw, and lean capture is **naming-opt-in** — a bare capture no longer persists a typed
  phrase name).

**2026-06-23 (cont.) — the ~640 ms control-surface hang FIXED; root cause was NOT (only) the SD poll
(BUILT + by-eye GO + HIL 197/197).** Took the parked polish above and built it diagnostic-first, which
overturned the predicted fix-path. Built a **+2 UI-task service-gap probe** (mirror of the emission probe:
`SEQ_CORE_UIServiceGapReset/MaxGapGet/Mark`, marked at the top of `SEQ_TASK_Period1mS`; `CMD_CAPTURE_PERF`
now also reports `ui_gap`) and confirmed the control-surface starvation: `ui_freeze_fraction = 1.00` (UI
dead for the whole capture) while the clock stayed at ~0.001.
- **The poll-yield alone did NOT work** (`ui_freeze` 1.00 → 0.989). A driver census (temporary counters)
  showed the hook fired **1197×** — the CPU *was* being yielded — yet the +2 task still never ran. So the
  hang is **lock contention, not CPU starvation**: `SEQ_PATTERN_Handler` (the first callee in the +2 UI
  task, every 1 ms) takes `MUTEX_SDCARD` + a critical section *unconditionally*, just to poll for a pending
  pattern-switch request. During a capture the +3 task holds `MUTEX_SDCARD` for ~1 s, so the UI marks its
  gap (first line) then immediately **parks on the mutex** and never reaches the LED/menu/button work.
- **The fix is two complementary parts** (`ui_freeze` 1.00 → **0.011**): (a) a **lock-free pre-check** in
  `SEQ_PATTERN_Handler` — scan the (sticky) `seq_pattern_req[].REQ` flags WITHOUT the mutex and return early
  if none pending (this handler is the only place that clears REQ, so a request set just after is serviced
  the next tick — never lost), so the UI stops contending for the SD mutex every tick; and (b) the
  **poll-yield** (`MIOS32_SDCARD_WAIT_HOOK` → scheduler-guarded `vTaskDelay(1)` in `TASKS_SDCardPollYield`,
  fired every 256 completion-polls with a 1024-yield stuck-card fail-fast), which frees the CPU so the
  now-unblocked +2 task actually gets to run while the card programs. Neither alone suffices.
- **Platform-code discipline:** the driver change (`mios32/common/mios32_sdcard.c`) is a new *optional*
  hook macro defaulting to a no-op — every other MIOS32 app is byte-identical; only SEQ V4 opts in via
  `mios32_config.h`, mirroring the existing `MIOS32_SDCARD_MUTEX_TAKE`/`SUSPEND_HOOK` idiom. +104 B text,
  ~0 RAM. Full HIL **197/197** (many pins do real SD I/O — the regression net for the shared edit); new
  permanent pin `test_capture_while_playing_keeps_control_surface_live` (ui_freeze ≤ 0.30).
- **LCD residual — HARDENED (follow-on, same session).** The LCD is driven by the *low-prio* +2 task,
  which once per second runs `SEQ_TASK_Period1S` → `FILE_CheckSDCard` under `MUTEX_SDCARD`; a blocking take
  there parks that task (and the LCD) behind a ~1 s capture. Fixed with a non-blocking `MUTEX_SDCARD_TRYTAKE`
  (new macro in `tasks.h`): if the card is busy the per-second housekeeping skips this second and retries
  next (the check is periodic; the format/backup/save-all request flags are sticky → nothing lost). This is
  preventive hardening — correct by construction (a per-second task must not block the LCD on a long write),
  verified by HIL 197/197 + by-eye, not a measured before/after (the `ui_gap` probe watches the *regular*
  +2 task, not this one). +0 B/0 RAM (compiles to the same footprint). Committed alongside the main fix.

**2026-06-23 (play-readiness safety net) — unified UNDO/REDO net, Stage 2a SHIPPED (§8 queue #3,
part of #4).** Built the §10(a2) Tier-1 net: ONE global **action journal** `{state, before, after}`
(CCM, ~4.7 KB) consolidating the three bespoke one-deeps (`track_undo`, generator `undo_slot`,
utility buffers) behind `SEQ_CORE_JournalArm/Undo/Redo/Invalidate/InfoGet`, and **the REDO that
never existed anywhere**. Lazy `after` (snapshot live at undo-time); **symmetric** redo (each
direction re-snaps, so the one-deep is a reversible 2-way swap — nothing silently lost). Armed by
all four deliberate track-grain verbs (pull / utility copy-paste-clear / generator first-ENGAGE /
**capture** — both `CaptureToTrack` and the live `CaptureSpan` grab); wander can't pollute it (only
deliberate verbs arm). Gesture = **SELECT+CLEAR toggle** (undo↔redo; no UNDO button on midiphy;
EMPTY never destructively clears). >4-generator arm guard (refuse-leave-EMPTY) mirrors the
capture-ring overflow cap. **by-ear GO** ("working great, gesture feels good") + **HIL 206/206** +
a 5-lens adversarial review (22 confirmed findings) that caught 4 real defects the green suite
missed (the live CaptureSpan gesture wasn't armed; pool-full/re-engage clobbered a valid undo;
journal was in the wrong RAM region — main not CCM; redo was one-way). **§2 #8 discipline note:**
HIL-green was NOT sufficient confidence on a 6-file consolidation — the adversarial diff review was
load-bearing. Full trail + the 3 refuted + accepted-as-documented items in
`doc/plans/2026-06-23-play-readiness-safety-net.md`. **Phase 1 (visible modes, #2) DROPPED** —
recon found the `lso` rig already wires the FREEZE + sel_view LEDs (premise false on the hardware).

**2026-06-24 (play-readiness safety net cont.) — Stage 2b: REVERT-undoable SHIPPED; the bundle
closes (§8 queue #3 + #4 done, #2 dropped).** REVERT joined the Tier-1 net via a new ORGANISM
journal scope: `SEQ_PATTERN_Revert` stashes the live organism to a pre-revert anchor block (slots
4..7) *before* restoring the checkpoint, then arms `SEQ_CORE_JournalArmOrganism` — so SELECT+CLEAR
brings the jam back (`RevertUndoRead` re-reads slot 4), and a REDO re-reverts (`RevertRedoRead`
re-reads slot 0). A **fixed** 2-way swap (NOT symmetric like TRACK scope — a REDO-of-REVERT discards
post-undo edits by intent; a fresh track gesture re-arms TRACK scope). The reads route through
`SnapshotRead` (which invalidates the journal), so each organism undo/redo re-arms its state on
return. **by-ear GO** + **HIL 210/210**, and again the adversarial review (8 angles) was
load-bearing — HIL was green at 208 yet it caught three real data-loss/staleness holes on the panic
path, all fixed before commit: (a) **CHECKPOINT didn't invalidate the journal** → a checkpoint after
a revert left a stale organism redo pointing at the slot it just rewrote (now invalidates on a
committed checkpoint); (b) **double-tap REVERT destroyed the recoverable jam** → the 2nd revert
re-stashed live (== the checkpoint) over the jam (now skips the re-stash when already
ORGANISM/UNDOABLE, and arms on `stash>=0` alone so even a torn revert is recoverable); (c) magic
pre-revert base `4` vs `NUM_GROUPS` → compile-time assert. Plus a cleanup the review flagged: the
duplicated UNDO/REDO button-dispatch block extracted to `SEQ_UI_JournalToggleDispatch`. +0 RAM
(stash on SD, scope byte in CCM padding); +48 B flash. §10(a2) is now fully built; codebase facts
folded into REFERENCE (action-journal + FEARLESS sections) + MANUAL (CHECKPOINT/REVERT). Plan
`doc/plans/2026-06-23-play-readiness-safety-net.md` retired.

**2026-06-26 — render change-detection SHIPPED; the all-16 GRAVITY render wall is GONE (§8 queue
#5, the last play-readiness item).** chord_mask / tension / live-pitch slots used to force a FULL
per-track re-render EVERY tick — even when the field was perfectly STATIC. The render runs in the
+4 emission task's tick prologue, so with many tracks gripped it pegged the CPU and starved the
lowest-priority +2 UI task (control surface dark while audio limped); the user's real patterns
locked up at ~6 gripped tracks. **Fix (`SEQ_CORE_RenderTracks` / new `render_live_sig`):** fold
every LIVE input a force-dirty processor reads into a per-track u32 signature and re-render only
when it CHANGES. Static field → signature stable → **zero renders**; a dial sweep / held-chord
change moves the signature → re-renders during the sweep only. The "Phase D sweep/quiet
detection" the §8 phase-D notes promised, finally built. **The diagnostic-first arc paid off
twice:** (1) a cost-cut tried first (bounding the per-tick `memcpy` to a track's *used* bytes via
`par_used_bytes`/`trg_used_bytes`) moved the ceiling only 4→5 — real tracks fill the 1024-byte
buffer, so the bound rarely shrinks the copy; the root waste was re-rendering 900×/sec to produce
identical output, which only change-detection removes (kept the bound anyway — free for genuinely
lean tracks). (2) The DWT-cycle-counter render probe (`CMD_RENDER_PERF`, conflict-free vs the TIM6
stopwatch SEQ_STATISTICS owns) *measured* the wall before any fix: 16 full-buffer tracks = ~95%
render duty, ~1500 µs/tick (> a 140 BPM tick), UI service-gap ~1869 ISR ticks (dead). **The
critical correctness catch:** the build plan's signature audit was INCOMPLETE — it listed only
gravity+grip+scale for TENSION, but `SEQ_CORE_TensionBandMask` reads the held chord via BOTH
`SEQ_MIDI_IN_BusPCSetGet` (chord tones / L2c) AND `SEQ_MIDI_IN_BusLowestNoteGet` (bass → L0/root),
plus `global_scale_root_selection` + `keyb_scale_root`. Missing the held-chord inputs would have
silently staled a gripped TENSION track when a chord is played under it (wrong pitches). All folded
in; a 3-lens adversarial review (staleness / control-flow / wire-format) confirmed the signature
is complete (verified input inventory per processor), the store-after-render contract converges,
and LIMIT's exclusion is safe (purely source-state). **by-ear GO + HIL 2/2** (`test_render_perf.py`
rewritten around change-detection, parametrized lean + full-buffer): a STATIC armed field on all 16
renders **nothing** (max_dirty 0, duty 0%, ui_gap == baseline) EVEN on the 1024-byte drum layout
(the case that cost ~2469 µs/tick before); a no_dirty GRAVITY change re-renders all 16 (positive
control isolating the signature from `RenderDirtySetAll`). On the user's real set: all 16 gripped +
robotize + echo + humanize on every track → **CPU ~27%, no lockup, no drops.** +64 B flash,
**+65 B main RAM** (the 16×u32 signature array). New permanent regression guards: `CMD_RENDER_PERF`
probe (with a peak-`max_dirty` field for a race-free "tracks-armed" proof) + `board.render_perf()`
+ `tests/diag_render.py` (on-device by-ear tool, reports peak +2 UI gap live) + an OOB stale-tail
fix the bounded copy exposed (`SEQ_PAR_Get`/`SEQ_TRG_Get` bound by real layer/instr count, not just
MAX; the LIMIT `no_fx` layer bound). Plan `doc/plans/2026-06-26-render-changedetect.md` retired.
**Next ceiling discovered + DIAGNOSED (separate from this fix; PARKED by the user — no firmware
written, keep the render win).** At the extreme all-16-echo load only ~8 tracks' notes emit and
muting frees the rest. Box-side confirmed: the secondary screen shows activity on all 16 (the box
generates + schedules them) but no notes reach Ableton on the silent tracks (all 16 on ONE cable,
USB2). **Two undersized emission buffers**, both newly *reachable* only because the render wall no
longer kills the box first: **(1) the USB Tx ring** `MIOS32_USB_MIDI_TX_BUFFER_SIZE` = **64**
packages ([mios32_usb_midi.h](../include/mios32/mios32_usb_midi.h)) — all 16 burst on the same 16th
out one cable; the ring holds 64 and drains ~16/USB-frame (single Tx buffer, no double-buffering),
so the burst overflows and the *blocking* `MIOS32_USB_MIDI_PackageSend` gives up after its
10000-spin `timeout_ctr` and drops — *below* the scheduler, so `seq_midi_out_dropouts` stays 0
(the first report). **(2) the SEQ scheduler pool** `SEQ_MIDI_OUT_MAX_EVENTS` = **256** — echo×16
schedules a future-event queue past 256, dropped at schedule time and *counted* (`dropouts` > 0 —
the later report; also explains "first 8 play, rest dropped, muting frees slots"). **Fix when
unparked:** bump USB ring 64→512 (~+1.8 KB) + pool 256→512 (~+4 KB) — fits the ~9 KB free main RAM
(size the pool to the INFO-page "MIDI Scheduler: Alloc cur/MAX" peak); zero-cost partial lever =
spread tracks across USB1–4 (helps the ring burst, NOT the pool). The §8 emergent queue note re the
~95% wall is closed; this is the emission analogue, queued behind it.

**2026-06-26 (cont.) — the config-grain self-bus ALREADY EXISTED (TK, 2019); discovered, validated
by ear, capture-faithfulness mapped. §10(c)'s "build queued" was reinventing a shipped feature.**
Picking up the queued self-modulation work (§10(c)), a source check *before* building found the
config-grain self-bus **already in the tree**: it is the upstream `SEQ_PAR_Type_Ctrl` ("Ctrl")
par-layer type, added by TK in commit `164c068b` (2019-12-18, "MBSEQ: support for Ctrl Layer"),
inherited by this fork. A Ctrl layer calls `SEQ_CC_MIDI_Set(track, cc_number, value)` on its OWN
track each step (`seq_layer.c:491` drum / ~848 normal) — routing per-step values to that track's own
config params — and the UI shows **named labels** (`ctrl_labels[]` via `SEQ_CC_LABELS_Get(...,
enforce_ctrl=1)`: Directn. / S.Replay / S.Fwd. / S.JmpBck / S.Repeat / S.Skip / S.Interv / ClockDiv
/ TrkLen.), exactly the "labeled-parameter self-route" §10(c) proposed to build. Tick order is
correct (NextStep `seq_core.c:4141` advances, then GetEvents `:4346` applies the Ctrl write → step N
steers the N→N+1 hop; René/Eloquencer-style path modulation). **By ear: self-modulating direction +
progression "works great."** No new code. The whole "build queued" framing was invisible-in-plain-
sight — a cryptic name, upstream's loopback-drives-*another*-track framing, and this doc itself
saying "build it" over a 2019 bridge. **LESSON (reinforces §2 / CLAUDE.md "verify against source"):
grep the source before planning to BUILD a "queued" platform feature; upstream MIDIbox may already do it.**

**FREEZE/CAPTURE faithfulness — code-traced + by-ear GO; the capture-centric MVP loop now proven
end-to-end.** The two freeze paths treat self-mod OPPOSITELY. **Bounce-to-pattern**
(`SEQ_CORE_CaptureToSlot`/`CaptureToTrack`) does NOT bake the re-phrasing: `ResetGenerativeForBounce`
resets dir_mode/steps_* to forward, but the **Ctrl layer is on the PRESERVED list** (`seq_cc.c:139`)
and its par values are kept, so the destination **re-runs the same modulation (preserve-and-REPLAY).**
It reproduces the heard order only when the modulation is deterministic *and* starts from the same
phase, and DRIFTS on: random directions (Directn. 4–6 draw `t->random_traverse_state`, which is
runtime-only and NOT saved in the pattern → re-seeds fresh on load, `seq_core.c:3758` = a different
walk); progression params the Ctrl layer doesn't itself drive (zeroed by the reset); or multi-bar
counter phase. This is exactly this doc's "the CC source layer is the editable artifact / not
bounce-bakeable," now confirmed — and it is what the user first read as "captured what I heard" then
"not fully accurate." The **UTILITY-held CAPTURE grab** (`SEQ_CORE_CaptureSpan` — re-sim when
stopped / tape when playing) IS faithful: it re-drives WITH the Ctrl layer live, records the EMITTED
notes in PLAYBACK order and materializes them into consecutive forward steps (baking the re-phrasing
into the note arrangement), memsets the dst par values to 0 to neutralize the inherited Ctrl layer
(`seq_core.c:2191`), and restores `random_traverse_state` from the frame (`:2273`) so **even random
directions reproduce.** **By ear: "worked perfectly using the utility method."** This proves the §8
capture-centric MVP loop end-to-end — *material (self-bus motion) → harvest (CAPTURE) → faithful
frozen pattern* — at a cost of ZERO new code (a 2019 feature + the already-built capture path).

**Parked (the only fork-era debt; neither is blessed-needed yet).** (1) **The dirty gate** —
`SEQ_CC_Set` unconditionally calls `SEQ_PATTERN_DirtySetTrack` (`seq_cc.c:523`), so continuous
self-mod (and the vestigial all-zero Ctrl layer a CAPTURE dst inherits) churns the ~290 ms
auto-writeback every step. Fix = a `seq_core_in_self_route`-style ambient flag + early-return in
DirtySetTrack (~2 lines). User has NOT reported the churn bothering play ("everything is mostly
working"), so it stays optional. (2) CaptureSpanPrepDst could null the baked Ctrl layer TYPE entirely
(its effect is already in the notes) → kills that churn on captured dsts + de-clutters. **Still
genuinely to-build (the note-grain half):** self-transpose / self-chord-mask (render-stack, born
bounce-bakeable; §10(c)) and self-arp (deferred, hard). TrkLen./ClockDiv self-mod carry the
immediate step-pointer wrap glitch (boundary-defer, `seq_cc.c:383-385`) — out of scope for
direction/progression.

**2026-06-26 (cont.) — CAPTURE lifted off the one-measure constraint; by-ear GO; HIL 216/216.**
The self-bus re-phrasing above was most interesting on a *longer/odd* base loop, but the UTILITY
grab refused any track that wasn't exactly one global measure (`spm != gspm → -8`). Lifting it
landed in two halves with a different reach each:
- **While PLAYING (the north-star) → ANY length.** The live tape records the emitted stream, so the
  grab window is a pure **tick-period slice** of the track's own loop period `P = spm·tps`
  (`SEQ_CORE_CaptureSpanTape` non-aligned branch) — traversal-agnostic, so it captures a 2-bar, a
  24-step polymeter, a sub-measure ostinato, or a self-modulating line identically. **By ear: "works
  great"** on 8/24-step + 2-bar self-mod tracks; HIL pins it note-for-note (incl. downbeat phase).
- **While STOPPED → one bar only.** The re-sim drive phase-aligns to the *global* measure, so it
  faithfully regenerates a 1-bar loop but rotates a multi-bar loop by a sub-measure amount (a HIL
  trace showed +11 steps on a 2-bar track). Multi-bar/odd stopped grabs now route to **"play to
  grab."** Fixing the stopped multi-bar drive (phase the drive to the track's own loop, not the
  global bar) is the queued **A2 kernel**; synch-to-measure support (route a synch'd track as a
  1-bar loop — the synch reset makes its audible loop the global bar) is a small queued follow-on.
- **Two FATAL bugs caught by adversarial trace-review *before* by-ear** (both in the multi-measure
  helper, neither visible to the build or the success/determinism HIL pins): (1) loop-boundary
  detection keyed on `frame->step==0`, but the frame is snapshotted in the tick PROLOGUE before the
  body's NextStep wrap, so `frame->step` holds the PRE-advance step (`==length` forward; an RNG value
  for random traversal) — never reliably 0; (2) a phase off-by-one — loop-start frames sit at
  `robotize_measure_ctr ≡ 1 (mod n)`, not `≡ 0` (the first frame lands at ctr=1 with FIRST_CLK
  suppressing that tick's advance). Both replaced by frame-count arithmetic (`e=(ctr-1)%n`,
  `win_o=e+k·n`) that reduces exactly to the original per-bar `FrameBack(k)` for n=1. **LESSON
  (reinforces §2 / CLAUDE.md): this capture timing is the "#1 hardware-validation item" — trust
  source traces over comments (a wrong struct comment fooled both me and the first review), and
  multi-measure PHASE needs a note-for-note HIL pin, not just success/determinism.** Plan +
  full bug write-ups: `doc/plans/archive/2026-06-26-multimeasure-capture.md` (not retired — A2/synch remain).

**2026-06-28 — Unified CAPTURE page → the CANVAS model; chord-aware; by-ear GO ("working great").**
The SONG button (a redundant PHRASE twin) became a unified Capture page: source = the live visible
track, B-row = destination track, GP row = destination pattern (number commits), datawheel = the GRAB
dial (`Save` detent = living deposit via `SEQ_CORE_CopyTrackLiveToSlot` / `1b..Kb` = frozen grab).
The build surfaced a reproducible **`!!!`** (TRKLEN `LENGTH > num_steps`) on real odd-length tracks —
and chasing it reframed the whole deposit:
- **Root cause (load-bearing, not cosmetic):** a frozen grab sized the dst to `dst_steps = K·spm`,
  but the TRG layer is bit-packed (`num_steps8 = steps/8`) so a non-÷8 `dst_steps` FLOORS the trg
  geometry while LENGTH is set unclamped → `!!!`. Worse, `SEQ_TRG_Set` REFUSES gate writes past
  `num_steps8`, so the tail-bar steps got par values but **no gate = silently muted notes**. A
  13-step source made it obvious — only K=8,16 are ÷8-clean (13 ⟂ 8), everything else broke.
- **The fix is a model, not a patch (user's call):** capture **NEVER resizes the dst's max length.**
  The grab tiles its `W=K·spm` window into the dst's **fixed canvas** (`SEQ_CORE_TileWindowToCanvas`,
  byte-faithful), so the floor mismatch is *structurally impossible*. Two fit modes, **GP1 encoder
  toggles `Fit:FILL/LOOP`**: FILL tiles across the whole canvas + loops at the canvas (grid-locked
  with the other tracks; the seam when `W∤canvas` is a wanted "rad hiccup"), LOOP loops at the window
  (drifts free, polymetric). LENGTH ≤ canvas always.
- **Chord-aware:** a CHORD event-mode source stores chord INDICES (`SEQ_PAR_Type_Chord1`), which the
  note-stream materialize can't round-trip (it writes raw notes into the index slot). `SEQ_CORE_
  CaptureChordWindow` bypasses the tape and copies the source's chord par loop directly (indices +
  velocity + gates), tiled. Freezes the chord loop *as it stands at grab*; static transpose re-applied
  (the index carries none); held-key transposer dropped (a live gesture).
- **Three adversarial review workflows were load-bearing again** (§2 #8): caught the canvas-overflow
  RE-INTRODUCING the bug (clamp the canvas ×8 to the captured layout's buffer budget), the chord
  synch-tiling garbage read (tile by `min(length+1, spm)` clamped to allocation, not `gspm`), and the
  chord transpose-loss. The first would have re-shipped the very bug the redesign removes.
- **UNCOMMITTED at GO**; HIL capture family 71/71 + 4 new canvas pins green, full suite re-run before
  commit. **Deferred (none block):** #3 "as-heard" windowing for note grabs (still loop-aligned;
  phase-offset = mid-run-restart edge) — **SHIPPED 2026-06-28, see below**; cross-pattern canvas uses the LIVE dst
  geometry not the slot's stored; NOTE-mode multi-bar grabs stay mono (the melodic-mono fence — chords
  only via the chord path or `Save`). Plan: `doc/plans/2026-06-27-unified-capture.md` (retire on commit).

**2026-06-28 — `phrase_drift` leak in the three slot-capture verbs — FIXED.** `SEQ_CORE_
CaptureToSlotTrack` / `CaptureSpanToSlotTrack` / `CopyTrackLiveToSlot` each do a staged
load-modify-save: read the target slot INTO the live dst group (CC-replay through `SEQ_CC_Set`
raises drift for that group), modify one track, write the slot, then restore the dst group's
live RAM byte-identical. They already snapshot+restore `seq_pattern_dirty` around this; they did
NOT restore `phrase_drift`, so a clean dst group came out flagged "deliberately edited" → the
next phrase recall's drift-gated writeback paid one spurious ~290 ms flash SAVE. **Fix:** mirror
the dirty dance for drift — snapshot `dst_group`'s drift bit before the slot load, restore it
after (new `SEQ_PATTERN_DriftGroupGet` / `DriftGroupRestore` accessors, since `phrase_drift` is
static to seq_pattern.c). The re-sim/tape drive can't leak the SOURCE group: generator wander
during the drive runs inside the `seq_generator_in_automutate` window, which suppresses drift.
+32 B text, 0 RAM. New per-group drift mask on the `CMD_PHRASE_META` DRIFT_QUERY reply (`reply[3]`,
backward-compatible) drives a 3-pin regression (`test_capture_drift_leak.py`, one per verb).

**2026-06-28 — As-heard windowing (deferred #3) — SHIPPED, by-ear GO + HIL 234/234.** A
while-PLAYING note grab was always **loop-aligned** (GRID): the tape window kept the last k
COMPLETE loops, ending at the last loop downbeat. New **Phase: GRID/HEARD** toggle (GP2 encoder
on the Capture page, **default GRID** — nothing existing changes feel). HEARD ends the window at
the **playhead**: `win_end = now`, `win_start = now − k·P` — the last k bars exactly as they
sounded, the deposit restarting from the grab phase (rotated off the source downbeat). It is
*simpler* than the loop-aligned code (purely relative to `now`, no boundary math → immune to the
mid-run synch/restart phase GRID assumes) and needs **no step-snap**: every source-grid note's
residual offset within its dst step is the same constant `< tps`, so the floor bucketing recovers
each step exactly — the capture is one global sub-step phase shift, inaudible on the dst's own
grid. **Tape (PLAYING) only**; STOPPED re-sim has no playhead and stays GRID. `phase` threads
through `SEQ_CORE_CaptureSpan` / `CaptureSpanToSlotTrack` (chord path ignores it — copies the
chord-index loop directly, melodic-only); legacy gestures pass GRID. `CaptureMaxK` needs no
change (HEARD reaches no further back than GRID). The `CMD_CAPTURE_SPAN` reply gained
win_start/win_end/tps so the note-for-note pin derives the deposit rotation race-free.
**Adversarial review (§2 #8, 4 lenses → verify) caught one real defect:** HEARD computed `P =
spm·tps`, which for a SYNCH_TO_MEASURE track on a foreign clkdiv doubles the period (`spm` is
forced to gspm while `tps` stays the track's own step_length) → wrong span/phase; GRID is immune
(reads real `bar_start` markers). **Fixed:** synch tracks derive `P = gspm·96` (the global
measure, = GRID's marker span). HIL: 4 new pins (`test_capture_as_heard.py` — window-ends-at-
playhead, note-for-note rotation, GRID-still-aligned, slot-track phase threading). Plan +
review write-up: `doc/plans/archive/2026-06-28-as-heard-windowing.md`.

**2026-06-28 — Foreign-clkdiv CAPTURE geometry — synch period-doubling + tick-based whole-measure
classification — FIXED (by-ear GO "everything worked"; HIL 241/241).** A review/harden pass on the
shipped synch-to-measure routing (57dc55af) found two latent bugs, both on a track whose `step_length
≠ 96` (i.e. NOT the global 16th grid), both from conflating a *global-16th-step* count with the
source's *own* `tps`. **(1) Synch period-doubling:** `SEQ_CORE_CaptureLoopSteps` returns `gspm` (a
global-16th count) for a synch track, but the dst was dimensioned with the source's own `tps` — so an
8th-note synch track (tps=192) got a 16-step dst at 192 ticks = 2 bars (re-sim drove two bars → the
bar captured twice; the tape window held one bar → bar + a silent bar). **(2) Whole-measure
misclassification:** the gate `(length+1) % gspm == 0` compared own-steps to global-16th-steps, valid
only at tps=96 — so a non-synch 8th 16-step loop (16·192 = 3072 = TWO bars) read as one measure
(captured half), and an 8th 8-step loop (one true bar) was refused stopped. **Fix = separate the
units** with three helpers (`seq_core.c`): `SEQ_CORE_CaptureTps` (factored fallback),
`SEQ_CORE_CaptureDstLoopSteps` (dst step count = `gspm·96/tps` for synch, `length+1` otherwise — the
deposit/drive geometry), and `SEQ_CORE_CaptureLoopMeasures` (measures-per-loop judged in TICKS:
`loop_ticks = (length+1)·tps`, `n = loop_ticks/(gspm·96)`, 0 = unaligned, synch ⇒ 1 — the
gate/classifier). `CaptureLoopSteps` stays in global-16th units for the window n-math. Applied at
re-sim, tape, chord-window, and the `CaptureMaxK` ceilings. Normal 16th-grid tracks are byte-identical
(dst_spm == length+1, n_meas == length+1/gspm). Bonus: an 8th 1-bar loop is now grabbable STOPPED (was
refused). HIL: +7 pins (`test_capture_synch_measure.py` foreign-clkdiv ×4, `test_capture_clkdiv_alignment.py`
×3), full suite **241/241**. **Lesson reaffirmed (review-before-commit):** the original synch bundle's
tests all ran at the 16th grid, masking the tps≠96 case; the adversarial harden pass surfaced it.

**2026-07-03 — G0: the operating grammar, proven on ChordMask (SHIPPED, by-ear GO; committed main 35cfd4b7).**
The turn from the UX study (`doc/plans/2026-07-02-ux-study-fluid-flow.md` §3.5): the panel's
warts are one cause — *no operating grammar*. The resolution is **make the grammar the constant and
processors the data** — five invariants (one rack / one selector / one operating surface / one
readout / one descriptor growth path). G0 built the **thinnest vertical slice** to test the whole
model by hand: operate **one** real processor (ChordMask) end-to-end through the uniform surface.
- **Decisions locked (all by-ear-confirmed):** rack = a new **`PROC` sel-view** (the B-row becomes
  the visible track's 4-slot render stack); operate = the **GP encoder bank** (GP1 strength / GP2
  bus / datawheel fine-ride, **push = snap-to-default**, pass-through at 0); readout = **B-row rack
  LEDs** (green occupied / amber focused / pass-through winks) + **GP-row 12-PC mask paint** + a
  **persistent LCD overlay** that *owns* both lines while latched (the page draw is suppressed, so
  nothing fights it). All edits route through **`SEQ_CC_Set`** (the golden path) — the UI never
  pokes `seq_processor_stack`; it only reads it for the readout.
- **PROC home = the stolen LIVE button** (latched, not held; its LED is now the "in PROC mode"
  lamp). The displaced `FWD_MIDI` is **pinned permanently ON** — the config reader now ignores a
  persisted 0 so it can never strand OFF with the toggle gone (caught in review, not by ear).
- **Known coupling (not a bug, a Phase-C bridge):** ChordMask's slot presence *is* the track's
  exclusive playmode, so double-tap-add sets ChordMask playmode and double-tap-remove returns to
  **Normal** (not the prior mode). Acceptable for the G0 tenant; dissolves when a processor becomes
  a truly independent slot.
- **Verdict → license:** GO. The B-row-select → encoder-sculpt → GP-row-readback loop feels more
  like playing than menu-editing. **G1 is licensed** — migrate the other slots (Tension / Pitch /
  Limit) and the FX/generator pages onto the same descriptor grammar, collapsing the page-scatter.
  What G0 proved unlocks the rest: every other processor becomes "fill in the descriptor + point
  the encoders at its params."

**2026-07-05 — G1: the rack migrated onto a param-list grammar; PROC became a real page (SHIPPED, by-ear GO; committed main 82173e5c).**
G0's ChordMask hardcode is gone. The operating surface is now **data-driven**: a per-processor
**param list** (proto-descriptor — study §3.5 invariant 5) `{label, kind, cc, range, default}` keyed
by the **fixed slot index**, iterated by the encoder routing / push-to-default / LCD readout. Adding
a processor to the grammar = filling in a table row. Heterogeneous backings hide behind `kind`:
plain CC, bus (A–D), **signed 4-bit nibble** (transpose — encoding verified against the consumer,
not the display page), a **MODE_FLAGS bit** (FTS), and the **global GRAVITY s8** via its setter. All
four rack slots now operate through the identical movement (Pitch: Semi/Oct/FTS · ChordMask: Str/Bus
· Tension: Grip/**Grav** · Limit: Lo/Hi). Keyed by fixed slot index means Pitch/Tension/Limit stay
focusable while neutral (id==NONE) — **dark rack key = true pass-through** (invariant 4), and turning
a dial brings the slot alive via its sync. Double-tap a slot = its on/off (ChordMask toggles playmode;
the param-driven slots reset to pass-through).
- **The PROC presentation was reworked twice, by ear, to a firm conclusion — a DEDICATED PAGE, not an
  LCD overlay or a latched-everywhere sel-view.** v1 (a persistent full-LCD overlay that suppressed
  the current page) read as a modal takeover → rejected ("make it like other modes"). The fix is
  `SEQ_UI_PAGE_PROC`: it owns the LCD *normally*, like FX/GRAVITY. **The processors are render-stack
  DSP and run continuously regardless of page** (leaving PROC never silences them — the same as
  leaving EDIT doesn't stop notes); **LIVE is pure navigation** to the rack to adjust the dials, not
  an on/off switch. The old "Processor Rack on/off" message (which wrongly implied LIVE gated the
  audio) is gone. Operate surface is **page-scoped**: `sel_view=PROC` set in the page Init, cleared
  in its exit callback, so the B-row rack / GP-encoder operate / GP-row mask are live on the page and
  revert on leave.
- **Lesson (interaction-model, by-ear):** the grammar's "operate the focused processor from *anywhere*"
  aspiration (§3.5) fought legibility — a persistent readout HAS to live somewhere, and hijacking the
  current page's LCD to provide it feels modal and wrong. Making PROC a page you *navigate to* (and
  the DSP stay running behind you) is the consistent resolution. "Operate from any page" is dropped
  for now; revisit only if a live need surfaces.
- **Verdict → G2 next.** GO. Operating Limit/Pitch/Tension feels like the *same* movement as
  ChordMask. Remaining: the FX/generator pages aren't render-stack processors yet, so bringing them
  onto the grammar is a separate lift (make them rack processors first) — call it **G1.5**. **G2** =
  lock the descriptor as the extension point (formatter registry, defaults, optional CUSTOM surface).

**2026-07-05 — G1.5: emission FX onto the grammar via EXPOSE-IN-PLACE; Echo the reference tenant +
Pitch completed (SHIPPED, by-ear GO; committed main ce9a9f70).**
The G1 "make the FX render-stack processors first" plan was **overturned by recon** and the user's
steer. A 7-agent read of Echo/LFO/Robotize/Humanize/Groove found the render-lift is *architecturally
wrong* for all five — the render stack is a fixed-geometry per-step buffer rewrite, but Echo is a
**scheduler** (posts to future ticks), LFO a **free-running CC stream**, Robotize/Humanize **per-event
stochastic**, Groove **timing** (negative delays can't bake). And the reason to lift is **already
spent**: `SEQ_CC_ResetGenerativeForBounce` + config-copy make them capture-faithful today (which is
*why* robotize was never migrated; §5 holds born-as-processors loosely). So G1.5 unifies the
**operation** (B-row select · GP encoders · LED/LCD readout — the ChordMask grammar) while the **DSP
stays at emission** — zero audio-path risk. The rule is bent, not broken.
- **The rack is now an ordered list of ROWS, not a walk of the 4 stack slots.** Each row is
  `PROC_ROW_STACK` (occupancy from `seq_processor_stack`) or `PROC_ROW_EMISSION` (occupancy derived
  from the effect's `tcc` CCs). `ui_focused_proc_slot` is a **row index** (stack rows first, so G1
  reads identically); `SEQ_UI_PROC_RowState()` is the single occupancy choke point. **Migrating an
  effect onto the grammar = adding a row** (a `proc_param_t[]` table + one occupancy predicate).
- **Echo = the reference tenant (row 5), fully realized.** All 7 dials on the encoders
  (Rpt/Dly/Vel/FbV/Note/Tick/Gate), each read out in its **own unit** via a new `fmt` field on the
  param descriptor (the seed of G2's formatter registry): Delay as a note-name in musical order
  (`Map*ToInternal`), Vel/FbV/Tick/Gate as **%**, Note as **±semitones**. Repeats is a masked RMW
  preserving the `0x40` disable bit; **double-tap the row toggles that bit** (bypass, count kept);
  **engage-seed** — turning Rpt up from a fresh (silent, velocity-0) echo seeds the neutral detents so
  it's audible at once.
- **Shared UI wins (all processors).** (1) **Encoder-aligned OPERATE grid** — each param in a 5-char
  cell at col i*5 so its label (line 0) / value (line 1) sit *under the matching GP encoder*; identity
  + custom readout (ChordMask mask, Pitch scale name/degree-note) on the right screen. This replaced
  the left-packed, misaligned readout that made the grammar hard to judge. (2) **Signed values print
  via a hand-rolled sign** — `SEQ_LCD_PrintFormattedString`'s vsprintf has **no `+` flag** (the
  literal-"3d" bug); a long-standing G1 display defect for Pitch Semi/Oct, cured here.
- **ChordMask engages from the rack.** Turning the **Str** dial up now enters the ChordMask playmode
  (`PROC_KIND_CM_STR`) — the mode gates the slot, so this is the "dial it alive" move the other three
  stack processors already had. All four now come alive identically: focus, turn a dial.
- **Pitch is now the complete pitch/harmony surface.** Chromatic transpose (Semi/Oct) + FTS + the
  **global** Scale/Root (the same globals the Scale page edits; name shown on the right screen) + a new
  **diatonic transpose "Deg"** dial: global, **FTS-gated** (a scale op, and FTS already arms the slot
  — no per-track plumbing), ±scale degrees via `SEQ_SCALE_WalkScale` (the keyboard's helper), persisted
  as `GlobalScaleTranspose`. A live `>note` readout beside it shows the tone the degree lands on
  (tonic walked Deg degrees). This is the first *new* transform of the run — proven by ear GO.
- **Verdict → G2.** GO ("really starting to come into shape"). Left as follow-ups: **per-track**
  upgrades where global was the POC (diatonic transpose; a per-track emission-row bypass shadow), the
  next emission tenants (**Groove** — the config-copy archetype — then LFO/Robotize), and **G2** =
  locking the descriptor (formatter registry, defaults, optional CUSTOM surface). Two latent notes:
  the emission double-tap + `RowState` hardcode `ECHO_REPEATS` (a 2nd emission row must touch them),
  and the ChordMask GP-mask/LCD compare a row index to the stack-slot constant (holds only while
  `proc_rows[]` keeps stack rows in slot order). Plan: `doc/plans/archive/2026-07-05-g15-emission-fx-on-the-grammar.md`.

**2026-07-05 — ChordMask gains a static Self mask + the EDIT page anchors on SOURCE, not the
processed mirror (both SHIPPED, by-ear GO; committed main c7209731 [Self mask] + 8aec049f [EDIT fix]).**
Two things landed on the ChordMask track in one run — one feature, one regression the feature flushed out.
- **ChordMask Self mask — a static, hand-set 12-PC target (feature).** ChordMask could only snap to a
  *live* chord read off a routing bus; now it works **standalone**. The **Bus** dial sweeps 0..4 —
  A..D (live bus) then **Sf** (Self). Self is a static 12-PC set stored on the track (new ext CCs
  `CHORDMASK_MASK_L` 0x9b / `_H` 0x9c in the free V3 block, so it persists with the pattern);
  **GP1..12 toggle pitch classes C..B**, LEDs paint the set, the right screen reads `M*:` (vs `M:` for
  bus-derived). Encoding: **bit 2 of `chordmask_bus` = Self, bits 0..1 = bus index** — so the bus index
  survives underneath for **Tension** (which shares the CC; `TensionSlotSync` and the testctrl path mask
  `& 0x03`). The bridge move: **selecting Sf while the static mask is empty seeds it from the current
  live bus chord** — grab a chord off the bus and freeze it, per the constraints-are-materials rule
  (Self at "empty" is a true pass-through you fill by ear). *Morph fix flushed out en route:* phrase-morph
  Loop A lerped every ext CC 0x80..0x9f as a magnitude — nonsense for **bitfield/mode** CCs (bus-hopping,
  Self-bit flicker, garbage PC sets, and a **pre-existing** corruption of the drum-scope masks). Fixed by
  **snapping** the chord-context family (BUS + both drum masks + both Self masks) at the morph midpoint,
  like the groove/transpose snaps already in Loop B.
- **EDIT edits the SOURCE note, not the mirror (regression fix, from the pitch-chain migration ~2026-06-10).**
  On a heavily-processed track (transpose+FTS+diatonic+chord-mask+Limit) the EDIT page went **dead** — a
  step's LED lit but you couldn't set a note, the C-3 gate-on default never fired, the encoder did nothing.
  Cause: EDIT *read* notes through `SEQ_PAR_Get` (the **output mirror** — the fully-rendered note the tick
  plays) while it *writes* the **source** layer. On a processed track those diverge: the mirror is pinned
  by the chain (Limit floors it above the C-3 sentinel), so the empty→default test and the increment base
  both read a non-moving nonzero value. Fix: **`SEQ_PAR_GetSource()`** — a source twin of `SEQ_PAR_Get`
  with identical index math returning `seq_par_layer_value[]`; every note read on the EDIT page (12 sites)
  points at it. **Editing/display now target the material; processors overlay non-destructively and are
  heard, not edited** — the durable rule for how the EDIT page relates to the render stack. Safe by
  construction: when nothing processes, **source == mirror**, so unprocessed editing is byte-identical.
  *Consequence to know:* on a processed track EDIT now shows the note you **programmed**, not the heard
  output; a "heard note" secondary readout is an easy future add if wanted.

**2026-07-05 — G1.6: Groove joins the grammar as the 2nd emission tenant, as a PAINTABLE
16-step shape (SHIPPED, by-ear GO; committed main).**
Groove was the design's named "config-copy archetype" and the explicit next emission
tenant after Echo. Bringing it on did the two jobs G1.5 predicted: it proved the
emission-row pattern generalises to a real 2nd instance, and it flushed out the two
`ECHO_REPEATS` hardcodes that flag had called. The user pulled the *rich* cut forward:
groove isn't a preset picker on the rack, it's a **surface you paint by ear**.
- **The row = four operate dials, mirroring Echo.** `Styl` (headline/occupancy — 0=off
  dark row, 1..6 presets, 7..22 custom templates; index in the cell, **name on the right
  screen** like Pitch's Scale; engage-seeds intensity so it's audible at once), `Intn`
  (intensity — scales the VPOS/VNEG template cells, so on the classic Shuffle it *is* the
  swing depth; true 0→max, pass-through at 0), `Sync` (phase reference Trk/RefS), and
  `Lane` (a UI-only selector — Dly/Len/Vel — for which template lane the GP row paints).
- **The GP row is the paintable shape.** On a *custom* style, GP1..16 toggle that step's
  selected-lane cell between 0 and `+intensity` (`VPOS`); the LEDs paint the lane's
  non-zero steps (presets show read-only; off = dark). First paint **expands the template
  to a full 16-step bar** (`num_steps=16`) and seeds intensity if still 0, so a painted
  step sounds immediately. This is groove's analogue of ChordMask's live 12-PC mask — the
  first emission tenant to get a 16-object GP-row surface.
- **Bypass = a new bit-7 `disable` in `groove_style`**, mirroring Echo's `0x40` on
  `ECHO_REPEATS`: **double-tap the row** flips it — a live A/B against straight that keeps
  the dialled config. Two-line DSP guard in `SEQ_GROOVE_DelayGet`/`_Event` (the only audio-
  path touch, both melodic + drum). It rides the same CC that morph already snaps and that
  capture copies-as-config, so it's faithful for free.
- **The G1.5 hardcodes are now generalised.** `proc_row_t` carries `{occ_cc, disable_mask}`;
  `SEQ_UI_PROC_RowState` and the B-row double-tap read the row's descriptor instead of
  naming `ECHO_REPEATS`. Echo = `{ECHO_REPEATS, 0x40}`, Groove = `{GROOVE_STYLE, 0x80}`.
  Row identity for the Groove-specific GP/LCD branches is a **params-pointer compare**
  (`proc_params_groove`), reorder-safe — deliberately NOT the stack-slot index compare the
  ChordMask branches use (those only hold while the 4 stack rows keep slot order).
- **Persistence:** paint sets a dirty flag; `SEQ_UI_PROC_page_Exit` writes `MBSEQ_G.V4`
  under `MUTEX_SDCARD` on leave — the same file + gesture as the stock TRKGRV exit.
- **Two consequences to know.** (1) Groove **templates are global** (the existing groove
  architecture — you pick *which* template per-track via Styl, but the template cells are a
  shared pool): painting edits the shared slot, so other tracks on that same style move too.
  Style/intensity/sync/disable are per-track. (2) PROC paint forces a **16-step bar** and
  toggles cells to `+intensity` only — **negative (VNEG) / graded per-step values and
  short-loop customs stay on the stock TRKGRV page**. Both deferred, by choice.
- **Verdict → G2.** GO. Groove operates and paints like the same movement as
  ChordMask/Echo. Plan: `doc/plans/archive/2026-07-05-g16-groove-tenant.md`. Left as before: the
  **G2** descriptor lock (formatter registry, defaults, optional CUSTOM surface — the Lane
  selector + VPOS-toggle paint is a hint at what a per-processor CUSTOM surface wants), and
  the per-track upgrades where global was the POC.

**2026-07-05 — G1.7: LFO joins the grammar, the rack's first MODULATION SOURCE (SHIPPED,
by-ear GO; committed main).**
The 3rd emission tenant after Echo/Groove, and the load test that licensed G2 by pull. Six
OPERATE dials: **Wave** (headline/occupancy 0..25, name on the right screen, bit-7 disable
like Groove; engage-seeds a Vel target + depth so it's audible at once), **Amp** (the
rack's first BIPOLAR dial — logical -128..+127 around raw 128, centre = pass-through),
**Rate** (steps/cycle, shown +1), **Phas** (%), **Targ** (Note/Vel/Len/CC — CC = the
free-running `FastCC_Event` stream, gated by `lfo_cc` + clearing `EXTRA_CC_OFF`, NOT the CC
enable bit), **CC#**. CUSTOM surface = a **waveform palette** on the GP row (tap to pick;
the lit key = current shape) — a third distinct GP-row content after ChordMask's mask and
Groove's step-shape. 2-line DSP guard in `seq_lfo.c` (Event/FastCC early-out on bit 7,
ValueGet masks). Plan: `doc/plans/archive/2026-07-05-g17-lfo-tenant.md`.
- **Friction it surfaced (the G2 pull):** (1) a 3rd bespoke CUSTOM branch on the same three
  global sites; (2) the descriptor's `s8` ranges can't hold 0..255 (Rate capped at 128);
  (3) engage-seed is per-processor imperative code (Echo/Groove/LFO each hand-roll it).
  Three worked examples = rule-of-three → G2 licensed.

**2026-07-06 — G2 (part 1): the PLANE toggle, proven by migrating Robotize as a two-faced
unit (SHIPPED, by-ear GO; committed main).**
Floated by the user from use: a two-faced unit (Robotize params ↔ the Robotize Loop) needs
to *flip back and forth*; the split-view "bespoke on the right half" idea was dropped — a
bespoke face gets a **full plane**, reached by a uniform toggle. This is §3.5's plane model.
- **The plane mechanism (reusable, minimal-churn):** `proc_row_t` gained an OPTIONAL 2nd
  plane (`params2/n_params2/face2`) rather than wrapping every row in a `planes[]` array —
  only Robotize sets it. A global `ui_proc_plane` (0/1, reset to 0 on focus change / page
  entry); `SlotParams()` is plane-aware; a top-right `OPER`/`LOOP` cue names the current
  plane. `face2` (a `proc_face_t` id) drives the bespoke GP-row/button/readout branches by
  **descriptor id**, not a per-slot compare — a step toward the full custom hook.
- **The toggle gesture is Up/Down** (NOT ‹/› — `seq_hwcfg_button.left/right` default to
  `0xff`/disabled on this panel; that was the "flip doesn't work" bug). Any nav button
  FLIPS the two planes (toggle is exact for 2; split into prev/next when a 3rd lands).
- **Occupancy generalised again:** `enable_cc`. An emission row's ENABLED bit is either a
  mask in `occ_cc` (Echo 0x40 / Groove·LFO 0x80) OR a separate CC — Robotize splits
  occupancy (`PROBABILITY>0`) from enable (`ACTIVE`). RowState + the double-tap read the
  descriptor. New `PROC_KIND_ACTION`: a momentary dial where **encoder-push executes** (the
  one non-snap push) — Robotize's Reseed/Freeze.
- **Robotize, two planes.** Plane A OPERATE: Prob (headline — engages `active` + seeds the
  per-dim ranges so the probability dials bite), Note/Vel/Len/Oct/Skip. Plane B LOOP
  (`PROC_FACE_ROBOLOOP`): the GP row is the 16 bar-anchors (pool lit, playhead winks, **tap
  = reroll**); dials Cyc/Pal/Strt/Rot + action dials Rsd/Frz (push = reseed / freeze last
  Cyc bars, default 4). Stock `fx_robotize`/`robomold` pages KEPT for the deep config
  (per-dim ranges, exotic probabilities, the step mask, sync-to-master). Plan:
  `doc/plans/archive/2026-07-05-g2-planes-robotize.md`.
- **Verdict → G2 continues.** GO ("its good"). The plane model is validated. Left: the
  formatter/defaults registry, a `planes[]` generalisation + a CONFIG plane to pull the
  deferred Robotize params onto the grammar, and folding the bespoke CUSTOM branches
  (ChordMask/Groove/LFO) into the `face` hook now that Robotize proved it.

**2026-07-07 — G2 (part 2): PitchGen onto the grammar — the rack's first GENERATOR row, a
genuinely CONTINUOUS/self-mutating tenant (SHIPPED, by-ear GO; committed main).**
A "what's next" design conversation surfaced that the dynamic pitch Turing machine the user
was picturing to build **already exists and ships** — `seq_generator.c`, a 64-step self-
mutating loop (lock/depth/contour/anchor/roll/bounce) running live on drum + normal tracks
via its own dedicated page. The actual gap is a **trigger** Turing machine (GENERATE's five
types — Eucl/CA/Poly/Sub/Lsys — are all static one-shot fills, nothing runs live). User's
call: rack-ify the existing PitchGen FIRST, both because it's the smaller lift and because
it proves the rack can host a truly continuous tenant (not just emission FX or config-copy)
before the trigger machine needs the same slot shape.
- **A new rowkind, `PROC_ROW_GENERATOR`.** PitchGen's state is a `SEQ_GENERATOR_*` **pool-
  slot allocation** (ENGAGE/DISENGAGE/BOUNCE), not a CC — `{occ_cc, disable_mask/enable_cc}`
  can't express it. Occupied = slot allocated; enabled = `SEQ_GENERATOR_IsEngaged`; strength
  = `mutation_rate` (an acknowledged-imperfect proxy — Rate=0 is a legitimate *engaged*
  frozen state, not silence, unlike every other row's kind-0-means-off).
- **B-row double-tap = ENGAGE ⟷ DISENGAGE**, not a headline-dial-up gesture — there's no
  clean 0-means-off dial here, so the toggle carries occupancy directly. Surfaces the stock
  page's own ENGAGE failure reasons (pool full / bad track / no Note layer assigned).
- **Plane A OPERATE:** Lo/Hi (range), Rate/Dpth (touch probability / perturb-vs-reroll
  depth), Cont (reroll bias Uni/Lo/Hi/Tri), Roll (ACTION — on-demand reroll of unlocked
  steps). Dials no-op pre-ENGAGE, printing dashes — mirrors the stock page's own contract
  exactly, not a new rule.
- **Plane B STEPS** (`PROC_FACE_PITCHGEN_STEPS`): Win (0..3, UI-only, which 16-step quarter
  of the 64-step loop the GP row shows — mirrors Groove's Lane selector), Anc/Snp/Bnc
  (ACTIONS — snapshot identity / hard-restore it / freeze-into-source-and-free-the-slot, the
  generator's own harvest verb). **GP row = LOCK toggle for the window** — the paintable-
  shape idiom's 4th tenant (ChordMask mask → Groove step-shape → Robotize bar-anchors →
  this). Target resolution (`gen_instr`/`gen_par_layer`: drum = cursor instrument, normal =
  cursor's Note layer if it is one else the linked layer) is duplicated verbatim from the
  stock page — small, no cross-file coupling, the PROC module stays self-contained.
- **The stock PITCHGEN page is unchanged and shares the same pool slot** — both are views
  onto one engine, not two engines; a tweak on either surface is visible on the other.
- **Verdict → G2 continues.** GO ("works great"). Proves a rowkind whose backing is neither
  a stack slot nor a CC — the descriptor now spans all three shapes state can take in this
  codebase. Plan: `doc/plans/archive/2026-07-07-g2-pitchgen-tenant.md`. **Next: the trigger Turing
  machine** — the genuine gap — now with a proven slot shape (rowkind + 2-plane + GP-row-
  paint) to build toward, plus the earlier-surfaced insight that Robotize's bar-anchor loop
  is the *same* register-primitive at bar-granularity — worth a consolidation look once the
  trigger engine exists and the shared shape is undeniable across three implementations.

**2026-07-07 — G3: the trigger Turing machine, extending `seq_generator.c` with an
independent key-space (SHIPPED, by-ear GO — "so far so good"; committed main).**
The genuine gap named a few turns earlier: GENERATE's five types (Eucl/CA/Poly/Sub/Lsys)
are static one-shot fills — nothing writes triggers *live*. This builds a second Turing
engine reusing PitchGen's proven mechanics (lock/rate/depth/anchor/roll/bounce), now
writing 0/1 into a trigger layer instead of a note value into a par-layer.
- **Extended the existing pool rather than building a separate one (recon-driven call).**
  `seq_generator.c`'s pool is wired into UNDO (`SEQ_CORE_JournalArm`), FEARLESS SWITCHING,
  the CAPTURE ring, and slot save/restore — ~10 call sites in `seq_core.c`, all via
  `TrackSnapshot`/`TrackRestore`/`TrackClear`/`TrackEngagedCount`. A standalone pool would
  need all of that re-wired or would silently leave "ghost" engaged generators after an
  UNDO/switch — a real bug, not a hypothetical. Extending inherits it for free.
- **A size-neutral mode discriminator.** Renamed the struct's old alignment-pad byte to
  `trg_layer_p1`: `0` = PITCH mode (every existing slot defaults here via memset — zero
  behavior change for the shipped pitch generator, confirmed by matching every refactored
  function's body against the original); `N>0` = TRIGGER mode, targets trigger-layer
  `N-1` (the same "0=unassigned,index+1" convention `seq_trg_assignments_t` already uses
  elsewhere). `range_min` doubles as **density** in TRIGGER mode; `range_max`/
  `contour_shape` go unused there — no analogue for a boolean (a coin flip has no
  distribution shape), an honest divergence, not an oversight.
- **Independent key-space, built in full (user's call).** A single melodic track always
  resolves generator instrument 0 for both kinds, so a shared key space would make
  pitch-gen and trigger-gen mutually exclusive on exactly the highest-value case — one
  track, decoupled pitch + rhythm. Added a second sparse index (`pool_index_trg`), same
  shared physical 64-slot pool underneath. ~8 (track,instrument)-keyed lookups
  (Get/IsEngaged/Disengage/Bounce/Anchor/Snap/LockToggle/Roll) got a `Trg`-prefixed twin,
  refactored through a shared static core parameterized by which index table to consult —
  the pointer-based helpers (Lock/MultGet/Set) needed no twin. **`Roll` gained a real
  correctness fix**: it used to walk the whole pool by track membership only, which would
  have rerolled the sibling engine's slots too once both shared one pool; now mode-filtered.
  `TrackSnapshot`/`Restore`/`Clear`/`EngagedCount`/`SlotSet` all extended to cover **both**
  key-spaces — otherwise the exact "ghost generator" risk extending the pool was meant to
  avoid would have reappeared, just scoped to the new key-space instead.
- **Mutation semantics for a boolean register.** `mutation_depth>=127` = reroll (Bernoulli
  @ density, same threshold pitch uses for full-reroll); 1..126 = flip (toggle) — no
  graduated "how far" is possible for a 2-state value, so the continuous ±depth window
  collapses to one outcome across that whole range. Coarser than pitch's dial, flagged not
  hidden. Both draws share the slot's own xorshift stream — same deterministic/seekable
  discipline as pitch.
- **The rack row (TrigGen, row 10) mirrors PitchGen's shape exactly**: B-row double-tap =
  ENGAGE⟷DISENGAGE (no dial here has clean 0-means-off semantics either); Plane A OPERATE
  (Dens/Rate/Dpth/Roll, no Contour); Plane B STEPS (`PROC_FACE_TRIGGEN_STEPS`: Win + GP-row
  LOCK toggle + Anc/Snp/Bnc). Target resolves the track's assigned **Gate** trigger layer
  (mirrors PitchGen targeting the semantically-loaded Note layer). `PROC_ROW_GENERATOR` is
  now shared by two tenants living in different key-spaces — disambiguated by row identity
  (`IsTrigGen`/`IsPitchGen`) at each of RowState/double-tap/GP-button/GP-LED/readout, the
  same shape every prior tenant's branches already take. Plan:
  `doc/plans/archive/2026-07-07-g3-trigger-turing.md`.
- **Verdict → the fusion-instrument thesis has its second half.** By-ear GO covered
  TrigGen alone (engage/sweep/lock/anchor/snap/bounce) and — the actual point — PitchGen
  and TrigGen running **simultaneously on one melodic track**, pitch and rhythm decoupled.
  +1344B flash total, +256B RAM (one new 16×16 index table). Left: the noted consolidation
  candidate (PitchGen's step-register and Robotize's bar-anchor register are now provably
  the same primitive at two granularities, with a 3rd data point); G2's formatter/defaults
  registry and folding the bespoke CUSTOM branches into the `face` hook.

**2026-07-08 — G2: the formatter/defaults registry, collapsing 6 tenants' duplication
(SHIPPED, by-ear GO; committed main 1d336c9c).**
Named as a follow-up at every milestone since G1.5 and re-deferred five times running
(G1.6/G1.7/G2 part1/G2 part2/G3). A grounded survey first (not from memory — CLAUDE.md's
own rule) found the rack already had a real partial registry (`proc_param_t.deflt`,
`proc_fmt_t`/`fmt`), just under-used in two spots — not a stub, so the scope narrowed to
those two:
- **Defaults.** Each tenant hardcoded its own first-engage seed values inline at its
  `ParamWrite` 0→on transition, duplicating numbers that already sat in that row's
  `deflt` field (only consumed by `SlotReset`/push-to-default before this). Added
  `proc_param_t.eng` — an engage-seed override, 0 = same as `deflt` — needed only where
  "make it audible on first touch" differs from the pass-through detent (Groove Intn=32,
  LFO Amp=+96, Robotize Note/Vel/Len/Oct=5/32/32/1; Echo's 6 dials all happened to have
  `eng==deflt` already). One shared `SEQ_UI_PROC_SeedRowDefaults(track, headline)` finds
  its row by `params`-pointer identity (the same reorder-safe idiom `IsGroove`/`IsLFO`
  already used) and walks `params[1..n-1]` (index 0 = the headline just written by the
  caller), skipping `PROC_KIND_ACTION` and any param whose seed resolves to 0.
- **The "untouched" test is per-field, not a proxy** — a genuine tightening, not just a
  refactor. Echo's old gate checked ONLY Velocity before blindly overwriting all six
  dials (a hand-tuned FbNote sitting at raw 0-adjacent-but-Velocity-still-0 would have
  been silently stomped); LFO's Target had no guard at all (always reset to Vel on every
  re-engage, discarding a deliberate Len/CC choice). Both now respect any dial the user
  already shaped, matching the guarantee the original comments claimed but didn't fully
  implement. One kind needs its own predicate: `PROC_KIND_LFO_AMP`'s raw-0 boot state
  decodes to logical −128, and any non-positive depth (not just literal 0) still wants
  the seed, so it checks `raw <= 128` instead of `raw == 0`.
- **Status-line dispatch.** The right-screen "line 1" custom readout (style/waveform
  names, engaged state, loop status) was a 7-way if-chain (`slot==PITCH_SLOT`,
  `slot==CHORDMASK_SLOT`, `IsGroove`/`IsLFO`/`IsRobotize`/`IsPitchGen`/`IsTrigGen`) with
  zero table backing. Added `proc_row_t.status`, a per-row function pointer (NULL for
  Tension/Limit, which never had custom text); each tenant's block became its own named
  function (PitchGen/TrigGen share one body, parameterized by which key-space's
  Get/IsEngaged pair to call — the same shared-shape idiom the rest of the rack already
  uses). `proc_rows[]` moved to designated initializers now that `.status` is sparse
  (cleaner than threading zeros through 11 positional fields). `IsRobotize`/`IsPitchGen`
  had no other callers and were deleted; `IsGroove`/`IsLFO`/`IsTrigGen` remain (used by
  `RowState` + GP-row button handlers).
- **Net effect**: flash size dropped ~248B from collapsing the duplication (not a
  feature — a side effect of deleting dead literal copies).
- **Verdict → G2 continues.** By-ear GO on the flashed firmware across all 7 tenants'
  engage-seeding and right-screen readouts. Left: folding the bespoke CUSTOM GP-row/
  button branches (ChordMask/Groove/LFO still hand-roll theirs) into the `face` hook now
  that Robotize proved the mechanism. The step-register consolidation candidate
  (PitchGen/Robotize/TrigGen) was investigated (2026-07-08) and found NOT to be the same
  primitive — parked as a hybrid idea in §10, not pursued.

**2026-07-08 — G2: fold ChordMask/Groove/LFO's GP-row surfaces into the face hook (SHIPPED,
by-ear GO; committed main 90b9bc5e).**
The last of the four follow-ups left after the formatter/defaults registry. Robotize/
PitchGen/TrigGen's bespoke GP-row surfaces already dispatched by descriptor id
(`proc_face_t`/`.face2`) — but only for a row's OPTIONAL SECOND plane. ChordMask's Self-mask
paint, Groove's paintable 16-step shape, and LFO's waveform palette live on each row's ONLY
plane, so they'd never had a face id to hang on — their GP-row button and LED branches were
still ad-hoc identity checks (`ui_focused_proc_slot == SEQ_CORE_CHORDMASK_SLOT`, `IsGroove`/
`IsLFO` params-pointer compares).
- **`proc_row_t.face1`**, paralleling `face2` — a face on the PRIMARY plane. `SEQ_UI_PROC_
  CurFace` now reads `face1` on plane 0 / `face2` on plane 1 (previously hardcoded
  `PROC_FACE_NONE` for plane 0, since only the 2nd plane had ever carried a face). New ids:
  `PROC_FACE_CHORDMASK_SELF`, `PROC_FACE_GROOVE_PAINT`, `PROC_FACE_LFO_PALETTE`.
- **Pure identity-check swap** — every inner behavior (mask toggle, lane paint, palette
  pick, the Self/bus-mode branch) is untouched; only the OUTER gate moved from a slot-index
  or params-pointer compare to `SEQ_UI_PROC_CurFace(slot) == PROC_FACE_*`, the same idiom
  the 2nd-plane faces already used. `IsGroove`/`IsLFO` had no other callers and were deleted.
- **Deliberately left alone**: ChordMask's B-row double-tap bypass gesture (still its own
  `slot ==` check) — that's the row's engage/disengage gesture, orthogonal to the GP-row
  paint surface, out of scope for this fold.
- **Verdict → G2's four named follow-ups are now all closed** (formatter/defaults registry,
  the note-display velocity-gate bug, the step-register consolidation investigation, and
  this fold). By-ear GO — "everything working the same as far as I can tell," which is
  exactly the bar for a pure dispatch-mechanism swap. No new open G2 thread named; next
  rack work is whatever the ear asks for from here, not a queued follow-up.

**2026-07-08 — Tension row: the GRAVITY cockpit's visualization ported in, plus a new
zone-jump gesture (SHIPPED, by-ear + by-eye GO; committed main 5dd30fb0).**
The first rack work after G2's four follow-ups closed — user-initiated, not a queued item.
The rack's Tension row (Grip/Grav, two bare numbers) never got any of the dedicated GRAVITY
cockpit page's (`seq_ui_gravity.c`, §8/§9 2026-06-09) real visualization: the zone name
(DETENT/SCALE/CHORD/DRONE pulling, LEAN/RUB/SLIP pushing) and the bipolar meter. Ported it
in, then the user pushed further into a genuinely new gesture, not just a display port.
- **Reuse, not a 2nd copy.** `zone_name`/`tension_meter` (both file-static in
  `seq_ui_gravity.c`) exposed as public `SEQ_UI_GRAVITY_ZoneName`/`TensionMeter` — one
  zone-threshold source of truth for both the dedicated page and the rack row. The
  dedicated page itself is untouched, still the same page it always was.
- **First pass** (superseded before commit, kept for the record): zone+meter on row 1's
  right screen, continuous 27-char bar. User's reaction: like the direction, but move the
  zone+value to row 0 right-justified, and reshape the meter to align with the 8 physical
  GP9-16 buttons so you can jump between zones directly — not just watch the number.
- **PROC_FACE_TENSION_ZONES** (a face on the PRIMARY plane, the same mechanism the
  ChordMask/Groove/LFO fold just built out) — the rack's first face to carry a real GESTURE
  via GP-row button press, not just a paint/toggle surface. Row 0 right-justified: zone +
  signed value. Row 1 (GP9-15, 5 cols each): one cell per zone, current one bracketed
  `[DRN]`; GP16 = a steady "Rslv" hint. Tap GP9-15 = instant jump to that zone's rough
  midpoint (DRONE≈-56/CHORD≈-36/SCALE≈-12/DETENT=0/LEAN≈12/RUB≈36/SLIP≈56), cancelling an
  in-flight RESOLVE first — a manual turn, fast-travelled. Tap GP16 = the original page's
  own smooth bar-quantized RESOLVE-to-detent ramp, unchanged, reused verbatim
  (`SEQ_CORE_TensionResolve`). The Grav encoder itself never moved — fine control between
  zones is exactly as before; the buttons are an ADDITIONAL fast-travel layer, not a
  replacement (surfaced explicitly mid-build: user asked to confirm the encoder still
  reaches the in-between values before continuing).
- **Left screen dead space stays USED**: Tension only fills 2 of 8 dial cells (cols 0-9);
  the first-pass 16-track GRIP overview bar (col 12+, `.`/`o`/`O`/`#` per track, same
  thresholds as the original page's) survived the row 1 redesign — it moved out of the way
  of the new zone cells rather than being cut.
- **Verdict → GO, both by-ear (RESOLVE/jump gestures) and by-eye** ("looks great" — this is
  the first rack change this session where the visual layout itself, not just behavior
  parity, was the thing being judged). +320B flash for the new gesture (the display-only
  first pass added none net, a pure port). No new open thread named.

**2026-07-08 — Self-bus legibility: first cut, three targets decoded (SHIPPED, by-eye GO).**
The §10 fork below, picked up the same session. Source-checked before building (CLAUDE.md's
"verify platform-internals claims" rule earned its keep): the §10 note's example target list
(ClockDiv → `/16`, Probability, Length-gate) didn't survive contact with `ctrl_labels[]` —
Probability and gate-Length aren't self-bus-reachable CC targets at all, and ClockDiv turned
out to have no fraction table on its own dedicated page either (`seq_ui_trkdiv.c` just prints
`SEQ_CC_Get(...)+1`, a plain number) — so decoding it would have been a near-nonexistent
legibility win for real code cost. Re-scoped against the real catalog with the user
(`AskUserQuestion`): **bipolar signed values** (Trn.Semi/Trn.Oct/LFO Amplitude) + **Direction**.
- **Mechanism.** New `SEQ_LCD_CtrlTargetCC` (`seq_lcd.c`) resolves a Ctrl-type par layer's
  routed CC exactly like `SEQ_PAR_AssignedTypeStr` already does (`SEQ_CC_Get(track,
  SEQ_CC_LAY_CONST_B1+par_layer)` normal tracks, `seq_layer_drum_cc[instrument][par_layer]`
  drum/MBSEQV4P) — one source of truth for "what does this layer point at," reused rather
  than re-derived. `mapped_cc = cc_number+0x20` mirrors `SEQ_CC_MIDI_Set`'s own mapping.
  `SEQ_LCD_PrintCtrlValue` switches on `mapped_cc`: Trn.Semi/Oct get the **unmasked** nibble
  decode (`>=8 → -16`) that `seq_core.c` actually runs at emission — deliberately NOT the
  rack's defensive `&0x0f` read (`SEQ_UI_PROC_ParamRead`), because a self-bus step can
  genuinely drive the target outside the sane -8..+7 range and the preview must not lie about
  what emission will do with it; LFO Amp gets the `×2, -128` bipolar decode
  (`SEQ_CC_MIDI_Set`'s 7→8-bit scale + `seq_lfo.c`'s centre); Direction gets its 7-name enum.
  Everything else keeps the original raw `%3d` read, unchanged.
- **Reuse over duplication, twice.** New `SEQ_LCD_PrintSigned` (`seq_lcd.c`) reuses the
  already-fixed `%+3d`→literal-`"3d"` vsprintf-flag bug's fix idiom (`SEQ_UI_PROC_PrintSigned`,
  seq_ui.c, from the G1/Pitch work) rather than re-discovering it — hand-emit the sign char.
  Left `seq_ui.c`'s copy alone rather than force a single shared function: the rack's version
  deliberately skips width padding because its cell is pre-blanked every redraw, while the
  par-layer step cell isn't, so the two have genuinely different padding contracts.
  `SEQ_LCD_PrintCtrlValue` returns handled/not-handled so each of the two display sites
  (`SEQ_LCD_PrintLayerValue` compact view, `SEQ_LCD_PrintLayerEvent` detail view) falls back to
  its OWN original raw rendering byte-for-byte when a target isn't decoded — the detail view's
  VU bar survives for the ~40 still-undecoded targets, not just the 3 new ones.
- **Verdict → GO by-eye on hardware** ("looks great"). +16B flash. HIL 241/241 (one
  unrelated pre-existing flake surfaced and fixed along the way — a hardware-timing test
  missing a per-test `pytest.mark.timeout` override, not a firmware bug; see
  `reference-build-and-flash` memory). No new thread queued — the remaining ~40 targets get
  decoded opportunistically, same POC-scoped low-blast-radius pattern, when the ear/eye asks
  for a specific one next.

**2026-07-08 — Humanize joins the rack: 5th emission tenant, 11th row (SHIPPED, by-ear GO
— a bypass bug found + fixed after the first flash, confirmed clean on the second).**
Picked from a user-supplied 16-row roadmap sheet as the one clean, well-precedented gap:
`seq_humanize.c`/`seq_ui_fx_humanize.c` already existed (Value 0..127 intensity +
Note/Vel/Length mode bits, its own stock FX page) but had never joined the grammar — same
EXPOSE-IN-PLACE shape Echo/Groove/LFO went through in G1.5-G1.7. The sheet's other ~7 names
(Step Gen, Rand Gen, Turing, Duplicate, Loop, Morph, Capture) don't map to existing
infrastructure the same way and were deliberately left unbuilt pending their own scoping
pass — see §10.
- **`proc_row_t` grew a `disable_cc` field — the first genuinely new generalization since
  G1.5's `{occ_cc, disable_mask}`.** Every prior emission tenant's headline CC has spare
  bits (Echo's Rpt 0..15, Groove's Styl ≤~28, LFO's Wave 0..25), so the bypass bit packs
  into the same byte as the count. Humanize's headline (`Int` = `HUMANIZE_VALUE`) is a
  genuine 0..127 intensity dial with no spare bit, so bypass needed to live on the
  sibling `HUMANIZE_MODE` cc instead. `disable_cc` (0 = same byte as `occ_cc`, the
  unchanged default for every existing tenant) tells `RowState` and the B-row
  double-tap which cc to read/flip the mask on. `RowState`'s count-masking
  (`raw & 0x3f`) is now conditional on the disable bit actually sharing `occ_cc`'s
  byte, so Humanize's `Int` reads its true 0..127 value instead of being truncated to
  63 — a real correctness fix the old code would have silently hit the moment any
  tenant needed a full-range headline, not just a Humanize-specific patch.
- **Shipped-then-caught: bypass didn't work at all on hardware (user report), root
  cause was TWO bugs, both in the first pass's choice of bit 7 for the Mode disable
  flag.** (1) `humanize_mode` is a **4-bit struct bitfield** (`u8 humanize_mode:4`,
  `seq_cc.h`, packed tight with `transpose_semi`/`transpose_oct`/`morph_mode`), not a
  full byte — only bits 0..3 physically exist, so writing bit 7 silently truncated
  away on every `SEQ_CC_Set`, never actually stored. (2) independent of storage,
  `SEQ_HUMANIZE_Event` (`seq_humanize.c`) never checked any disable bit at all — it
  only ever tested bits 0/1/2 for Note/Vel/Length, unlike Groove/LFO's `_Event`/
  `FastCC` functions, which DO early-out on their bit-7 disable flags. Both were
  visible in source (the field declaration came up in an earlier grep) but the `:4`
  width wasn't registered as significant when picking which bit to use — exactly the
  class of platform-internals claim CLAUDE.md's "verify against source" rule exists
  for, missed once despite reading the right line. **Fix**: moved the flag to bit 3
  (`0x08` — the only bit actually free in a 4-bit field; bits 0..2 are Note/Vel/Len)
  and added the missing early-out to `SEQ_HUMANIZE_Event`. Same flash size after the
  fix (502520B either way — coincidence, not a wash).
- **Note/Vel/Len bypass `SEQ_UI_PROC_SeedRowDefaults` on purpose.** That registry's
  per-field "untouched" test is `SEQ_CC_Get(track, p->cc) == 0`, re-read fresh for each
  param — correct when every param owns its own CC byte (true of every tenant so far),
  but Humanize's three mode dials share ONE byte: seeding the first bit would make the
  byte non-zero and the second/third field's "untouched" check would then read the
  now-dirty byte as "already touched" and skip. Caught before shipping, not after.
  Fix: the headline's own `PROC_KIND_HUM_VALUE` seeds all three bits in one direct
  write (`f | 0x07`) on its own 0→on transition, guarded once on `(f & 0x07) == 0`,
  bypassing the shared-registry path entirely rather than stretching it to cover a
  case it wasn't built for.
- **`.status` readout** names which of Note/Vel/Len the intensity actually touches
  (`Note:on  Vel:off Len:off`) — unlike Echo/Groove/LFO, where the headline dial alone
  tells the whole story, Humanize's Int says nothing about *what* it's humanizing.
- **+392B flash, +0 RAM.** Full HIL suite (241 tests) reran clean twice — once after the
  `RowState`/double-tap generalization (shared code every tenant depends on), once
  again after the bypass-bit fix.
- **Verdict → GO, by-ear on the re-flash.** Int audible at once on the 0→on turn;
  double-tap bypass now actually silences the effect and preserves Note/Vel/Len/Int on
  re-enable. No new open thread from this tenant itself — the sheet's other ~7 names
  stay unscoped (§10).

**2026-07-09 — Arp joins the rack: a deterministic arpeggiator born as a render-stack
processor, NOT a port of the legacy playmode (SHIPPED, by-ear GO). 5th stack SLOT.**
The box's built-in "Arpeggiator" is a track *playmode*: live-keyboard-driven, stateful
(`t->arp_pos` cycles per emission against a bus notestack), emission-time — one of the
three `legacy_pitch` fences precisely because it's invisible to `OutputActive` and
can't be captured/bounced (§9 2026-06-10). It is left untouched. Instead this is a
**new** transform that keeps the arpeggiator's musical *idea* (walk the chord tones)
but re-expresses it the born-as-processor way (§3): a step-indexed deterministic
SELECT sitting beside ChordMask in the render stack.
- **Sibling to ChordMask, not a variant of the playmode.** ChordMask *snaps* a note
  toward any tone in a 12-bit PC-set; Arp *selects* exactly one tone per step
  (`chord_tones[f(mode,step) % N]`) and snaps to it — same `chord_mask_snap` primitive,
  same `chordmask_mask_h/l` self-mask source (paint the chord once on ChordMask's row;
  no new storage, no new paint face), opposite operation. This is why it's a **stack
  SLOT** (PITCH→CHORDMASK→**ARP**→TENSION→LIMIT), not an emission tenant like the
  Echo/Groove/LFO/Robotize/Humanize rows — it rewrites the buffer before emission, so
  capture/bounce are faithful *for free*, which is exactly the thing the legacy playmode
  can't do.
- **Determinism dissolves the "hard" in "self-arp (deferred, hard)" (§10 c).** That
  entry assumed a per-track mini-notestack + reset hooks to reproduce the runtime
  cursor. The deterministic-by-construction principle (hash/step-index, not live state —
  the same move PitchGen/TrigGen made) removes the cursor entirely: Up = `step%N`,
  Down/UpDown are arithmetic, Random draws `grip_hash` (reproducible, not RNG). No
  notestack, no reset lifecycle — the "hard" was an artifact of porting rather than
  rebuilding.
- **Scope, minimal by design.** A `Mode` dial (0=Off/pass-through, Up/Down/UpDown/Random)
  riding the slot's generic sweep byte; a `Bus` dial (chord source) added the next day
  (2026-07-10 GO — self-source shipped first, bus followed once proven). Two persisted CCs
  (`ARP_MODE 0x9d`, `ARP_BUS 0x9e`, inside the already-shipped `0x80..0x9f` block — old
  patterns load unchanged; `0x9f` still free). +96 B RAM (5th slot × 16 tracks) + ~208 B
  flash, builds/links clean.
- **Bus-source (SHIPPED 2026-07-10, by-ear GO).** `Bus` = `Self` (0) reads the shared
  static mask; `A`..`D` (1..4) read the live held chord off a bus via
  `SEQ_MIDI_IN_BusPCSetGet` — same source ChordMask uses, so a loopback/external chord
  drives the arp. In bus mode Arp joins the `render_live_sig` force-dirty set (a held-chord
  change re-renders that tick); Self mode stays event-only. **Encoding call: Arp gets its
  OWN `PROC_KIND_ARP_BUS` with Self=0** (not ChordMask's Self-at-4 bit-2 scheme) — so the
  zero-default IS self-source (no engage-seed, no bus-A/untouched ambiguity), at the cost of
  a `Self A B C D` dial order vs ChordMask's `A B C D Sf`. Deliberate: default-cleanliness
  over dial-order symmetry.
- **Deferred:** multi-octave spread (single-octave PC cycle today) — the only remaining
  Arp thread. See the REFERENCE doc for build facts + `SEQ_CORE_ArpSlotSync`/
  `arp_render_range` anchors.
- **Verdict → GO, by ear (both cuts).** Arpeggiates painted-chord (self) and live-held
  (bus) sources across steps; Mode→Off is exact pass-through; Self↔bus switch is immediate.
  The standing "self-arp — deferred (hard)" line in §10 c is fully retired.

**2026-07-10 — Capture deposit: silent-park + the Fit×Phase length model (SHIPPED, by-ear
GO).** Two bugs + a by-ear length redesign, all surfaced by capturing a live-generating
track to *another pattern on the same track*.
- **Silent-park (`seq_ui_capture.c` `Commit`).** After a grab the page auto-loaded the
  just-written slot back into the LIVE dst track "so it plays immediately" — *regardless of
  which pattern slot it landed in*. When the dst pattern ≠ the group's currently-loaded slot
  (park a freeze into A2 while playing A1) this (a) replaced the live generation with the
  frozen line on the spot AND (b) left the group dirty+drifted while `seq_pattern[]` still
  pointed at A1, so the next switch's FEARLESS writeback stamped the freeze back over A1 on
  disk ("capture to A2 rendered A1 too"). Fix: the auto-audition fires ONLY when the deposit
  landed on the dst group's currently-loaded slot (freeze-in-place / looper stamp onto a
  live-playing track); any other slot silent-parks — honoring the page's own "never switches
  what's playing" contract.
- **Length vs allocation, untangled (`seq_core.c` `CaptureSpanToSlotTrack`).** The CANVAS
  model (2026-06-28) sized the deposit from `slottrk_trg_steps` = the track's *allocated*
  `num_steps` (128), not its musical LENGTH loop (14/16) — and `SEQ_CC_LENGTH` only sets
  `tcc->length`, never repartitions, so the two are fully independent (the UI shows both as
  `LENGTH/num_steps`, e.g. `14/128`). FILL therefore ballooned a 16-step loop to 128. Two
  wrong intermediate cuts taught the shape: taking `LENGTH+1` but folding it into the
  x8-aligned `canvas` truncated non-x8 loops (12→8, the "8/8"); making FILL = the source's
  single-loop length truncated a *k-loop* grab (2 bars of a 14-step track = 28 → 14). The
  buffer (num_steps, x8, for TrackInit) and the loop length (LENGTH, arbitrary, tiled) are
  now separate quantities.
- **The Fit×Phase deposit-length model (by-ear redesign).** The window is always tiled
  end-to-end; Fit×Phase pick where it wraps. **LOOP** = exactly the grab `W` (short/odd,
  drifts). **FILL+GRID** = the track's max available steps (`num_steps`, the "/128" the UI
  shows) — partial last repeat, so the loop RESETS at the wrap (deliberate glitch).
  **FILL+HEARD** = the largest whole ×`W` that fits (`floor(canvas/W)*W`) — complete repeats
  only, SEAMLESS. Phase does double duty (it also sets the capture window in `CaptureSpan`:
  GRID loop-aligned / HEARD playhead-ending) — same GRID=locked / HEARD=as-heard spirit.
  Self-verifying by eye off the `LENGTH/num_steps` readout: 28/128 (LOOP), 112/128 (HEARD),
  128/128 (GRID).
- **Grab depth reminder (not a bug).** A non-whole-measure track (14 ≠ the 16-step global
  bar) grabs via the Approach-A tape at depth = complete loops played since transport start;
  the "Grab: Nb" readout + thermometer self-clamp to it. "2 bars gave 14" can also mean the
  dial capped at 1b — let it play a couple loops first.
- **Verdict → GO, by ear.** LOOP drifts, HEARD is seamless, GRID resets on the beat; parking
  to another pattern leaves the live jam (and its disk slot) untouched.

**2026-07-10 — Reconciliation: the recall-freeze cure (§8 queue #1 / Wall 1) was already SHIPPED;
the roadmap's "still gating" citations were stale.** Picked up cold to "build #1, the highest-
leverage open item"; a source check found it had shipped 2026-06-22 (by-ear GO) and been carried
as open ever since — the §8 queue, the MVP list, and the Arp/G3/Humanize §9 entries all repeated
"#1 is still the one gating item" without reconciling against the code. Exactly the drift CLAUDE.md
warns about (verify platform claims against source, not memory).
- **What is actually built** (verified in `seq_pattern.c`): the DRIFT-gated writeback, at BOTH
  live paths — phrase recall (`SEQ_PATTERN_SnapshotRead` → `WritebackAllDrifted`, gated on
  `phrase_drift`) and the running pattern switch (`SEQ_PATTERN_Handler` → `WritebackIfDrifted`,
  the "Tier-1 (b)" fix). Only the transport-*stopped* immediate path (`SEQ_PATTERN_Change`) stays
  ungated — correctly, there is no live clock to freeze. The by-ear decision *(abandon un-captured
  wander on recall?)* was answered **yes** and the `no → incremental-save` fallback was never
  needed.
- **The one real gap, now closed: a regression pin for the cure's own invariant.** Existing tests
  covered drift-leak hygiene (`test_capture_drift_leak`) and the deliberate-edit writeback on
  *switch* (`test_fearless_switching`), but nothing pinned *wander-only recall → zero writeback* —
  a refactor reverting `WritebackAllDrifted` → `WritebackAllDirty` would have passed the whole
  suite. New `test_recall_freeze_cure.py` states the invariant as **writebacks on recall ==
  popcount(drift mask)** and instantiates both ends: a generator wandered live (dirty, drift clear)
  recalls with 0 writebacks and restores the pristine captured loop (wander abandoned); a deliberate
  CC edit (drift set) writes back exactly once and the live organism returns to the phrase (nothing
  lost). The running-switch drift-gate is left unpinned (measuring it needs the transport running at
  the switch instant — timing-sensitive; the recall pin + the fearless deliberate-edit switch pin
  bracket it).

**2026-07-10 — Robotize → render-stack: DEFERRED BY CHOICE after a recon-grounded talk-through
(not a build; §10 bullet updated).** Picked "the big one" (bounce faithfulness) as the next item,
did the source recon (`SEQ_ROBOTIZE_Event`, the anchor/seed loop machinery, the render-slot
dispatch), then the user reframed: *now that the system has taken shape, should we move it — it can
create its own loops?* Talked it through and decided **not to migrate now**. The reasoning:
- **The faithfulness gap that motivated it (2026-06-12) is already largely covered.** CAPTURE-the-
  tape grabs the post-robotize *heard* stream (robotize applies at emission; the tape records the
  emitted stream), and the self-bus freeze finding (2026-06-26) already names the **tape as the
  faithful freeze for randomness** — deterministic bounce is the wrong tool for random content. So
  "bounce freezes the pre-robotize line" is no longer the live pain it was when the north-star was
  written; harvesting a robotized line is a solved path.
- **Robotize is not a stateless render slot — it is a generative LOOP engine.** Palette of 16
  per-bar anchor SEEDS, loop machinery (`loop_cycles`/`loop_start`/`loop_rotate`/`palette_length`),
  and sculpting verbs Reseed/RerollBar/**Freeze** (its Freeze is *already* a capture-the-last-K-heard-
  bars-as-a-loop). The five render slots are stateless per-step transforms; a robotize slot would be
  a bespoke *stateful* slot carrying per-bar phase/anchor state + honoring PRNG-consumption order,
  not a sibling-to-Pitch add. This also brushes the parked register-unification fork (robotize's
  anchor is a *seed*, live-reinterpretable — a different "frozen" bet than the value-storing slots).
- **Feasibility (verified, for if it is ever revived):** only the **note / oct / vel / len / skip**
  half can render into the mirror — emission reads note/vel/len via `SEQ_PAR_Get` → OutputActive
  (`seq_layer.c:394/404/433`), so writing them there would take. The **SUSTAIN / ECHO / NOFX /
  DUPLICATE** flags add extra events (echo/duplicate), hold runtime note-length state (sustain), or
  gate other emission tenants (nofx) — none fit a per-step mirror; they would stay a thin
  emission-time residual. So a migration = *lift the pitch/dynamics/length wander into a stateful
  generative-loop slot; leave the flags behind*, not a clean whole-unit move.
- **The one thing that would revive it:** wanting robotize as a permanent **live** layer you keep
  sculpting (reseed/reroll/freeze) and bounce **as-heard repeatedly** — the "live AND bounce-
  faithful" property only the render stack gives a stateful loop, distinct from the tape's one-shot
  concrete grab. The user's call for now: *good with it where it is.* Recon is preserved in this
  entry so a revival starts from the split above, not from scratch.

**2026-07-10 — §10 CAPTURE open-question reconciled: the "locked to one measure" framing was stale
(same class as the recall-freeze mis-citation).** Picked the §10 "CAPTURE is locked to one-global-
measure tracks (self-bus ↔ CAPTURE tension)" bullet as the next build, then the source read
contradicted it: `SEQ_CORE_CaptureSpanTape` (while PLAYING) already grabs **any** length note-for-
note (`n_meas >= 1`, `seq_core.c:2906`; SHIPPED 2026-06-26, foreign-clkdiv-hardened 2026-06-28) — the
"grab a long self-modulating loop" case the bullet named is served on the live path. The §10 text
(*"both freeze paths refuse a source whose length ≠ global measure"*) predated the A1 win and was
never reconciled; the REFERENCE doc (§3, lines ~165–180) was **accurate** throughout — the CLAUDE.md
ownership split (REFERENCE owns codebase facts) held. The one genuine remaining gap is the **STOPPED**
re-sim path (`SEQ_CORE_CaptureSpanReSim`, `n_meas != 1 → -8 "play to grab"`), which is **DEFERRED BY
CHOICE**, not open: the A2 kernel was attempted 2026-06-28 and reverted on two walls (the per-global-
measure ring needs re-framing around the track's own loop wrap for non-aligned multi-bar loops; plus
an undiagnosed post-real-play multi-bar drive hang) — the *build-less-listen-sooner* call was to keep
multi-bar grabs while-PLAYING only. **No build, no test pin needed** (behavior already shipped + HIL
241); the fix was doc-only: §10 bullet rewritten from "genuinely open" to "deferred by choice," recon
preserved in the plan for a possible revival. *Revive only if grabbing a long self-mod loop while the
transport is STOPPED becomes a workflow the user wants.*

**2026-07-10 — Atomic CHECKPOINT anchor write (§8 queue #7, the last queue item; SHIPPED, reliability
hardening, not musical).** The one unprotected power-loss exposure in the snapshot layer: every
CHECKPOINT overwrote the anchor's four group records **in place** (`MBSEQ_AN.V4`), and REVERT reads
all four back **wholesale with no completeness witness** (unlike the phrase probe's dual group-0 +
last-group check) — so a mid-write failure left a half-new/half-old anchor that REVERT would restore
as a corrupt Frankenstein organism, losing the performer's blessed safety net. The doc had carried
"atomic temp+rename is the fix *if it bites*" since FEARLESS Stage C (2026-06-11); built now as the
queue's tail.
- **The fix:** `SEQ_PATTERN_SnapshotWrite(ANCHOR_BANK, 0)` now delegates to a new
  `SEQ_FILE_B_AnchorWriteAtomic` — build the whole anchor in a scratch (`MBSEQ_AN.TMP`, sharing the
  anchor info slot; base "MBSEQ_AN" + ".TMP" fits the 8.3 `_USE_LFN=0` limit), then `FILE_Rename` it
  over `MBSEQ_AN.V4` in one directory op. Failure drops the scratch and leaves the old anchor intact;
  a power loss in the tiny unlink→rename window leaves NO anchor, which REVERT safely refuses — never
  a mix. New thin `FILE_Rename` primitive in the shared FILE module (`f_unlink(dst)` because FatFs
  `f_rename` refuses an existing dst, then `f_rename`; POSIX `rename()` under emulation) — **this is
  the temp+rename the SET durable baseline (§10(a)) will reuse**, now proven.
- **Scoped deliberately to the checkpoint (anchor block 0), by ear/by source, not blanket:** (1) the
  **pre-revert stash** (`SnapshotWrite(ANCHOR_BANK, PREREVERT_BASE=4)`) must stay **in-place** — it
  writes slots 4..7 into the SAME file and has to preserve the checkpoint in slots 0..3, whereas the
  atomic replace intentionally rewrites the file as a fresh 4-slot anchor (correct for a checkpoint —
  it drops any stale stash, and CHECKPOINT invalidates the journal that referenced it — but it would
  corrupt a stash). Branch is `bank == ANCHOR_BANK && base_pattern == 0`. (2) The **phrase** path
  stays in-place: its file holds all 16 phrases (~300 KB — a whole-file swap per capture would be
  prohibitive) and its dead-capture case is already tolerated by the occupancy witness (reads as
  absent, not corrupt). The residual **phrase-overwrite** Frankenstein (new group 0 + old tail both
  pass the witness) stays an accepted POC cost — a per-slot barrier is the fix if it bites.
- **The SET durable baseline itself was NOT built** — it stays design-settled / build-on-GO (§10(a),
  §9 2026-06-19). #7 bundled "atomic snapshot writes + the SET baseline"; only the reliability half is
  reliability hardening. The SET baseline is a separate feature (a file-copy layer above the
  disposable session) gated behind its own GO.
- **Pin:** `test_recheckpoint_replaces_anchor` (checkpoint A → re-checkpoint B over the live anchor →
  REVERT lands on B, never stale A, never a dropped file — exercises the remove-old + rename overwrite
  path). The single-checkpoint round-trip and the checkpoint↔stash interaction are already covered by
  the existing FEARLESS pins (`test_checkpoint_revert_restores_sculpted_organism`,
  `test_revert_is_undoable`, `test_double_revert_preserves_jam`), which now run over the atomic path.
  Firmware builds clean; HIL pending on the user's hardware.

**2026-07-10 — Waypoint direction modes: a painted path the playhead traverses (SHIPPED, by-ear GO; POC, side-exploration off the capture spine).**
Origin: the user tried self-modulating a track's Direction via a Ctrl→Direction layer and it "traps itself on two steps." Root cause is structural, not tunable — Direction is a *per-step scalar that decides the very next move* (`SEQ_CC_MIDI_Set` → `tcc->dir_mode` → the ±1 in `SEQ_CORE_NextStep`), so any adjacent Fwd/Bwd pair is a 2-cycle attractor. The cure: stop steering a scalar and instead traverse a sparse set of **pins**, reflecting only at the path *extremes* — interior pins are passed through, so the trap can't form.
- **Surface — a dedicated `SEQ_PAR_Type_Waypoint` (=20) parameter layer.** Per-step value = **visit order** (0 = not on the path); emits no MIDI (no case in the emission switches; `default:-1` in the send-value switch). Falls back to the track's **gated steps** (all order 1 → step order) if no Waypoint layer is assigned, so the idea works before you set one up. Registered in the four `SEQ_PAR_NUM_TYPES` arrays (name/map/default/**max-value** — the max-value entry is what makes it paintable; a zero there silently un-paints the layer) and rendered on **both** `SEQ_LCD` par-value paths (else an unhandled type prints "????"). Painted as a dotted line with order numbers.
- **Three modes on the Dir page (`dir_mode` 7/8/9, datawheel-only — the GP grid is full).** `WpHop` (7): land only on pins, **ping-pong** through them in order key `(value, then step)` — non-monotonic routes allowed (the path can jump around the step line). `WpFill` (8): **ordered segment-scan** — walk the raw steps toward the next pin *in path order*, so every step sounds and the scan direction reverses at interior pins; needs `t->waypoint_target` because non-monotonic paths self-cross (position alone can't say which segment you're in) — self-heals if stale. `WpHopSaw` (9): **rotary/sawtooth** — forward through the order, hard-reset to the first pin at the top; a **repeated order number folds** the path back at that pin (holds out higher-numbered pins — a movable fold). Sawtooth is Hop-only *by design*: in Fill every step sounds, so a "reset" is just a backward sweep = ping-pong.
- **Code anchors:** `SEQ_CORE_WaypointNextStep` + `SEQ_CORE_WpNextPin` (shared next-pin search with a `saw` flag and a `wrapped` out-param for bar-counting) + `SEQ_CORE_WpOrderGet`/`WpKeyLt` in `seq_core.c`; `NextStep` bypasses the jump-back/skip/replay progression for waypoint modes (they define their own traversal, so it's only ever called reverse==0).
- **Known edges (POC costs, documented not fixed):** run-start plays step 0 once before the path engages (FIRST_CLK doesn't call the stepper); **CAPTURE re-sim is not frame-faithful for a mid-segment Fill** (`waypoint_target` isn't in the capture frame — the *live tape* of what sounded is unaffected; add the field to the frame if it bites); Skip triggers fight the traversal; the layer consumes a par-layer slot; sawtooth ties hold out the pins above the repeat.
- **Status:** by-ear GO 2026-07-10 (Hop, then ordered Fill, then Saw), user "this is cool and what i had in mind." Non-waypoint modes (0–6) untouched; additive. **HIL pin DEFERRED** — the harness has no verb to observe `t->step` or to paint a Waypoint layer, so a rigorous traversal pin needs new firmware/harness plumbing plus a hardware run; not written blind. Likely future extensions (user: "maybe we'll add more"): a Fill-saw that actually retriggers/silences on reset, per-pin dwell, painting the path *order* directly as a gesture.

---

## Migrated from §10 (closed / decided-not-built — final text, verbatim)

*Moved here 2026-07-11 when design-doc §10 was pruned to genuinely-open forks.
Each block is the entry's final state at migration; §10 keeps one-line pointers.
Some blocks are the SHIPPED headers of entries whose still-open bullets remain in §10.*

**Closed/shipped — folded to §9 + REFERENCE (no longer open):** base-SRAM + MSP high-water
(measured, §A5); the phase A–F step-5 sub-decisions (CCM placement, render scheduling, knob
detection, cache invalidation, TRKMODE migration, generator pool, BOUNCE destination, §9);
phase-F.3 cross-track capture + the withdrawn ENGAGE auto-jump; the `0x96`+ ext-CC persistence
bug (CLOSED 2026-06-10 — V3 ext-tag widened to 0x80–0x9F); the sampler-slot / windowing-playback
/ render-cache / set-density forks (now §5 / §A2); self-bus legibility first cut — Trn.Semi/Oct
+ LFO Amp signed decode, Direction enum (SHIPPED 2026-07-08, by-eye GO, §9) — the remaining
~40 targets are opportunistic follow-on, not a standing fork; **§10(b) trigger generators**
(SHIPPED as G3, 2026-07-07, §9 — folded below, kept only as a pointer).

**STOPPED multi-bar CAPTURE — DEFERRED BY CHOICE (reconciled 2026-07-10, §9; NOT a gating open
question).** *Was carried here as "CAPTURE is locked to one-global-measure tracks (the self-bus ↔
CAPTURE tension)" — that framing is stale: it predated the A1 win and cited "both freeze paths
refuse."* The true state (REFERENCE §3 ~165–180 has had it right throughout):
- **While PLAYING → ANY length already works.** `SEQ_CORE_CaptureSpanTape` grabs a sub-measure /
  odd / multi-bar / self-modulating loop **note-for-note** (`n_meas >= 1`, `seq_core.c:2906`; SHIPPED
  2026-06-26, foreign-clkdiv-hardened 2026-06-28, HIL 241). The tension the old bullet named — *grab
  a long self-modulating loop* — is served on the live path.
- **Only WHILE STOPPED is it one-global-measure.** `SEQ_CORE_CaptureSpanReSim` gates
  `SEQ_CORE_CaptureLoopMeasures(src) != 1 → -8 "play to grab"`. The stopped multi-bar kernel (A2) was
  attempted 2026-06-28 and **reverted** on two walls: (1) the ring stores one frame per GLOBAL
  measure, so a non-aligned multi-bar loop has no frame on its loop start → the grab comes out
  **rotated** — the real fix is re-framing the ring around the track's OWN loop wrap (Approach A
  general, ring depth in loops), not a phase tweak; (2) an undiagnosed post-real-play multi-bar drive
  **hang** (>10 s timeout, only with real emission/scheduler history). The call: *build less, listen
  sooner — multi-bar grabs stay while-PLAYING only.*
- **Revive only if** grabbing a long self-mod loop **while STOPPED** (transport parked, freeze
  without hitting play) becomes a workflow the user wants. Reopening = commit to the ring re-frame
  (wall 1) + debug the hang (wall 2) — a real multi-session project. Recon + two-wall postmortem:
  `doc/plans/archive/2026-06-26-multimeasure-capture.md`.

**Tension Workbench — SHIPPED + by-ear GO 2026-06-10 (§9; build narrative → REFERENCE).** The
GRAVITY field (monotone pull / varied push), per-track GRIP, RESOLVE, SHADE; ext-CC fix shipped
as the V3 tag (0x80–0x9F). Soft GO (works, no refinement requested). The by-ear tunables below
stay live dials, revisited only when the ear asks.

**Save-model rethink (2026-06-11) — SHIPPED as FEARLESS + RECOMBINE (§9; narrative → REFERENCE).**
Resolved (built + GO): checkpoint storage (sentinel `MBSEQ_AN.V4`), gen-state V4 tag, the
track-grain pull gesture, the tape's §5.5 supersession, the two-surface model. Genuinely-open
forks that remain:
- **Robotize → render-stack processor — DEFERRED BY CHOICE (2026-07-10, §9; recon complete, not
  just unscheduled).** After the source recon + a design talk-through: the migration does not earn
  its cost now — CAPTURE-the-tape already harvests the post-robotize *heard* line faithfully (the
  tape is the named freeze path for randomness), and robotize is a generative LOOP engine (per-bar
  anchor seeds + loop machinery + a Freeze that already captures the last K heard bars), NOT a
  stateless render slot — so a migration is a bespoke *stateful* slot, and only its
  note/oct/vel/len/skip half could move (the SUSTAIN/ECHO/NOFX/DUPLICATE flags stay an emission
  residual). Revive only if robotize should be a permanent *live* layer bounced as-heard repeatedly.
  Full reasoning + the feasibility split in §9 (2026-07-10). *Original 2026-06-12 motivation:*
  Surfaced by ear during FEARLESS Stage B: bouncing a robotized track freezes the
  *pre*-robotize line — designed behavior per §3:191-193 / the §8-step-5 deferred list
  ("random/generative effects stay reset; re-applying would diverge"), but that rested
  on "you'd never want the heard variation frozen." The user does — same call FORCE_SCALE
  got (bake-as-heard, then migrated to the stack). The hinge that made the design treat
  them differently was determinism; this fork's robotize is *anchorable* (per-bar anchor
  seeds + palette + loop control), so a given bar's variation is reproducible — which is
  exactly what makes "render the current bar's robotized output deterministically into the
  mirror" tractable. Scope before coding: probability gating, per-bar anchor/palette/loop
  state, and the SUSTAIN / ECHO / NOFX flag interactions all have to render deterministically
  per bar. Reopened next to FORCE_SCALE in §9's freeze-faithfulness lineage.

- **(d)** The third beneficiary of the generative law is the existing **robotize →
  render-stack migration** (see the Bounce north-star entry above) — migrating it makes FREEZE
  hold it and BOUNCE freeze it editable, dropping the lone emission-time exception.

- **(a2) Unified UNDO/REDO (Tier 1) — BUILT (Stage 2a 2026-06-23 + Stage 2b 2026-06-24, §9).**
  Shipped as designed: ONE global **action journal** `{state, scope, before, after}` in CCM
  consolidating the three bespoke one-deeps (`track_undo`, generator `undo_slot`, utility buffers)
  behind `SEQ_CORE_JournalArm/Undo/Redo/Invalidate/InfoGet` + the new REDO. Lazy `after`, symmetric
  2-way swap (TRACK scope); ORGANISM scope (Stage 2b) folds REVERT in via a pre-revert SD stash.
  Only deliberate verbs arm (wander can't pollute — the `!seq_generator_in_automutate` invariant is
  structural, not an explicit gate). SELECT+CLEAR toggle. See §9 (2026-06-23 / 2026-06-24) for the
  shipped detail and the adversarial-review fixes; REFERENCE for the codebase facts. *Original
  settled-design sketch (now realized): a shallow ring per deliberate gesture; the implementation
  landed it as one-deep, in CCM (main was the scarce region), reusing the existing snapshot stores.*

- **(b) Trigger generators — SHIPPED as G3, the trigger Turing machine (2026-07-07, §9;
  by-ear GO — "so far so good").** Extended `seq_generator.c` with an independent key-space
  (`pool_index_trg`) rather than a twin pool, reusing PitchGen's mechanics (lock/rate/depth/
  anchor/roll/bounce) to write 0/1 into a trigger layer instead of a note into a par-layer;
  `range_min` doubles as density, no contour analogue for a boolean. Proves pitch-gen and
  trigger-gen running **simultaneously on one melodic track** — pitch and rhythm decoupled,
  the actual point. The register-unification question it surfaced (PitchGen/Robotize's
  step-register vs bar-anchor register) is tracked separately below, parked not gating.

**Retroactive CAPTURE — the frame/ring ("Capture MIDI," generalized). SHIPPED first cuts + by-ear
GO 2026-06-20 (§9 2026-06-20; narrative → REFERENCE + plan files).** The north star:
always-listening, play-then-keep — grab the last K bars off the ring as editable, bounce-able
material. Two kinds of ring content: a regenerable **seed-frame** (re-sim) and a lived-through
**tape** (records what sounded). Shipped: the per-bar generative-frame **ring** (`seq_core_cap_ring`, 17
deep), **re-sim** (stopped — rewind + re-drive with wander, record the emitted stream), the **live
tape** (playing — passive tee off the MIDI-out drain; strictly more faithful), the **UTILITY hold
→ row → GP-n** gesture, the par-aware thermometer, and **precise gate + multi-step length** (tape).
Plans: `doc/plans/archive/2026-06-19-capture-ring.md`, `doc/plans/archive/2026-06-20-unified-freeze.md`.
**PER-TRACK-RNG keystone — FIRST CUT SHIPPED + by-ear GO 2026-06-20 (§9; narrative → REFERENCE).**
The determinism refactor that makes spans seekable + forward-deterministic (the CAPTURE
prerequisite). Policy: stateful streams → caller-owned xorshift seed; standalone per-step render
gates → stateless `grip_hash`. Done: generators (per-pool-slot seed, minted at ENGAGE), chord_mask
(shared `grip_hash`, zone 0x20), random traversal (`random_traverse_state`, SCRUB-safe). +256 B
CCM. `CMD_RNG_SEED` 0x4d.
**Phrase morphing (Loop A + B + note Phase 1) — SHIPPED + by-ear GO 2026-06-16/18 (§9; narrative
→ REFERENCE).** Per-group posture interpolation live→a target phrase, datawheel/GP-bar, SELECT+tap
to arm; pos 0 = reversible pass-through. Loop A = ext-CC posture block (robotize / chord-mask /
GRIP); Loop B = main-CC whitelist (groove/transpose) + per-step velocity lerp + reversible gate
crossfade; note Phase 1 = discrete pitch swap sharing the gate threshold. `CMD_PHRASE_MORPH` 0x4f;
~6.6 KB .bss. Makes "a set is a path" continuous — the morph is the transition, the bar-aligned
recall the arrival. **Feel retuned by ear 2026-06-22 (§9): snap CCs (octave/groove-style) flip at the
MIDPOINT not full throw; notes + gates do a TENT ("flip then unflip", ~50% scramble at the center,
re-cohere to A at both ends) — so full throw = B's posture/dynamics/transpose over A's notes+rhythm.**
- **Keep/cut — RESOLVED: morph KEPT (by-ear GO 2026-06-22, "best they've been").** Once it was felt
  working, the cut reversed — the live posture-morph stays default-ON. The `SEQ_PHRASE_MORPH` compile
  flag (`make PHRASE_MORPH=0`, mirrors TESTCTRL) survives **only as a RAM lever**: build-verified it
  reclaims a **measured 6680 B (~6.5 KB) main RAM** (free 9.06 → 15.59 KB; the "~7.7 KB" estimate is
  corrected — the buffers are main-RAM, not CCM), to pull if something on main RAM's critical path
  ever needs it. Recorded-state morph (§5) remains the *cheaper alternative* model but is no longer
  "the" canonical one — both coexist; the live morph is what's used. (Windowing for recorded-state
  morph still isn't built — that gesture is a separate follow-on if ever wanted.)

**SWITCH-QUANTIZE — global launch grid + auto-measured margin (SHIPPED 2026-06-18, by-ear
pending; §9 → REFERENCE).** Switches land on a selectable musical grid (Instant…8 bars), datawheel
on the PHRASE view; the forward-delay self-sizes to measured SD cost. `CMD_SWITCH_QUANTIZE` 0x4e.
**Phrase-recall freeze — the ~290 ms SD save is the wall (investigated + PARKED 2026-06-19).**
A live phrase recall during a generators-running jam froze the clock for up to ~1.3 s.
Instrumented on hardware per-phase (the 1 µs stopwatch saturates at 65.5 ms and STM32F4
`MIOS32_SYS_TimeGet` has no sub-second resolution — measure per-unit & accumulate; see
REFERENCE). The recall *body* is cheap — open ~5 ms + 4-group read ~66 ms + 16-track render
~1 ms ≈ **72 ms**, which fits the forward-delay margin. The cost is the **writeback**: one
pattern SAVE ≈ **290 ms** (vs ~22 ms load), flash-PROGRAM-bound (NOT sync, NOT CPU, NOT the
zero-fill; ~12–16 ms × ~18 sectors). Recall wrote back *every dirty group*, and **generator
auto-mutate marks a group dirty** (`seq_generator.c` → `SEQ_PATTERN_DirtySetTrack`), so a
4-group jam paid 4×290 ms.
- *Margin-sizing can't fix it* (needs ~1.3 s; `SEQ_CORE_SwitchMarginMs` caps at 250 ms).
- *Full-fidelity deferral can't fit* — snapshotting the outgoing organism is ~30 KB vs ~9 KB free main SRAM (§A5 corrected). Dead end
  as a RAM snapshot.
- *Tried + reverted (ephemeral-wander):* gate the recall writeback on `DRIFT` (deliberate
  edits) instead of `seq_pattern_dirty`, so generator wander is abandoned on recall (snap to
  the committed phrase). Worked on the bench (wander recall → 0 writebacks → ~72 ms), but the
  live feel raised a "things revert" worry and prompted a step-back on whole-model complexity,
  so it was reverted to baseline. The idea is sound and re-buildable; it's gated on the §5.6
  clarity pass + a by-ear call on whether un-captured wander should survive a recall.
  - **CURED + SHIPPED + by-ear GO 2026-06-22 (§9):** the capture-centric model *answered* the by-ear
    question — recall = select a static grab, the living-return is a performed move between grabs, so
    un-captured wander is not precious at recall (you'd have captured it); DRIFT-gated writeback is
    the **faithful** recall behavior, not a compromise (the "things revert" worry was an artifact of
    the retired two-face model). Built as `SEQ_PATTERN_WritebackAllDrifted` (gated on `phrase_drift`),
    called by `SnapshotRead`'s pre-writeback. Played on hardware: freeze gone, feels right ("i like
    it, no freeze"). Incremental-save is no longer needed for this (it stays a fallback only if a
    future by-ear call reverses the wander-survives question).
- *The only structural cure* if the freeze must die without losing wander: **incremental save**
  (program only the sectors that changed) — drops a save from ~18 sectors to a few. Its own
  bundle; touches the SD/file write path.


**2026-07-11 — F1/F2 closed: Arp bounce-neutralize + the AllSlotSync raw-write rule (FIXED;
HIL 250/250 — 244 baseline + 6 new pins, green on first run after flash)**
- **F1 (P1 musical): the ARP tenant joins the GENERATION axis.** `arp_mode`/`arp_bus` are now
  zeroed in `SEQ_CC_ResetGenerativeForBounce` (every capture verb) and in
  `SEQ_CORE_ProcessorBounce`'s doubly-bound-tcc untangle — the capture bakes the arp's
  re-ordering into the notes, so a kept `arp_mode` re-arps the frozen tape when
  `ArpSlotSync` next fires.
- **F2: `SEQ_CORE_AllSlotSync(track)`** — one helper = all five tenant syncs
  (ChordMask/Tension/Pitch/Limit/Arp). The three slot-capture restore fans
  (`CaptureToSlotTrack` / `CaptureSpanToSlotTrack` / `CopyTrackLiveToSlot`) and the
  preset-import fan (`seq_file_t.c`) use it, so an arp-armed bystander track in the borrowed
  dst group comes back audible, not silently un-arped.
- **New rule surfaced by the trace: stale slots are NOT benign for slot-strength tenants.**
  ARP/CHORD_MASK/TENSION render from `slot->strength` (the mode/dial copied at arm time);
  PITCH/LIMIT re-read tcc and no-op when stale. So **every RAW tcc write on a LIVE track
  (memcpy restore / direct-field reset) needs `SEQ_CORE_AllSlotSync` after it** — the
  SEQ_CC_Set chokepoint fires syncs itself, raw writes bypass it. Applied to the to-track
  paths (`CaptureToTrack`, `CaptureSpanPrepDst`), whose CC inherit armed dst's slots from
  PRE-reset values: capture onto a track that previously ran an arp re-arped the frozen tape
  (sibling of F1, found while fixing it).
- **Deliberately NOT synced: `SEQ_CORE_CaptureToSlot`'s staged window.** The reset stays
  sync-free, so the source's slots keep matching the ORIGINAL tcc through the borrow and the
  raw restore lands consistent by construction (invariant documented at the reset).
- **HIL: 6 pins in `test_arp_bounce.py`** — CC readback + output-mirror evidence (Self mask
  {D,A}, mode Up → step-dependent snap {62,69,62} vs raw {60,64,67}) across the slot path,
  the whole-group bounce (incl. source restore), the GP8 in-place freeze, the stale-dst
  to-track path, and the F2 bystander (both slot verbs). All green on hardware; full suite
  250/250, zero regressions.
- **F3/F4 closed same session (P4 tail; comments/classification, no behavior change).**
  `win_o` init → the build is now ZERO-warning (any new warning = defect signal). One real
  classification call: **`SEQ_PAR_Type_Waypoint` is PRESERVED by
  `ResetGenerativeForBounce`** — the painted path is deterministic step data (like Note),
  inert on a frozen copy (dir_mode resets to Forward), and re-arming a Wp mode should
  re-use it, not find it silently erased. Also documented: LfoWaveName's static buf
  (one call per printf) and the accepted 1-step-track auto-mutate hole in
  `SEQ_GENERATOR_Tick` (degenerate musically; not worth a per-track advance counter).


**2026-07-11 (cont.) — RT-timing tail closed: #54 prefetch cap, #55 O(1) tape off-match,
#2 Fwd/Rew lost-update (the adversarial review's last software-only items)**
- **#54 (P2 rt-timing): the switch forward-delay prefetch is now capped per service.**
  The margin-sized batch (100–220 ticks at 384ppqn) used to run to completion inside ONE
  1 ms `SEQ_CORE_Handler` service under MUTEX_MIDIOUT — a multi-ms drain/UI starvation
  right at the switch instant. Now prefetched (not-yet-due) ticks are budgeted at
  **8/service** (`SEQ_CORE_PREFETCH_TICKS_PER_SERVICE`); the unmet target is carried in a
  new `bpm_tick_prefetch_carry` and merged (max) with any new `prefetch_req` next service.
  Design points that mattered:
  - **Carry ≠ req.** `SEQ_CORE_AddForwardDelay` refuses while `prefetch_req` is nonzero;
    carrying in the same variable would refuse fresh forward-delays for the whole spread
    window. Separate variable + max-merge keeps both.
  - **Carry the PRE-offset-pad goal.** The negative-port-delay preload (`+offset`) is
    re-derived from real time every service; carrying the padded target would creep it by
    `offset` per service — a runaway prefetch whenever `offset >= budget` (caught in
    self-review before flash).
  - **Due ticks are never capped** (`bpm_tick_must` = real tick + offset pad): realtime
    catch-up after a starved service behaves exactly as before.
  - Completion headroom is structural: the runway to the boundary is `batch` ticks of real
    time, a capped batch completes in `batch/8` services — ~9× margin at any tempo. The
    budget is shared across the handler's do-while re-checks (no fresh burst window), and
    the carry joins `prefetch_req` in `SEQ_CORE_Reset`'s cancel + the capture-span snapshot.
- **#55 (P2 rt-timing): the CAPTURE tape note-off gate back-fill is O(1).** Every drained
  note-off of the recording track used to LIFO-walk the tape ring (up to 768 iterations,
  modulo each) inside the MIDI drain under MUTEX_MIDIOUT — a chord release multiplied it.
  Replaced with `seq_core_cap_tape_open_idx[128]` (index+1 of the most recent still-open
  note-on per note; 256 B main SRAM), re-validated against ring wrap (note match + gate
  still 0) before filling. Chosen over the review's "cap the scan depth" because a depth
  cap breaks exactly the note that matters most — a long-held drone under dense playing
  loses its measured gate. Accepted edge (documented at the decl): a same-note re-trigger
  while the note is still open steals the slot, so the OUTER note of a same-note overlap
  falls back to the default gate.
- **#2 (P3 concurrency): Fwd/Rew tick-jump lost-update — fixed by DELETING code, not by
  the suggested IRQ-wrap.** The review proposed wrapping the read-modify-write in
  IRQ-disable, but the RMW window spans `SEQ_CORE_Reset` (PlayOffEvents) + `NextPos`
  (pattern fetch) — masking IRQs across that is the #17 anti-pattern. Re-derivation
  against current source: `SEQ_BPM_TickSet` is already IRQ-atomic (finding #1's fix), and
  `SEQ_CORE_Reset` already lands the jump atomically; the only REAL lost-update was the
  mainline-inherited trailing `SEQ_BPM_TickSet(next_bpm_tick)` re-pin, which discarded any
  master-ISR ticks elapsed during `NextPos`/`PrevPos`. Dropped it in both verbs
  (`SEQ_SONG_Fwd`/`SEQ_SONG_Rew`); `FetchPos` verified to never move the tick itself.
- **No new HIL pins**: #2 and #54 are timing/race shapes the USB harness can't observe
  directly; the 250-suite pins behavior (capture articulation covers #55's gate fidelity,
  switch/recall tests cover #54's prefetch path). Validation = full suite on hardware +
  the by-ear switch feel; the freeze-net probes (`diag_switch.py`/`diag_freeze.py`,
  CMD_SWITCH_PERF 0x44) remain available if a number is ever wanted.

**2026-07-11 (cont. 2) — Isomorphic keyboard integration push: EDIT-RECORDING punch-in,
collapse-to-scale, live layout/velocity controls (SHIPPED, by-ear GO — "its awesome i
love it" — after two same-session revs below; HIL 250 regression pending)**
Origin: user ask — deepen the B-row isomorphic keyboard (shipped 2026-07-03 with parked
polish): use it in the stock EDIT RECORDING mode, improve the UI, add collapse-to-scale,
push expressiveness.
- **EDIT-RECORDING punch-in.** Play-surface events (melodic keys AND drum pads) are now
  offered to the active page's MIDI-IN callback (`SEQ_UI_NotifyMIDIINCallback`) — the same
  hook `seq_midi_in.c` offers external packages before the record path — gated on a new
  `SEQ_UI_EDIT_MidiLearnActive()` (EDIT page + GP step held). Hold-step + tap key =
  punch that key into the held step, exactly like an external MIDI keyboard into the
  stock MIDI-learn. ALL-held note-copy across selected steps rides for free (it lives in
  the learn handler); monitoring is the stock learn FWD (the step replays as it now
  sounds; `SEQ_RECORD_PrintEditScreen` feedback included).
- **Chord punches land atomically.** The stock learn path is per-note last-wins (per-
  package Enable toggle + `SEQ_RECORD_Reset` after each insert clears the held-note
  stack), so a triad through notify would collapse to its last note. Chord-layout presses
  instead pin the held step through `SEQ_UI_INSSEL_RecordChord(..., force_step)` (new
  param; <0 = pick live as before) and monitor live. Release symmetry via a new per-key
  `inssel_kbd_learned` flag: learned releases go back through notify; if the step was
  released first, the learn exit's AllNotesOff already cleaned up and the fallback live
  note-off is harmless.
- **Inherited stock quirk (documented, not fixed):** the learn handler leaves
  `seq_record_state.ENABLED = 0` on exit, so punching while REC is armed disarms record —
  identical behavior with an external keyboard; fix belongs upstream of this feature.
- **Collapse-to-scale (FOLD).** `INSSEL_KBD_FOLD` — the 16th (last) bit of
  `seq_ui_options`. **Rev same session after first by-ear**: v1 snapped each chromatic
  key in place via `SEQ_SCALE_NoteValueGet`, which left adjacent keys doubling on one
  note ("duplicate notes filling in" — user); reworked to **compact**: with FOLD on,
  Jump strides **scale degrees** from the tonic (`SEQ_SCALE_WalkScale(base, key*jump)`,
  tonic-anchored like the Degrees layout; degree delta clamped to 127 for the s8 param).
  Every key a distinct scale note — Jump 1 = scale steps (≡ Degrees), 2 = diatonic
  thirds, 3 = diatonic fourths. Jump encoder + transient messages read "degrees" in fold
  mode. Degrees/chords layouts are in-key by construction and ignore it. OPT item +
  persisted as `UiInsselKbdFold` in `MBSEQ_GC.V4`.
- **Performance gestures (no OPT trip mid-jam):** SELECT+key2 = cycle layout
  (Chromatic → Degrees → Chords), SELECT+key15 = fold toggle, SELECT+GP1 encoder =
  velocity 1..127 (replaces the fixed 100; plays AND records). Octave keys stay on
  SELECT+key1/key16 — the toggles sit inboard of them. All confirm via transient LCD.
- **Rev same session (by-ear): stock SELECT-tap learn latch REMOVED — no replacement;
  hold-a-step is the only learn gesture.** The stock EDIT page latched MIDI-learn on
  every SELECT tap, which collided head-on with the keyboard's SELECT modifier
  ("annoying" — user): shifting an octave latched learn and the latch then swallowed the
  keys. A double-tap-REC replacement latch was built (PROC-row <350 ms convention,
  flashing REC lamp, latch surviving the deferred page Init) and **dropped the same hour
  on user feedback** — the hold-a-step punch-in already covers the workflow and a second
  latched mode wasn't pulling its weight ("i cant see the point"). Final state: the
  momentary hold-a-step learn (which the play-surface punch-in rides) is the whole
  feature; SELECT on the EDIT page does nothing. One durable gotcha from the dropped
  attempt, worth keeping: **`SEQ_UI_PageSet` defers the target page's Init** (via
  `seq_ui_display_init_req`), so state set right after PageSet gets clobbered by the
  page's Init unless the Init is taught to preserve it.
- **UI:** held keys light pure red (the green in-scale / amber root planes yield while
  pressed — finger feedback was previously absent); INSSEL LCD line 0 gains fold flag
  ("IsoFold J+n"), transpose (`Trn ±nn`) and velocity (`Vel nnn`) readouts.
- Files: `seq_ui_inssel.c` (core), `seq_ui_edit.c` (learn getter), `seq_ui.c` (SELECT+GP1
  intercept), `seq_ui_opt.c` (OPT item 37), `seq_file_gc.c` (persistence), `seq_ui.h`.
  Docs: surface map §4 block rewritten, manual fork gains "The Play Surface" section
  (also fixed the stale "GC unmodified by the fork" row).
- **Ideas surfaced, unbuilt** (expressiveness ladder — pick by ear, each wants a
  workflow-bundle framing per §2.7 before licensing): note-repeat/ratchet on held key at
  a clock division (strongest live-record candidate); latch/sustain mode for drones;
  strum (ms spread) on the chord layout; chord voicing/inversion dial (GP2-encoder
  candidate — would claim a second global encoder, weigh like the GP1 tradeoff);
  hold-time → CC pressure curve; per-key velocity tilt across the row.
- **Validation:** zero-warning build (only the pre-existing uip header-guard warning,
  verified present on unmodified main). **By-ear GO 2026-07-11** across three flashed
  iterations (v1 → fold compaction rev → learn-gesture rev). HIL 250-suite = the
  regression gate, still pending; **no new pin** — the harness has no verb for B-row key
  presses or a held-GP learn gesture (same gap as the waypoint pin, deferred with it).

**2026-07-11 (cont. 3) — Voicing tenant: the internal chord mode joins the rack (SHIPPED, by-ear GO)**
- **Origin ask: "apply Tension to the internal chord mode."** Investigation confirmed the
  fence is structural: a Chord1/2/3 par byte is a table INDEX (pitch exists only after
  `SEQ_CHORD_NoteGet` expansion at emission), so every render-stack pitch processor —
  TENSION included — passes chord tracks through untouched (the Track-2 fence, LOG
  2026-06-10). Two routes assessed: (A) snap the expanded voices at emission — rejected,
  re-creates the emission-effect/bake problem §3 exists to prevent; **(B) chord-space
  gravity = substitute the chord BYTE along the band ladder in the render stack —
  APPROVED as direction ("act 2"), not yet built.** The Hapax-style voicing modifiers
  were pulled forward as the smaller playable loop (build less, listen sooner): they make
  chord mode worth living in before the substitution act lands on top.
- **Voicing = 6th emission tenant, 13th rack row (Sprd · Inv · Strm), chord layers only.**
  Spread/Inv are a **pure function of the chord byte** evaluated inside the
  `SEQ_LAYER_GetEvents` expansion: Sprd 0–12 lifts upper voices an octave per click
  (bottom anchored, cycling — monotone widening); Inv ±8 walks classic inversions
  (+ lifts the lowest voice, − drops the highest); both fold back via `SEQ_CORE_TrimNote`.
  All-neutral or bypassed = byte-identical stock expansion. **Classified deterministic
  SHAPING in `SEQ_CC_ResetGenerativeForBounce` (PRESERVED, the Groove precedent):** a
  captured chord track still holds chord BYTES, so the voicing CCs must survive for the
  copy to re-expand to what was heard. This also un-conflates the stock tables' baked
  voicing entries (Maj.I/II/III, drp3/5) — identity stays in the byte, voicing is a dial.
- **Strum is the honest exception (timing, not pitch):** the expansion stamps each voice's
  direction-resolved pitch rank into a new `seq_layer_evnt_t.strum` byte (fits the
  struct's padding — no RAM growth); emission adds `rank × |Strm|` ticks to the scheduled
  tick (echo trains and rolls follow the strummed onset). Bipolar dial, 64-biased CC:
  CW = up-strum (low voice first), CCW = down; magnitudes reach broken-chord territory
  deliberately (63 ticks/voice). **Durable gotcha: `e->strum` is UNDEFINED for every
  producer except the chord expansion — consumers must gate on the layer type
  (Chord1/2/3) before reading it** (drum tracks gate on event_mode first; their
  lay_const bytes aren't par-layer types).
- **CC allocation: SPREAD took 0x9F — the LAST free slot in the persisted ext block**
  (bits 0..3 value, bit 7 = the row's bypass, Echo-idiom packing; added to the
  phrase-morph SNAP list — a lerp corrupts the flag bit). Safe against old files: the
  bank writer has always clamped unmapped ext CCs to 0. **INV (0xA0) / STRUM (0xA1) are
  RAM-only** — persisting them needs the ext-block V5 tag bump (V2→V3 freeze precedent),
  **licensed by this GO, queued on OPEN_ITEMS** (watch the pattern-slot capacity check:
  the writer silently skips ext blocks that don't fit old-sized slots).
- **Rack integration firsts:** occupancy = ANY dial off-neutral — the first row where no
  single headline CC can proxy occupancy (custom `SEQ_UI_PROC_RowState` arm, the
  IsTrigGen identity-compare idiom). Double-tap bypass = SPREAD bit 7 and **gates all
  three dials in the DSP** (the Humanize shipped-broken-bypass lesson, applied at build
  time). `.status` hook says "no Chord layer on trk" when the tenant is a no-op — the
  dials alone can't explain silence on a Note track.
- **Free side-effect:** the params are ordinary track CCs, so the self-bus Ctrl layer can
  already target 0x9F/0xA0/0xA1 raw — per-step spread/inversion painting works today
  (legibility decode not added; joins the ~40 raw targets).
- Closes (differently-homed) two "ideas surfaced, unbuilt" from the play-surface entry
  above: strum + voicing/inversion dial — they landed on the chord-layer expansion, not
  the live keyboard.
- Files: `seq_cc.h/.c` (CCs 0x9F/0xA0/0xA1 + fields + init/preset defaults),
  `seq_layer.h/.c` (event strum byte; voicing in the chord case; preset table),
  `seq_core.c` (strum offset at the 7 NoteOn schedule sites), `seq_pattern.c` (snap
  list), `seq_ui.c` (row + 2 kinds + custom RowState arm + status hook).
- **Validation: zero-warning build; by-ear GO 2026-07-11 ("hard GO").** MIDI export
  (`seq_midexp`) ignores strum — noted on OPEN_ITEMS. No HIL pin (no harness verb
  renders chord-layer expansions); the 250-suite run = the regression gate.
- **HIL incident during the gate (environment, NOT a regression — worth keeping):**
  the post-GO run came back 247/250, all three reds in the tension family with
  collapses landing on G instead of C. Root cause: the by-ear jam left held notes in
  the **bus notestacks** (RAM; survives session switches) and TENSION's L0 root =
  `BusLowestNoteGet` — the pull ladder re-rooted onto the residue. Proven with the
  pure-function band query (`tension_band_get` per bus: bus 0 = {G}, bus 2 = {E})
  plus a clean-bus replication of the failing collapse (all steps → C on bus 1, same
  firmware). External note-offs can't clear filtered buses from the harness port,
  and `track_config` can't route to a Bus port (7-bit SysEx mangles 0xF0). In-band
  reset = MIDI page GP16 "R.Stacks"; **conftest now drives it at suite start**
  (`_clear_bus_notestacks`) so jam residue can't red a run again. Full memory:
  `reference-hil-bus-notestack-residue`.
- **Final HIL state: green.** Run 2 (stacks cleared): 249/250 — tension trio green, one
  NEW red `test_as_heard_slot_track_threads_phase` (rotation off by a couple of steps +
  one foreign head byte = a capture-while-playing phase race). Classified pre-existing
  FLAKE, not a regression: it passed full run 1 on the same firmware, passed 3/3
  isolated re-runs, and the voicing paths are gated behind chord layer types this
  Note-track test never enters. Every test green in at least one full run this session;
  flake trail on OPEN_ITEMS.

**2026-07-11 (cont. 4) — Act 2: chord-space GRAVITY — the internal chord mode joins the field (SHIPPED, by-ear GO, HIL 254/254)**
- **The original ask lands: TENSION now substitutes the chord BYTE along the band
  ladder** (new chord-layer pass in `tension_render_range` + `tension_chord_snap`).
  Pull collapses chord QUALITY toward the stable skeleton (stock set 0: Maj.I stays in
  SCALE, → R.+5th in CHORD, → Root in DRONE); push substitutes toward tense color
  (LEAN: Maj.I → Maj.6) and only on a STRICT band-coverage improvement, so an
  already-tense chord stays put (the note snap's d=0 stability at chord grain). Same
  grip hash/threshold as the note pass — the whole chord grips as a unit, monotone
  pull preserved (pull zone-key 0; bands nest).
- **Root-relative table space is the key structural fact:** chord entries are rooted
  by the transposer at emission, so the absolute band is rotated into table space by
  the track's effective transpose PC (`tension_chord_transpose_pc` — the mod-12 shadow
  of `SEQ_CORE_Transpose`: semi nibble + live transposer note; octaves drop out).
  Consequence: substitution changes QUALITY, never re-roots. When a narrow band has no
  fitting entry, the pull RELAXES through the wider nested bands (L0→leL2→leL3, extra
  `TensionBandMask` calls hoisted per render) instead of giving up — collapse goes as
  far as the table allows. Accepted degrades: deep DRONE off the field root stops at
  the nearest quality; SLIP falls back to max-overlap (true planing is inexpressible
  root-relatively).
- **Faithfulness by construction, one level up from notes:** the substituted byte
  lands in the OUTPUT mirror — EDIT shows the substituted chord NAME, capture/bounce
  re-expand it identically, and the act-1 Voicing dials apply to whatever chord the
  field chose (render substitution → emission voicing compose with zero coupling).
  Byte 0 stays the rest idiom (never touched, never emitted — the oct-bits-000 + ix-0
  collision is guarded). Arp playmode fenced for parity (A8). Empty/undefined entries
  pass through.
- **Infrastructure:** per-entry pitch-class masks precomputed at init
  (`SEQ_CHORD_PCMaskGet`, seq_chord.c — render scan = mask compares, ~32×2+34 u16);
  `render_live_sig`'s TENSION case gains the track's OWN transposer context
  (busasg.bus ≠ chord-context bus) for chord-layer tracks, so a live transposer move
  re-renders gripped chord tracks.
- **Validation: zero-warning build; 4 new HIL pins** (`test_tension_chord.py`:
  detent byte-identity, DRONE collapse + rest guard, the quality ladder with exact
  per-step gripped/ungripped expectations, partial-grip determinism) — expected bytes
  cross-verified against an offline python simulation of the exact algorithm + tables
  before first hardware run; **all 4 passed first try. Full suite 254/254 = the new
  baseline** (one clean run — the 07-11 as-heard flake did not recur; the conftest
  bus-stack clear held). **By-ear GO 2026-07-11 ("works! GO").**
- Files: `seq_chord.c/.h` (PC-mask precompute + getter), `seq_core.c` (snap +
  transpose-PC helpers, chord pass, live-sig term), `tests/apps/seq_v4/
  test_tension_chord.py` (new), `tests/harness/sysex.py` (CC.LAY_CONST_A1).

**2026-07-11 (cont. 5) — Ladder rung 1: Drop + Tilt dials (SHIPPED, by-ear GO, HIL 254/254) + the expressiveness-ladder plan + the A1 CC-debris incident**
- **The surfaced ideas got a durable home:** `doc/plans/2026-07-11-chord-mode-expressiveness-ladder.md`
  — 6 rungs (Drop/Tilt · per-step voicing par layer · GRAVITY×spread coupling ·
  Ctrl-layer legibility · V5 persistence · strum export), each its own by-ear gate.
  Key insight written down so it survives: once voicing is a PAR LAYER it lives in the
  render buffer, so "GRAVITY collapses spread" and "LFO on spread" become ordinary
  render targets with MIRROR-faithful bounce — prefer that route over any emission
  coupling.
- **Rung 1 shipped: Drop (0..3 = off/Drop2/Drop3/Drop2&4) + Tilt (bipolar ±63)** on the
  Voicing row (now 5 dials). Drop = classic jazz drops applied Inv → Drop → Sprd (both
  Drop2&4 targets resolve against the close position BEFORE either moves — textbook);
  Tilt = linear velocity ramp by pitch order, top-accent CW / bottom CCW, clamped
  1..127 (a 0 would rest the voice via the disabled-note idiom). Same class as the
  act-1 trio: pure functions of the chord byte, preserved on bounce, gated by the row
  bypass, occupancy extended. CCs 0xA2/0xA3 RAM-only (V5 payload now 0xA0..0xA3).
  PROC_KIND_VOICE_STRUM generalized to PROC_KIND_VOICE_BIPOLAR (Strm + Tilt share it).
- **HIL incident #2 of the day (environment again — the DIAGNOSTIC is the keeper):**
  the validation run red-ringed the recorder-capture family with reads that looked like
  a 6-step buffer rotation (and, at first glance, like act-2 substitution: chord ix
  1 → 7). Real cause: a jam + auto-writeback had saved **TRANSPOSE_SEMI = −6** (+
  groove 80, length 10) into AUTOTEST A1 track 0 — the migrated PITCH render bakes
  transpose into the mirror, and TrimNote's octave fold turns v−6 into a fake
  "rotation" (1→7, 6→0-rest at the seam). The event-mode self-heal passes this class.
  **Diagnostic that cracked it: diff the sick fixture's track-0 CCs against a healthy
  fixture's.** Fixed A1 + hardened conftest: `_ensure_autotest_fixtures` now
  neutralizes semi/oct/groove debris and persists the fix.
- **Process lesson, recorded:** a user interrupt does NOT kill a background pytest —
  restarting created two concurrent suites on one device (both runs void, ~130 broad
  cross-talk failures each). Check for a live pytest before kicking a run.
- **Validation:** zero-warning build (only the pre-existing uip header-guard pair on a
  full rebuild); full suite **254/254** after the A1 repair; **by-ear GO 2026-07-11.**
- Files: `seq_cc.h/.c` (0xA2/0xA3), `seq_layer.c` (drop transform, tilt ramp, preset
  rows), `seq_ui.c` (2 kinds reworked, 5-dial row, occupancy), `tests/conftest.py`
  (CC-debris heal), plan doc, OPEN_ITEMS V5 item widened.

**2026-07-11 (cont. 6) — ext-CC block V5 bump: voicing dials persist (SHIPPED, HIL 256/256 = new baseline)**
- **What:** the per-track ext block gets tag **0x05 (V5)**: CC range widened
  `0x80..0x9f` → **`0x80..0xaf`** (COUNT 32 → 48) so the voicing dials
  INV/STRUM/DROP/TILT (0xA0..0xA3) persist with the pattern; 0xA4..0xAF = headroom.
  Follows the V2→V3 precedent exactly: **V3/V4 count frozen at 32**
  (`SEQ_FILE_B_TRK_EXT_CC_COUNT_V3`, like the frozen V2=22), per-tag stride in all
  three read paths (PatternRead / TrackRead / PhraseReadCCs), write ladder
  **V5→V4→V3→none** per record (a degraded record carries only the frozen 32-CC
  payload; gen sub-block rides V4+). `SEQ_FILE_B_TRK_EXT_SIZE` → V5 (822/track), so
  only sessions/banks **created by V5 firmware** reserve V5 room — older slots
  degrade silently (voicing resets to neutral on reboot there; recreate the session
  to upgrade). Old firmware reading a V5 file: unknown tag → ext skipped, no
  misalignment (same forward-compat degrade as ever).
- **The one non-mechanical decision — NEUTRAL-extend, not zero-extend:**
  `PhraseReadCCs` fills the morph B endpoint; the old V2 arm zero-extended (correct
  there — masks/GRIP are 0-neutral), but STRUM/TILT are **64-biased** (center detent
  = off). A zero-fill would make "morph toward an old-format phrase" sweep strum/tilt
  to hard-left instead of to neutral. New `ExtCcNeutral(cc)` helper (64 for
  0xA1/0xA3, 0 otherwise, matches `SEQ_CC_TrackInit`) extends every shorter record
  to the live count.
- **Headroom constraint written at the define:** V5 records persist 0xA4..0xAF as 0
  TODAY, so any future CC assigned into the headroom must be 0-neutral (mask /
  selector / two's-complement-nibble idiom). A 64-biased dial there would read "hard
  left" from every record written before its birth — that shape needs a V6 bump, not
  a headroom slot.
- **Posture-morph:** snap list gains VOICE_INV (two's-complement nibble — raw lerp
  crosses the +7/−8 discontinuity) and VOICE_DROP (discrete selector); STRUM/TILT
  lerp (64-biased linear, per the OPEN_ITEMS spec). Unmapped headroom CCs in the
  morph loop are inert (`SEQ_CC_Set` returns −2, no write, no dirty).
- **Old-range read semantics kept (precedent):** a V3/V4 record read leaves
  0xA0..0xAF at their in-RAM values (same as the V2 arm has always left 0x96+ alone)
  — loading an old pattern does NOT stomp live voicing to neutral.
- **Validation:** zero-warning build. New `test_voicing_persist.py`: pin 1 =
  round-trip on a **freshly created session** (`V5EXT` — AUTOTEST's banks are
  older-firmware-sized and would degrade; restores AUTOTEST in `finally`); pin 2 =
  degrade-non-corruption on AUTOTEST (frozen range incl. SPREAD@0x9F must
  round-trip; 0xA0+ dials must read written-or-clobbered, never a third value =
  misaligned stride). **Full suite 256/256 = the new baseline.**
- **Harness gotcha found by the first run (pin 1 red, everything else green):**
  a freshly created bank is SPARSE (`SEQ_FILE_B_Create` is header-only), so the
  first `pattern_save` to a HIGH slot must allocate the whole cluster chain up to
  `N × pattern_size` — slot 61 ≈ 550 KB of FAT allocation, which outran the verb's
  4 s reply timeout (`cmd 0x6d` TimeoutError; the save itself was fine — the V5EXT
  session came up intact). Fix: first-touch writes on a fresh bank use a LOW slot
  (extension trivial) + `timeout=30.0`. AUTOTEST never shows this because its bank
  files are already grown. The `finally` AUTOTEST-restore held even on the failing
  run — the other 255 stayed green.
- Files: `seq_file_b.h` (range + headroom constraint), `seq_file_b.c` (tag, frozen
  count, sizes, 3 read arms, write ladder, ExtCcNeutral), `seq_pattern.c` (snap
  list), `seq_cc.h` (comment), `tests/harness/sysex.py` (CC.VOICE_*),
  `tests/harness/board.py` (docstring), `tests/apps/seq_v4/test_voicing_persist.py`
  (new).

**2026-07-11 (cont. 7) — Ladder rung 2: per-step voicing par layers (BUILT, zero-warning; flash + HIL + by-ear pending)**
- **What:** the four sweepable voicing dials become paintable per step via four new
  thin par-layer types `SEQ_PAR_Type_VSprd/VInv/VStrm/VTilt` (21..24) — one
  64-biased bipolar byte/step, **0 = unpainted (neutral, shown as `.`)**, composed
  onto the dial at chord expansion: `eff = clamp(dial + step − 64)` (Sprd 0..12,
  Inv −8..+7, Strm/Tilt 0..127). Emit no MIDI (Waypoint precedent, no `default:` in
  the GetEvents switch = silent for free); muted offset layer reads neutral;
  Drop stays dial-only (discrete selector, not a ramp).
- **Forks settled (user decision, AskUserQuestion):** thin layers over the packed
  spread-nibble+inv-nibble byte (encoder inc/dec would carry across nibbles, display
  would need a 2-field decode, rung-3 scalar writes would be nibble RMW); **OFFSET
  over OVERRIDE** (dial stays the performance macro, layer = automation contour —
  override would kill the macro sweep wherever painted); **all four params** get a
  layer (not just the Sprd+Inv core pair).
- **The rung-3 unlock, now real:** the layers are ordinary par bytes in the render
  mirror — a render pass (GRAVITY register-collapse, LFO→spread) can WRITE them and
  the result is bounce-faithful by construction. That was the whole point of making
  voicing a PAR LAYER instead of more emission coupling.
- **Mechanical consequence worth remembering:** emission used to compute the strum
  stagger as `e->strum(rank) × |dial−64|` — a per-step VStrm offset can't reach that
  without riding the event. `seq_layer_evnt_t.strum` widened u8→u16 and now carries
  the **precomputed tick offset** (direction-resolved rank × eff ticks/rank); the
  emission block gates on `e->strum != 0` + the existing chord-layer-type check and
  no longer re-reads the dial. Only two touch points existed (producer
  seq_layer.c chord case, consumer seq_core.c pre-ScheduleEvent block).
- **Grammar note:** 64-biased-bipolar-at-center-detent IS the processor-dial rule
  applied to a layer; preset-fill on EVENT-page type confirm = painted-neutral 64,
  raw/foreign 0 bytes = unpainted pass-through (defensive: fresh par memory is
  memset-0). Bounce: V* layers PRESERVED by ResetGenerativeForBounce (deterministic
  SHAPING like the dials — same byte+dials+offsets re-expand identically). Persistence
  free (ordinary par layers in the bank file — none of the V5 ext-block ceremony).
- **Encoder speed:** V* deliberately NOT in the fast-encoder type list (useful range
  is ±12; slow default is right).
- **Validation so far:** zero-warning build; new `test_voicing_steps.py` (6 pins:
  VSprd dial≡layer set-equivalence, offset-composes-not-overrides (dial 2 + layer +1
  ≡ dial 3), unpainted=pass-through + big-offset clamps to dial ceiling, VInv
  dial≡layer, VStrm staggers via dial-only (guards the emission refactor) AND
  layer-only, VTilt velocity ramp). **Firmware not yet flashed** — suite run and
  by-ear GO happen after upload; ladder plan doc updated (rung 2 BUILT, rung 4
  SUPERSEDED — the V* layers are the legible per-step surface).
- Files: `seq_par.h/.c` (types, names, UI map, defaults/max), `seq_cc.h/.c` (4 link
  fields + LinkUpdate + bounce-preserve comment), `seq_layer.h` (strum u16),
  `seq_layer.c` (offset read + eff composition + precomputed strum), `seq_core.c`
  (emission strum block), `seq_lcd.c` (2 display cases), MANUAL_FORK (per-step
  voicing section, heading now names all 5 dials), ladder plan doc,
  `tests/apps/seq_v4/test_voicing_steps.py` (new).

**2026-07-12 — Rung-2 boot hard-fault postmortem: task-stack overflow via event-struct widening (FIXED, rebuilt; flash pending)**
- **Symptom:** first flash of rung 2 hard-faulted at boot with PC=0x00000000 during
  the SD phase ("SD Card not found" in the debug stream, garbage on the right LCD);
  a second boot loaded fully then froze. Reproduced identically after a clean
  re-upload → not transit corruption (the 2026-06-11 md5 trick proved the hex fine).
- **Root cause:** cont. 7 widened `seq_layer_evnt_t.strum` u8→u16 so the strum tick
  offset could ride the event. That grew the struct 8→12 bytes (alignment), and the
  struct sizes the **`layer_events[83]` stack arrays** (MBSEQV4P `GetEventsPlus`
  emission path, `GetEvntOfLayer` UI/LCD path, seq_record) — **+332 bytes of stack
  per frame** on task stacks tuned to 1000–2100 bytes. FreeRTOS task stacks live in
  `ucHeap`, so the overflow smashed neighboring TCBs/semaphores → context switch
  into garbage (PC=0), file task death ("SD Card not found"), boot-to-boot symptom
  roulette. Dead-end theories worth remembering: transit corruption (ruled out by
  clean re-flash), stale incremental objects (.d deps were fine), MSP/ISR-stack
  starvation (the top-of-RAM gap was ~8.6 KB — arithmetic said no).
- **Fix (three parts):**
  1. `strum` reverted to **u8 rank** + compile-time size guard
     (`_seq_layer_evnt_size_guard` pins sizeof==8; every byte of that struct costs
     83 bytes of task stack per frame). The effective ticks-per-rank (dial +
     per-step VStrm offset, muted layer = neutral) is now composed **at emission**
     from the same tick's tcc/mirror state — semantics identical to the u16 design,
     zero footprint anywhere. Cheap gate: `voice_strum != 64 || link vstrm >= 0`.
  2. `seq_core_cap_snap` (~5.6 KB re-sim snapshot, pure task-context memcpy state,
     never DMA) moved to **CCM** — main-RAM tail gap grows 8.8 KB → 14.3 KB; CCM
     headroom left: 1464 bytes (watch it: next CCM tenant needs to check).
  3. Linker `_Minimum_Stack_Size` 0x100 → **0x400**: the `._usrstack` check section
     is the only fence between bss growth and the MSP region — now a sub-1 KB tail
     fails at link time instead of corrupting at runtime. (Guards the *main* stack
     tail, NOT the FreeRTOS task stacks that actually blew here — those have no
     static guard; the sizeof pin is the defense for this class.)
- **Durable rule:** anything that grows `seq_layer_evnt_t`, adds big locals to the
  GetEvents/GetEventsPlus call chain, or fattens hot-path stack frames is a
  task-stack budget change on a ~1.4 KB budget — treat like an ISR-context change,
  not an ordinary struct edit.
- **Validation:** zero-warning rebuild (uip pair aside), hex 63b06a25…; RAM map
  verified (__ram_end 0x2001cc48, __ram_end_ccm 0x1000fa48). Flash + HIL + by-ear
  still pending (cont. 7 gates unchanged). Known-good V5 hex staged from 5d8ca867
  as fallback during diagnosis.
- Files: `seq_layer.h` (u8 + guard), `seq_layer.c` (rank, comments), `seq_core.c`
  (emission-side eff composition; cap_snap → CCM_SECTION),
  `etc/ld/STM32F4xx/STM32F407VG.ld` (stack fence 0x400).

**2026-07-12 (cont.) — Rung-2 pins: 6/6 red → geometry, not firmware (FIXED, 6/6 green)**
- After the hard-fault fix flashed clean: baseline 256/256 green, all 6 new
  voicing-steps pins red with a total per-step no-op. Live probe: layer types
  stored (cc readback 21), chord byte reached the mirror, painted VSprd byte
  read 0 even post-render.
- **Root cause — the test, not the feature:** AUTOTEST A3 track 0 has a
  **1-par-layer geometry**. Painting "layer B" hit SEQ_PAR_Set's bounds reject;
  `SEQ_CC_LinkUpdate` correctly scans only real layers (the type in
  `lay_const[1]` is inert config debris), so `link_par_layer_v*` stayed −1. The
  on-device UX has no such trap (EVENT page bounds layer selection to the
  geometry).
- **Accomplice:** `cmd_track_par_set` discarded SEQ_PAR_Set's return code and
  always replied OK — the harness had no way to see the rejected writes. FIXED:
  the verb now propagates the verdict (status 0x02 on rc<0; hex a107764d, flash
  with the next upload).
- **Test fix:** pins provision their own geometry via the existing
  `track_note_init` verb (16 steps × 4 par layers) + trg gate fill; restore =
  `pattern_load(A3)` + explicit 0xA0..0xA3 dial neutralization (old-format
  AUTOTEST slots keep those dials in-RAM across a load). **6/6 green (38s)**
  against the already-flashed firmware — the feature worked all along.
- New baseline after next flash + full run: expected 262. By-ear GO still open.
- **Display fix (same session, caught by eye on device):** the V* EDIT cells used
  `"%+3d "` — the LCD vsprintf has NO '+' flag (documented at SEQ_LCD_PrintSigned
  and in seq_ui.c — third strike for this trap), so cells rendered a literal "3d"
  and underprinted, leaving the last ~3 step cells stale. Both cases now use
  SEQ_LCD_PrintSigned (hand-emitted sign, fixed 4-char cell). Hex 20f1492c.
- **OUTCOME: by-ear GO 2026-07-12** (dials + painted offsets + display verified on
  device) — **full suite 262/262 single-pass on hex 20f1492c = the new baseline.**

**2026-07-12 (cont. 2) — Ladder rung 3: GRAVITY × Voicing register collapse (BUILT, flash + HIL + by-ear pending)**
- Deep pull narrows WHAT the chord is (act 2, byte substitution); rung 3 narrows
  HOW WIDE it sits: through DRONE (−49..−64) the TENSION render pass scales each
  step's **effective spread** (Sprd dial + painted VSprd offset, expansion-clamped
  0..12) toward close position and writes the collapsed OFFSET byte back into the
  render mirror's VSprd layer. Full pull + full grip = spread 0.
- **Route: via rung 2, as the ladder plan preferred** — a render write, so
  expansion / tape / bounce all read the same narrowed voicing. The interim
  emission route (scale at expansion) stays UNBUILT: it would be invisible to
  OutputActive (§3 faithfulness class the fork is retiring).
- **Continuous, not hash-gated:** register is a field-wide squeeze — GRIP scales
  DEPTH (`keep = 1 − depth×grip/2032`) instead of selecting steps; the grip hash
  keeps selecting which chord BYTES substitute. Deliberate: at partial grip
  mid-DRONE the ungripped (still-tense, still-wide) chords narrow too — that's
  where the musical value lives, since at full pull + full grip the byte pass
  already lands on 1-voice Root entries where spread is moot anyway.
- Exact math (pinned): `depth = −g − 48` (1..16); `collapsed = (eff×(2032 −
  depth×grip) + 1016) / 2032` (round-to-nearest, ties down); write `64 − dial +
  collapsed` (range 52..76 — never 0 = unpainted); **write only when collapsed ≠
  eff** (byte-identical pass-through at zone entry / shallow depth / closed
  voicings — unpainted 0 stays 0). Zero new state, zero RAM.
- **Scope gates:** chord layer + VSprd layer + playmode ≠ Arp (A8 fence) +
  voicing not bypassed (Sprd bit 7) + pull deeper than −48. **No VSprd layer =
  no collapse** — the coupling is opt-in per track via the rung-2 EVENT-page
  layer assignment.
- **Durable rule (new live sig input):** the Sprd dial byte (value + bypass bit)
  and the VSprd layer link are folded into render_live_sig's TENSION chord-track
  branch. The voicing dial CCs have NO slot sync (emission-pure before rung 3),
  so the sig fold is the ONLY thing that re-renders the collapse on a dial turn
  — regression-pinned by `test_dial_move_rerenders_collapse` (transport RUNNING:
  the sig lives in the tick prologue, so a STOPPED dial turn is structurally
  invisible until the first tick of the next PLAY — same platform class as a
  stopped held-chord change; the first pin version tested it stopped and
  correctly failed).
- Pins: `test_tension_voicing.py` (6, mirror-exact, offline): zone fencing
  (detent / −48 / push all byte-identical), full-pull close (all 52), exact ramp
  math composed onto painted offsets, grip-scales-depth, closed-voicing writes
  nothing, dial-move re-render. Fixture provisions its own 4-par-layer geometry
  (the A3 1-par-layer trap from the rung-2 postmortem).
- Validation: zero-warning rebuild; RAM map unchanged (__ram_end 0x2001cc48 /
  __ram_end_ccm 0x1000fa48).
- **OUTCOME: by-ear GO 2026-07-12** (user: GRAVITY sweep through DRONE on a
  gripped chord track with a VSprd layer) — **full suite 268/268 single-pass
  (8:37) = the new baseline** (6 new pins; the dial-move pin first ran stopped,
  failed correctly, and now pins the tick-prologue mechanism while playing).
- Files: `seq_core.c` (tension_render_range rung-3 block; render_live_sig fold),
  `tests/apps/seq_v4/test_tension_voicing.py`, ladder plan doc + manual updated.

**2026-07-12 (cont. 3) — Ladder rung 6: strum in MIDI export — claim STALE, export already faithful (ladder COMPLETE, plan archived)**

- **The OPEN_ITEMS §4 item ("`seq_midexp` export renders unstrummed onsets", noted
  during act 1) is FALSE — no code change needed.** Source: `strum_ofs` is composed
  into the scheduled **timestamp** at every NoteOn schedule site (`seq_core.c`,
  `SEQ_CORE_ScheduleEvent(... bpm_tick + t->bpm_tick_delay + strum_ofs ...)`), and
  `SEQ_MIDEXP_GenerateFile` replays the REAL scheduler tick-by-tick with hooked
  callbacks — `SEQ_MIDI_OUT_Handler` drains strictly by `timestamp <= export_tick`
  and the export hook writes the delta from the drain tick. Everything
  timestamp-shaped (strum, echo trains, groove delay) lands in the file tick-exact.
  The render mirror is fresh during export too: `SEQ_CORE_RenderTracks()` runs in
  `SEQ_CORE_Tick`'s prologue, which export drives directly — so per-step VStrm
  offsets and the rung-3 GRAVITY collapse also export as heard.
- **Proven on hardware, tick-exact** (`tests/diag_strum_export.py`): chord track
  (Maj.I, steps 1+9), Disk-page export UI-walked via testctrl, the .MID pulled back
  over SysEx and parsed. Detent: both chords' 3 voices at identical ticks. Dial
  64+20: onsets exactly `[0,20,40]` / `[768,788,808]`, low voice first. No firmware
  delta → 268/268 baseline stands (spot-check `test_voicing_steps` + smoke 11/11
  post-diag).
- **New harness capability (no new firmware): SD file readback over SysEx.** The
  firmware already speaks the MIOS Filebrowser protocol (`FILE_BrowserHandler` via
  `seq_terminal.c`); the diag's `Filebrowser` class (debug-string SysEx `0x0D 0x01`
  command / `0x0D 0x41` reply frames; `read` streams `%08X`+hex 32-byte blocks) is
  importable for any future test that needs to inspect files the firmware wrote.
- **Trap found while validating (harness, not firmware): `track_note_init` zero-fills
  the par buffer → the Velocity layer is all-0 → every note emits at velocity 0 =
  suppressed at emission AND silent in export.** The rung-2 voicing pins never hit it
  because they reassign layer B away from Velocity (link lost → default 100). Any
  future fixture that keeps the Velocity layer must paint it.
- **Environment discovery — a third cause for "broad HIL fails / box looks dead":
  BPM Auto-mode slave latch.** Found the box with PLAY showing running=TRUE but
  `bpm_tick` frozen (Auto + MClk-In USB1 enabled; external clock seen at some point
  latches slave → PLAY arms and waits forever). `CMD_TRANSPORT` start
  (`SEQ_BPM_CheckAutoMaster` + start) cleared it; PLAY behaves again in Auto. If HIL
  goes broadly silent with a running transport, check the BPM page mode FIRST.
- Byproducts: harness `Page.DISK=53` added, `Page.BPM` corrected 47→46 (was unused;
  the page enum includes ETH — uIP still compiled in).
- **The expressiveness ladder is COMPLETE** (1 Drop/Tilt, 2 per-step layers,
  3 GRAVITY×Voicing, 4 superseded, 5 V5 persistence, 6 resolved-stale) — plan moved
  to `doc/plans/archive/`. Rung-6 line removed from OPEN_ITEMS §4; manual caveat
  corrected (export follows the strummed onsets).
- Files: `tests/diag_strum_export.py` (new), `tests/harness/sysex.py`,
  `doc/OPEN_ITEMS.md`, manual, ladder plan → archive.

**2026-07-12 (cont. 4) — Ptch merge: Pitch + ChordMask become one row, and the PROC base layout**

- **New working convention: LCD layouts are communicated as tab-per-column grids** —
  one cell per LCD character, two rows for the 2×40+2×40 panel (pasteable from a
  spreadsheet). First use: the user mocked the merged pitch page; the mock parsed
  cleanly onto the 5-col GP-cell grid and drove this whole session.
- **The rack drops from 13 to 12 rows: the ChordMask row dissolves into Pitch.** The
  merged **Ptch** row fills all 8 encoder cells — Semi Oct **Str Bus** FTS Scle Root
  Deg — and carries the mask face (`PROC_FACE_CHORDMASK_SELF`) + both stack slots
  (RowState ORs PITCH and CHORDMASK: alive when either is, strength = the louder).
  Decisions (AskUserQuestion, all recommended options): dots for inactive keyboard
  slots · GP buttons 1-12 keep painting (encoders and GP buttons are separate
  hardware) · dissolve the row rather than keep both.
- **PROC base-layout header (all 12 rows):** identity compressed + right-aligned to
  cols 65–79 — `Ptch  1/12 G1T1` (new per-row `.abbr`, stock `SEQ_LCD_PrintGxTy`),
  replacing `Pitch     1/13 Trk 1` at col 40. Row-0 left of it now belongs to the
  row readout; BYP cue moved 61→55, plane cue 75→60; Tension zone name right-
  justifies to 64 instead of 79.
- **The mask keyboard is now FIXED-position and always drawn** (row 1 right screen):
  `M*:`/`M: ` + one 3-col cell per PC C..B — active = note name, inactive = dot.
  Gate change: paint/LEDs read the **CC** Self bit, not the live slot (`visible ==
  paintable`) — painting a parked mask is harmless (engage reads the CCs) and it
  unblocks Arp-Self painting when ChordMask is not engaged. Scale name promoted to
  row 0 (trimmed, may run to col 60 — deliberate); Deg landing note (`>E`) follows
  it when Deg≠0 and it fits under col 65.
- **Row==slot identity is GONE** (was: first 5 rows == slot indices). Fallout fixed:
  focus init + LIVE landing → `PROC_ROW_PTCH` (0); the B-row dbl-tap ChordMask
  branch folded into the stack-row reset — **Ptch dbl-tap = SlotReset (all 8 dials,
  globals included, as Pitch always did) + drop the ChordMask playmode to Normal;
  painted mask CCs survive** (the FX-bypass spirit).
- **No CC/persistence delta** — pure UI-layer merge: same CCs, same slots, same DSP.
  HIL drives CCs/testctrl (no row-index deps found in tests/).
- Validation: zero-warning rebuild, RAM map unchanged (`__ram_end 0x2001cc50` /
  `__ram_end_ccm 0x1000fa48`, CCM tail 1464 B). **Flash + HIL + by-ear PENDING.**
- Files: `seq_ui.c` only (+ surface map §5a/§5/header, this log, §9 chronology).

**2026-07-12 (cont. 5) — Voic merge: Voicing + Limit become one row at rack position 2**

- Second tab-grid mock of the day. **The rack drops 12→11: the Limit row dissolves
  into Voicing**, and the merged **Voic** row MOVES to position 2, right after Ptch —
  the pitch-SPACE rows cluster at the top (Ptch = what the notes are, Voic = where
  they sit). New rack order: Ptch · Voic · Arp · Tension · Echo · Groove · LFO ·
  Robotize · PitchGen · TrigGen · Humanize.
- **Dial bank: Sprd Inv Drop Strm Tilt · [gap] · Lo Hi** — the mock deliberately
  leaves cell 6 empty as a GROUP SEPARATOR. New `PROC_KIND_SPACER`: a blank grid
  cell inside one row (label "", read=0, write/push no-op — its cc slot is 0 and
  must never reach SEQ_CC_Get/Set). First cross-rowkind merge: Voicing is EMISSION,
  Limit was STACK — the row keeps rowkind EMISSION and its custom RowState arm now
  ORs the LIMIT stack slot (the Ptch/CHORDMASK pattern).
- **Double-tap (user decision): bypass + limit off** — flips the SPREAD 0x80 bypass
  (voicing dials preserved) AND resets Lo/Hi→0 on the OFF edge only (no spare bypass
  bit in the full-range Lo/Hi CCs; two knobs to re-dial). Re-enable restores voicing,
  not the discarded range.
- **Status (user decision): keep + add range** — the chord-layer warning / strum
  direction stays on row 1; row 0 (the base-layout readout zone, col 41) gains
  `Rng <lo>..<hi>` as NOTE NAMES via stock `SEQ_LCD_PrintNote`, only when the clamp
  is active — PrintNote prints `---` for 0, which is exactly Limit's open-side
  semantics (hi=0 = open top). Range prints even on non-chord tracks (Limit clamps
  any track; the voicing warning only covers the dials).
- Mock said `2/12` = current-firmware count again (the 1/13 pattern) → ships as
  `Voic  2/11 G1T1`.
- **No CC/persistence/DSP delta** — same CCs, same LIMIT slot, UI-layer only.
- Validation: zero-warning rebuild (+128 B flash), RAM map unchanged. **Flash + HIL
  + by-ear PENDING** (stacked on the unflashed cont. 4 build — flash once, test both).
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-12 (cont. 6) — Voic clamp decoupled from the bypass + Hi rests at 127**

- **Reverses cont. 5's "bypass + limit off" double-tap draft (user call, same session,
  pre-flash): the range clamp is INDEPENDENT of the row bypass.** The 0x80 SPREAD bit
  only ever gated the voicing dials in the DSP — the coupling was UI-only, now removed.
  Double-tap = voicing bypass alone; an active clamp keeps applying and (correctly)
  keeps the row enabled/green via the RowState arm. Kill the clamp by pushing the
  Lo/Hi encoders to their detents.
- **Lo/Hi default to 0/127 — the resting range reads as the full keyboard.** New
  `PROC_KIND_LIMIT_HI`: the CC keeps the STOCK encoding (0 = open top, DSP
  substitutes 127) but the dial presents it honestly — read 0→127, write 127→0,
  logical range 1..127, deflt/push detent = 127. Pass-through sits at the TOP of
  the sweep (the bipolar-center-detent idea, at the ceiling). Zero seq_core /
  persistence delta; fresh tracks, old sessions, and bounce resets (raw 0) all
  read Hi=127; a legacy stored 127 is display-normalized to open in the Rng readout.
- Inherently seed-safe: even if engage-seeding ever reached the row, seeding Hi=127
  writes raw 0 — a no-op.
- Validation: zero-warning rebuild, RAM unchanged. Flash/HIL/by-ear still pending
  (one hex carries cont. 4-6).
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-12 (cont. 7) — Tension row expansion: Shade + doubled FTS, position 3**

- Third tab-grid mock. No merge this time — the Tension row GROWS: dial bank
  **Grip · Grav · Shade · [spacer] · FTS**, and the row moves to **position 3**
  (ahead of Arp): the pitch-space cluster is now Ptch(1) Voic(2) Tens(3).
- **Shade joins the rack from the GRAVITY page** — the brightness ladder (7 parallel
  modes Lyd..Loc) as a dial. It is a VIEW on the GLOBAL scale, not a CC: new
  `PROC_KIND_SHADE` reads the ladder position (-1 = off-ladder, shows "---"), write
  goes through new exports `SEQ_UI_GRAVITY_ShadePosGet/ShadeName/ShadeSet` (one
  ladder source of truth; same side effects as the page — store flag + full
  re-render). **Guard: write<0 = NO-OP, and the dial's deflt is -1** — encoder-push
  and the double-tap SlotReset must never yank the global scale onto the ladder
  mid-jam. Accepted divergence: CCW from off-ladder does nothing (the page jumps to
  Loc); you enter the ladder CW.
- **FTS is DOUBLED from Ptch** (user: "for convenience") — same per-track
  trkmode_flags.FORCE_SCALE, two homes; GRAVITY and force-to-scale play as one
  instrument. First deliberately-duplicated dial on the rack.
- **Status rework:** zone name + signed value LEFT-anchored at col 41 (the
  base-layout row-readout home; was right-justified); the 16-track grip bar KEPT
  (user decision vs the mock's blank cells) but UNLABELED at cols 24-39 — the 16
  tracks fit the dead cells right of FTS exactly. Zone strip + GP9-16 face
  unchanged.
- Mock said `3/12` = the running-count pattern again → ships as `Tens  3/11 G1T1`.
- Validation: zero-warning rebuild (+184 B flash), RAM unchanged. Flash/HIL/by-ear
  pending (one hex now carries cont. 4-7).
- Files: `seq_ui.c`, `seq_ui_gravity.c` (Shade exports), `seq_ui.h` (+ surface map,
  this log, §9 chronology).

**2026-07-12 (cont. 8) — PGen/TGen two-screen sets: Roll on cell 8 of both planes + the plane cue becomes the row readout**

- Fourth tab-grid mock ("2 screen sets for the two modes" — the OPER and STEP planes
  of PitchGen). **Roll moves to cell 8 and is DOUBLED onto both planes** (the FTS
  idiom): one physical "dice" encoder, reachable without a plane flip. Spacers pad
  the gap — OPER = Lo Hi Rate Dpth Cont ··· Roll, STEP = Win Anc Snp Bnc ··· Roll.
- **TrigGen mirrored** (not in the mock, but the siblings share one grammar — same
  status hook, same idiom): Dens Rate Dpth ···· Roll / Win Anc Snp Bnc ··· Roll.
  Encoder 8 = reroll on all four gen plane-screens.
- **PGen moves to position 4** (the mock's `4/12` identity — the mock-by-mock rack
  rebuild continues: Ptch 1, Voic 2, Tens 3, PGen 4). TGen stays in the tail
  pending its own mock. Order now: Ptch · Voic · Tens · PGen · Arp · Echo · Groove
  · LFO · Robotize · TrigGen · Humanize.
- **The plane cue grew into the row-0 readout**: `OPER 1/2` / `STEP 2/2` (and
  Robotize's `LOOP 2/2`) at col 41 — the plane rows' answer to Ptch's scale name /
  Tens's zone. Replaces the bare 4-char cue at col 60. With planes now named and
  numbered on row 0, Status_Gen's "Up/Dn=STEPS" hint is gone; status text moved
  40→41 (Steps window + lock count / ENGAGED / dbl-tap hints unchanged otherwise).
  Robotize's own row-1 "Up/Dn=LOOP" hint left as-is (out of mock scope; now
  redundant — candidate for a later trim).
- Validation: zero-warning rebuild (+136 B flash), RAM unchanged. Flash/HIL/by-ear
  pending (one hex carries cont. 4-8).
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-12 (cont. 9) — TGen screen set + the ARP row dissolves into it (rack 11→10, TGen to position 5)**

- Fifth tab-grid mock ("lets do the Trig gen this time as well and im added the arp
  to this page, figure it fit well enough"). **The ARP row dissolves into TGen's
  OPER plane**: cells 6-7 = **Arp** (the old Mode headline, prints Off/Up/Dn/U-D/Rnd)
  + **Bus** (Self / A..D) — rhythm generator and arpeggiator share one page. OPER =
  Dens Rate Dpth ·· Arp Bus Roll; STEP unchanged from cont. 8.
- Fourth cross-tenant fronting: the TGen row (GENERATOR rowkind) ORs the **ARP stack
  slot** into RowState — alive when either tenant is. **Double-tap stays the
  generator's ENGAGE⇄DISENGAGE; an armed arp keeps playing** (the Voic-clamp
  pattern: secondary halves are dial-controlled — kill via Arp cell → Off, one click
  or an encoder-push). Arp CCs write through SEQ_CC_Set → ArpSlotSync, same as the
  old row.
- **TGen moves to position 5** (mock `5/12` = position assignment). Rack now **10
  rows**: Ptch · Voic · Tens · PGen · TGen · Echo · Groove · LFO · Robotize ·
  Humanize — the entire pitch/generative cluster leads the B-row.
- Mock slip noted: set 2's plane cue read "OPER 2/2" — shipped as `STEP 2/2` (it is
  the steps face; matches the PGen mock and the generic plane2 naming).
- Validation: zero-warning rebuild (-8 B flash), RAM unchanged. Flash/HIL/by-ear
  pending (one hex carries cont. 4-9).
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-13 (cont. 10) — Grve: the full-page groove cockpit + hold-step value editing (position 6)**

- Sixth tab-grid mock — the first to fill all 8 cells: **Styl Intn Glob Sync Stps
  Lane Val Clr**, abbr Grv→Grve, row moves to position 6 (the timing/feel row leads
  the emission tail). Rack: Ptch · Voic · Tens · PGen · TGen · Grve · Echo · LFO ·
  Robo · Hum.
- **Glob = the stock TRKGRV global-groove flag ON the rack** (inverted bit of
  `seq_groove_ui_local_selection`, config-file state): with Glob on, **Styl/Intn/
  Sync edits BROADCAST to every global track** (GrooveEditMask — the whole kit
  grooves as one); a local track edits itself alone. Stock semantics, now operable
  live.
- **Stps** = the selected template's num_steps (1..16, custom-editable, presets
  read-only, push=16). **Clr** = whole-template reset via stock `SEQ_GROOVE_Clear`
  (presets refuse; num_steps back to 2, next paint re-expands).
- **Val + hold-step (user picked option 1 over an EDIT plane):** Val is the paint
  BRUSH — at the ±0 detent taps paint the legacy intensity-follow VPOS sentinel;
  off-zero they paint that literal signed offset (bipolar painting). **Hold a GP
  step + turn Val = dial that step's exact cell** (the EDIT-page hold-step idiom):
  press arms, a turn converts the hold to a value edit (release skips the toggle),
  plain release = the tap/toggle. While held the Val cell reads out the step
  (`+Int`/`-Int` for sentinels); hold+push = erase. Paint toggle moved press→release.
- **Design gap caught by the user:** the original mock had no per-step access ("i
  cant see a way to add per step values for the Lane") — options were hold-step /
  a 2nd EDIT plane (16 encoders = 16 steps, costs the identity block) / cursor+Val;
  hold-step chosen (no new plane, no mode, matches the learn-gesture grammar).
- **Broadcast×seed bug caught in self-review:** Styl's per-track engage-seed writes
  Intn through the new broadcasting GRV_INTN case → one track's 0→on would stomp
  every global track's shaped intensity with the 32 seed. Fixed: `proc_in_seed`
  flag — seeds are single-track. Also SeedRowDefaults now SKIPS UI-state kinds
  (Lane/Glob/Stps/Val + SPACER — several have cc slot 0; the untouched-guard would
  misread CC 0).
- Validation: zero-warning rebuild, RAM unchanged. Flash/HIL/by-ear pending —
  NOTE: the cont. 4-9 batch got its **by-ear GO 2026-07-13** ("Go") but HIL was
  deferred at the user's request to fold this Grve pass into the same run.
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-13 (cont. 11) — Grve hold-step refined: peek without disturbing + toggle-off shadow**

- First-touch feedback on cont. 10 ("i need a way to check what a step is set to…
  turning it back on erases the step, maybe we just persist the value that was
  there?"). Two fixes:
- **Tap-vs-PEEK split**: the toggle only fires on a QUICK release (<350 ms, the
  B-row double-tap threshold). Holding to read the Val cell then releasing changes
  nothing — inspection is free; no more sacrificial turn to protect the step.
- **Toggle-off SHADOW**: a step's crafted value survives toggle-off AND hold+push
  erase; a re-tap RESTORES it (shadow beats brush; only shadowless cells paint the
  brush). 3 lanes × 16 steps (48 B UI state, +56 B bss w/ trackers), keyed to ONE
  template — invalidated on style switch and on Clr (a cleared template must not
  resurrect old cells). Not persisted (SD/session untouched).
- Gesture matrix now: quick tap = toggle(shadow/restore) · hold = peek · hold+turn
  = exact value · hold+push = erase(shadowed).
- Validation: zero-warning rebuild. Flash/HIL/by-ear pending with cont. 10.
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-13 (cont. 12) — Humn to position 7** (page already right — Int Note Vel Len; abbr Hum→Humn; status → col 41; the FEEL pair Grve+Humn now sits together. Rack: Ptch · Voic · Tens · PGen · TGen · Grve · Humn · Echo · LFO · Robo. BUILT clean.)

**2026-07-13 (cont. 13) — Robo to position 8** (both planes already matched the mock exactly — OPER Prob/Note/Vel/Len/Oct/Skip, LOOP Cyc/Pal/Strt/Rot/Rsd/Frz; the Up/Dn=LOOP hint KEPT per the mock; status → col 41. Rack: Ptch · Voic · Tens · PGen · TGen · Grve · Humn · Robo · Echo · LFO. BUILT clean.)

**2026-07-13 (cont. 14) — Echo confirmed as-is; LFO expands to the full stock set on two named planes**

- **Echo: zero delta.** The mock matched the current page cell-for-cell (Rpt Dly Vel
  FbV Note Tick Gate; the 75%/±0/100% values = fresh-track stock CC defaults
  displayed); position 9 already held by elimination; no status line, as mocked.
- **LFO: the simplified G1.7 row grows to the FULL stock FX_LFO parameter set** on
  two dial-bank planes: **CONF** (Wave Amp Phas Step Rst 1Sht ClkD — the waveform
  palette face rides here) and **DEST** (Note/Vel/Len/CC enable flags + the extra-CC
  stream: xCC# `---`/%03d, xCC enable, Offs, PPQN decoded 1..384). First row with
  NAMED planes — new proc_row_t.p1name/.p2name (NULL = OPER/face defaults); mock's
  "DEST 1/2" slip shipped as DEST 2/2.
- **The single-select Targ router is RETIRED** (it forced EXTRA_CC_OFF and made the
  flags mutually exclusive) — the DEST flags are independent, stock semantics. New
  kinds: LFO_FLAG (bit number in the cc slot — added to the seed skip-list, cc 0 is
  not a CC index), LFO_XCC, LFO_XCC_ON (inverted bit 5; deflt on = raw 0 neutral),
  LFO_PPQN. Step/Rst now reach the stock 0..255 (the old 127 cap noted as G2
  friction is gone). Status_LFO deleted — the mock leaves row 1 right blank; the
  wave name lives in the Wave cell.
- Display honesty deltas vs the mock's value row (flagged): fresh xCC reads `on`
  (stock flag, stream silent while xCC#=---), Offs/PPQN read numbers (0 offset and
  a rate are not "off" states).
- Validation: zero-warning rebuild, RAM unchanged. Flash/HIL/by-ear pending. **The
  page-by-page rack redesign is COMPLETE — all 10 rows mocked or confirmed.**
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-13 (cont. 15) — PROC datawheel = the track walker (turn = track ±1, push = group jump)**

- User idea, endorsed: **in PROC mode the datawheel switches tracks** — the B-row is
  claimed by the rack, so track switching previously meant leaving the page. The
  wheel turns the rack into a MATRIX: focused row + plane survive the switch, so you
  sit on one processor and walk it across tracks; the GxTy identity block is the
  feedback. Costs nothing: the wheel previously just duplicated GP1's headline ride.
- **Push = group jump** (user follow-up): G1→G2→G3→G4→G1 keeping the track position
  within the group — fine/coarse navigation on one control. Datawheel-push was
  documented free real estate; now taken in PROC (still free elsewhere). Falls
  through to the FAST_ENCODERS tail like every enc press.
- Groove template shadow is style-keyed (templates are GLOBAL objects), so the
  hold-step shadow behaves correctly across track walks.
- Validation: zero-warning rebuild, RAM unchanged. Flash/HIL/by-ear pending —
  this closes the pre-HIL batch (cont. 10-15).
- Files: `seq_ui.c` (+ surface map ×4 spots, this log, §9 chronology).

**2026-07-13 (cont. 16) — gen STEPS planes show the window ACTIVITY strip**

- User ask: "if i put something in manually i could go lock it by sight then add
  more around it." The PGen/TGen STEPS planes now render a 16-cell strip (row 1
  cols 64-79, exactly the free tail after the Steps/locked text): **o** = gate on,
  unlocked ("yours to lock") · **#** = gate+locked · **-** = locked silence ·
  **.** = empty · blank = past track length. Active = the step's GATE (what
  sounds — TGen's gate layer IS its content; PGen's gated steps play the note
  layer). GP LEDs stay the pure LOCK state, 1:1 under the buttons.
- The punch-in→pin→generate loop is now sighted: play a phrase (iso keyboard /
  EDIT), open the STEP plane, lock the o cells, ENGAGE — the generator mutates
  only around the pinned phrase.
- Shared Status_Gen implementation (both key-spaces). Zero-warning rebuild, RAM
  unchanged. Closes the pre-HIL batch cont. 10-16.
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-13 (cont. 17) — gen STEPS LEDs go duo-color: triggered vs locked**

- User follow-up on the activity strip ("show the triggered steps in both the gens
  as one color, then another color for one thats locked"). The gen STEPS faces now
  drive BOTH GP LED colors: **color 1 = the window's triggered steps** (gate on —
  and visible pre-ENGAGE, so a punched-in phrase lights up before the generator
  exists), **color 2 = the locks**, both = the blend. The LCD strip stays (the
  legend with locked-silence/past-length detail).
- Plumbing: new `ui_gp_leds2` overlay on the pos-marker channel — XORed with the
  playhead (it inverts sweeping a lock), single-color hardware folds the overlay
  into channel 1. Cleared every LED pass; only the gen faces set it. Change-detect
  guard extended (prev2). PGen/TGen LED blocks merged into one duo-color block.
- Zero-warning rebuild, RAM unchanged. Closes the pre-HIL batch cont. 10-17.
- Files: `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-13 (cont. 18) — pre-ENGAGE locks: the ADOPT path**

- User ask: "could i have access to locking while the gen is disabled? set some
  stuff up thats locked, know what to expect, then enable it and add to it."
  Investigation surfaced the REAL stakes: **a fresh ENGAGE seed_loops a random
  Turing line and write_loop_to_source()s it over the track immediately** — a
  hand-punched phrase never survived a first engage at all. Pre-engage locks are
  the missing safety for the whole punch-in→pin→generate flow.
- **New `SEQ_GENERATOR_Adopt` / `TrgAdopt`**: allocate a DISENGAGED pool slot whose
  loop ADOPTS the current source (new `adopt_loop_from_source` — the inverse of
  write_loop_to_source; PITCH reads `SEQ_PAR_GetSource`, NOT the render mirror;
  TRIGGER reads the target trg layer) + re-anchors it. Nothing written, no undo
  armed (nothing destructive yet). The UI's STEPS lock tap auto-adopts when no
  slot exists; the row honestly reads occupied/disengaged after.
- **ENGAGE of an adopted slot re-adopts from the THEN-current source first** (material
  punched in between adopt and engage must not be clobbered by the stale copy),
  arms the undo net at that first destructive moment, clears the flag, then takes
  the normal re-engage path (write-back = audibly a no-op; mutation honors locks).
- **Format-safety**: `adopted` lives in a parallel CCM array (`slot_adopted[]`),
  NOT in seq_generator_t — the struct's size is frozen (gen-state V4 + SlotSet
  whole-struct copies; G3 spent the last pad byte). Cleared at Init, fresh engages,
  and SlotSet restores (a restored slot is real state).
- Validation: zero-warning rebuild (+64 B CCM, tail 1400 B). Flash/HIL/by-ear
  pending — closes the pre-HIL batch cont. 10-18. First seq_generator.c delta of
  the arc (everything prior was seq_ui-layer).
- Files: `seq_generator.c/h`, `seq_ui.c` (+ surface map, this log, §9 chronology).

**2026-07-15 — Slicer tenant act 1: chop the loop like a sample (BUILT, by-ear pending)**

- User ask: "a slicer/shuffler. something that i can use to chop up midi like a
  sample and resequence it." Scope decided via alignment: BOTH surfaces (seed-browse
  dials + painted order) and BOTH materials (drum + melodic) in act 1. Plan doc:
  `doc/plans/2026-07-15-slicer-tenant.md`.
- **Born as a render-stack processor at the TAIL** (new `SEQ_CORE_SLICE_SLOT 5`,
  `NUM_PROCESSOR_SLOTS` 5→6, `SEQ_PROCESSOR_ID_SLICE`): the pass is a buffer
  PERMUTATION — output slice i copies its par+trg step-block from source slice
  map[i]. Non-bijective mapping ⇒ reorder/stutter/reverse are ONE primitive.
  Chops what you HEAR (post Pitch/ChordMask/Arp/Tension/Limit); output mirror
  holds the chop ⇒ capture/tape/bounce faithful for free, EDIT stays source.
  Emission feel (Groove/Humanize/Echo/strum) rides on top, un-chopped.
- **Mapping construction** (deterministic, per-track-RNG keystone; new grip_hash
  zones 0x60-0x63): painted wins → seed fills (WITH replacement — repeats/drops
  are the point) → REPT replaces an engaged slice with the previous OUTPUT slice
  → REV flags in-slice reversal. STRENGTH = thermometer over hash-ranked slices;
  painted positions engage across the LOWER dial half, seeded across the upper
  (sweep = intent first, chaos second). Only the MAP hash folds in SEED — the
  engage/stutter/reverse skeleton survives seed-browsing. REPT/REV skip painted
  positions (painted = exact) and are independent dials (each 0 = off).
- **Painted order = new `SEQ_PAR_Type_SliceOrd` (25)** par layer, the Waypoint
  idiom: value 1..16 = source slice in the current 16-slice window, 0 =
  unpainted; painted ANYWHERE inside a slice counts (first non-zero byte); read
  from SOURCE (EDIT-page painting is the act-1 surface — a bespoke GP face on
  plane 2 waits for a user tab-grid mock, per the mock ritual). Excluded (with
  Waypoint) from the permute itself: control-topology layers stay pinned.
- **CCs 0xA4-0xA8** (GRID bits0..2 + bit7 bypass / SEED / STRENGTH / REPT / REV),
  all 0-neutral ⇒ V5 block persists them as-is, NO format bump (old V5 records
  hold 0s there — the write path already clamped unmapped headroom to 0).
  Bounce reset zeroes all five (GENERATION axis, the ARP reasoning: the capture
  baked the chop). `SEQ_CORE_SliceSlotSync` = the Arp sync shape, joined
  `AllSlotSync`.
- **Sweep fence**: a cross-step permute can't render sweep_window_render's
  partial window (output steps pull from OUTSIDE it) — slice-armed tracks skip
  the sweep regime, always the quiet full render (per-CC-write cost, fine at
  POC grain). Scratch = 400 B main-SRAM .bss (map 128 + rev 16 + one 256-byte
  row snapshot; render is effectively single-threaded).
- **Rack row 11 "Slic" at position 6** (between the generators and the feel trio):
  Grid (headline, off/2stp/4stp/8stp/16st; 0→on engage-seeds Seed=1 + Str=127) ·
  Seed · Str · Rept · Rev. Double-tap = config-PRESERVING bypass (GRID bit 7, the
  Voicing spirit — the chop drops in/out live), NOT the param-row reset. Status:
  slice geometry + where the painted order lives / how to get one.
- **Deferred by choice** (plan doc): CHOKE (cut lengths at slice edges), MOTION
  (per-bar re-roll — mechanism known: dirty-on-bar + seed=f(bar)), live
  slice-jump pads, per-drum scope CC pair, >16-slice painted windows, plane-2
  painted-order GP face (needs the user's mock).
- Validation: zero-warning rebuild; RAM: +128 B CCM (slot array, tail ~1.3 KB),
  +400 B main .bss scratch, ~12.7 KB main free. Flash/HIL/by-ear pending; HIL
  pins sketched in the plan doc.
- Files: `seq_core.c/h`, `seq_cc.c/h`, `seq_par.c/h`, `seq_ui.c` (+ surface map,
  this log, §9 chronology, plan doc).

**2026-07-16/17 — Slicer HIL pins + the ext-CC replay gap (fix: ExtCcNeutralExtend); baseline 278/278**

- Slicer act 1 flashed; by-ear "works!" after one display fix: the EDIT-page value
  printers (`SEQ_LCD_PrintLayerValue` + `SEQ_LCD_PrintLayerEvent`) fell to "????"
  for par type 25 — the encoder worked the whole time, every value just rendered
  as "????". SlcOr now shows the Waypoint idiom (dot = unpainted, else 1..16).
- **10 HIL pins** (`tests/apps/seq_v4/test_slicer.py`, + harness `CC.SLICE_*` /
  `LAY_CONST_A4`): true pass-through (all-zero AND armed-grid+strength-0), exact
  painted permutation across note+velocity+gate bits (mid-slice paint counts;
  SlcOr layer itself un-permuted), the strength-63 thermometer boundary (painted
  all in / seeded none), REPT=127 cascade to slice 0, REV=127 per-slice reversal,
  seeded rearrangement + byte-identical re-render, bypass-bit round-trip, GRID
  clamp, per-drum-instrument permute (trg fixture is 8 layers × 1 instrument —
  per-drum trg instruments don't exist on track_drum_init), degrade-safe ext-CC
  persistence. All hash-free except the rearrangement pin.
- **The 14 capture-family reds were NOT capture bugs** — a day-long hunt (worth
  its own postmortem): the user's jam left the slicer ARMED (Grid=1/Seed=1/
  Str=127, the engage-seed signature) and that RAM state SURVIVED every pattern
  load of V3/V4-era records, because the pattern/track ext-CC replay only wrote
  the CCs present in the record — "0xa0+ keep in-RAM values" (the documented
  old-slot voicing quirk, benign for feel dials, STRUCTURAL for the slicer).
  Every AUTOTEST fixture load carried the armed chop on the borrowed dst track;
  14 tests read correct content through a 2-step-slice shuffle (the "lost" drum
  bytes, arp 69→57, "+2 rotations" were all the slicer's deterministic map).
  Bounce/capture/SD paths were faithful throughout. Diagnosis was confounded by
  the conftest session dance (AUTOTEST ↔ the user's "Chop" session): passing
  repro scripts were accidentally measuring Chop's V5 banks.
- **Fix: `ExtCcNeutralExtend()`** (seq_file_b.c) — pattern AND track loads now
  neutral-extend the ext-CC replay to the live count (0s; strum/tilt 64) on all
  record generations V1..V4, the PhraseReadCCs contract applied to loads. A
  pattern load now genuinely clears leftover slicer/voicing state — recall
  faithfulness. Retroactively cures the "voicing persists until reboot on old
  slots" quirk. Remix-skipped tracks still deliberately keep RAM values.
- **DURABLE RULE (the general lesson): a new ext-block CC is not "0-neutral
  safe" until the READ side neutral-extends old records — the write-side clamp
  alone leaves RAM leakage through every old-record load.** The V5 bump pinned
  the write side; the read side was the missing half.
- Sharp edges found + boarded (OPEN_ITEMS): CMD_BANK_CREATE stale bank-info
  cache (loads -132 until a REAL session switch; same-name session_load
  short-circuits); harness transport-stop doesn't rewind the song position.
  AUTOTEST bank 1 was re-created + densified during recovery (test debris also
  landed in the user's Chop session bank 0 slots 60-63 — flagged to user).
- Validation: full suite **278/278 = the new baseline** (268 + 10 slicer pins)
  on the fixed firmware. `tests/log_traffic_plugin.py` added (SysEx wire-diff
  shim that cracked the case — kept as a diagnostic tool).
- Files: `seq_lcd.c`, `seq_file_b.c`, `seq_cc.c` (bounce-reset comment),
  `tests/apps/seq_v4/test_slicer.py`, `tests/harness/sysex.py`,
  `tests/log_traffic_plugin.py` (+ OPEN_ITEMS, this log, §9 chronology).

**2026-07-17 — Slicer act 2: choke + motion + jump pads (BUILT, by-ear pending)**

- User picked ALL FOUR act-1 deferred threads; three built this session, the
  plane-2 painted-order face WAITS on a user tab-grid mock (mock ritual). Plan:
  `doc/plans/2026-07-17-slicer-act2.md`.
- **CHOKE (CC 0xA9, dial "Chok")**: verified a par Length clamps at 96 ticks =
  one step — the ONLY per-step cross-edge tail is the GLIDE trg tie, so choke =
  clear the glide bit at choked slices' final output step (chain stops at the
  cut point). Per-slice thermometer (REPT/REV idiom, new grip zone 0x64) but
  painted slices are NOT skipped (choke is articulation, not order). Works at
  strength 0 (gate-tightener on the un-reordered loop). Track-mode SUSTAIN is
  emission-time — rides on top like Echo, accepted.
- **MOTION (CC 0xAA, dial "Motn")**: per-bar re-roll of the seeded fill. Value
  quarters = every 8/4/2/1 bars (0 = frozen). Folds `epoch =
  robotize_measure_ctr / N` into the SEED axis only (seed_eff = 1 + ((seed-1) +
  epoch*53) % 127; seed 0 stays identity) — skeleton + painted stay put, the
  fill re-rolls. KEY ORDERING FIND: SEQ_CORE_RenderTracks runs at the TOP of the
  tick, BEFORE the ref_step==0 hook — a plain dirty flag would render one tick
  late and smear the downbeat; the hook renders SYNCHRONOUSLY instead
  (TensionResolveBoundary spirit) so the new chop lands ON the One. Mid-epoch
  renders rebuild the same map (ctr/N constant between boundaries).
- **Jump pads (PROC_FACE_SLICE_JUMP, the Slic row's face1)**: GP9-16 punch the
  playhead to slice 1-8. NO new machinery — `SEQ_CORE_SetTrkPos` already
  latches manual_step while playing (consumed at the next step advance = a
  step-quantized jump) and sets the step directly while stopped. Active on grid
  geometry regardless of bypass (playhead, not render); pads past the play
  length inert. LEDs: playable pads lit, sounding slice winks (ROBOLOOP idiom);
  Status_Slicer row 1 right screen = the pad cells (the old verbose "order:"
  row-1 line compacted into row 0 as "SlcOr:x" / "no SlcOr").
- Rack row: n_params 5→7 (Chok, Motn); new PROC_FMT_MOTN_BARS formatter
  (off/8bar/4bar/2bar/1bar). SeedRowDefaults untouched (zero-seed params are
  skipped by construction). Double-tap bypass + engage-seed unchanged.
- Persistence: 0xA9/0xAA are in-range in the fixed-size V5 ext block; the write
  side always clamped unmapped headroom CCs to 0, so pre-act-2 V5 records read
  back as genuine 0s, and ExtCcNeutralExtend covers V1..V4 by count (0 default)
  — degrade-safe both directions, NO V6. Bounce reset zeroes both new dials
  (they sit in the existing slice_* reset block's file, added alongside).
- Files: `seq_cc.h`, `seq_cc.c`, `seq_core.c`, `seq_ui.c`,
  `MBSEQV4_CONTROL_SURFACE_MAP.md` (+ the plan doc, this log, §9 chronology).
  Compiles clean; flash/by-ear/HIL pending. Manual-fork entry follows the GO
  (act-1 precedent: the surface map documents gestures pre-GO).

**2026-07-17 (cont.) — Slicer act 2: the ORDR paint plane (first-instinct build, user refines)**

- User call: skip the author-a-mock step — "give your first instinct a try, then
  ill refine". Flash of the morning build was already done; this block adds the
  fourth thread on top.
- **The instinct: encoders paint, buttons jump, the LCD shows the RESOLVED map.**
  Plane 2 of the Slic row (Up/Dn flips CHOP⇄ORDR): 16 cells = the first window's
  output positions, each showing what it ACTUALLY plays — painted bare (` 3`),
  machine-decided in parens (`( 3)`), trailing `<` = REV'd; stutter reads as a
  repeated source. Browsing Seed/Motn animates only the parens cells — the
  painted skeleton visibly stays put (the Str thermometer's intent made visual).
- Gestures: encoder turn = set the position's source (1..S, down to 0 =
  unpaint), push = unpaint, GP button = jump-audition the position (the CHOP
  pads' SetTrkPos gesture extended to all 16). First turn with no SlcOr layer
  AUTO-ADOPTS one (first still-None par layer, zeroed first — the pre-ENGAGE
  adopt idiom; DRUM tracks are fenced to TrkEvnt: adopt writes LAY_CONST_A,
  the non-drum assignment home). LEDs duo-color (gen-STEPS idiom): color 1 =
  live positions + winking playhead, color 2 = painted.
- **Plumbing precedent: the first PURE-FACE plane 2** (face2 set, params2 NULL).
  HasPlane2 now ORs face2; SlotParams returns an EMPTY param list on such a
  plane (never a fall-through to plane 1's dials — the face owns the encoders
  via its own branch in the turn/push handlers).
- **slice_map_build now fills CALLER buffers** (render passes the old statics;
  the new `SEQ_CORE_SlicePreview` fills 16-entry UI-task locals — display can
  never race a render mid-build). Preview calls the build with S'=min(S,16),
  which IS the real first window (win=0 → same wsz; painted values are 1..16 by
  definition; REPT only looks backward). Preview strength reads tcc (not the
  slot): shows the source-side state and keeps working under bypass.
  `SEQ_CORE_SliceOrderPaint` zeroes the position's whole slice span before
  writing the canonical first step (a stale mid-slice byte from EDIT painting
  can't shadow the new value), then RenderTouched.
- Readout split: CHOP plane keeps geometry only, compacted to col 50 ("8x2" —
  col 41 is the plane cue now, 55 = BYP); the ORDR plane's row 0 carries the
  order context ("Order 8 slices of 2  SlcOr:C" / "turn adopts SlcOr").
- Files: `seq_core.c` (build refactor + SliceOrdLayerGet/SlicePreview/
  SliceOrderPaint), `seq_core.h`, `seq_ui.c` (SlotParams/HasPlane2 widening,
  PROC_FACE_SLICE_ORDER, SliceJump/SliceOrderEnc helpers, Status_Slicer plane
  split, LED branch), surface map, the mock TSV (now shows the built design —
  the user's markup canvas). Compiles clean; flash/by-ear pending — HIL pins
  for all of act 2 follow the GO.

**2026-07-17 (cont. 2) — Motn refined to a 5-detent rate selector**

- First hands-on report: "motn seems to only go to 8bar". Not a value bug —
  the 0..127 zone-quarters encoding needed 32 encoder clicks to leave a zone,
  and with no feedback until the next bar boundary it read as stuck. A 5-state
  selector on a 127-step dial was the wrong grammar; **Grid (its sibling
  headline selector) is the right idiom: one click per state.**
- CC 0xAA re-encoded: 0 = off, 1..4 = every 8/4/2/1 bars (write clamps >4 → 4,
  the GRID clamp idiom; still 0-neutral, V5 untouched). slice_motion_bars =
  8 >> (v-1); dial lo/hi 0..4; formatter = named cells off/8bar/4bar/2bar/1bar.
- Encoding-change note: any Motn value saved during today's session decodes as
  clamp(raw,4) on load — a raw 20 becomes "1bar" (4) instead of 8bar. Only
  today's test saves are affected; nothing older carries the CC.
- Files: `seq_cc.h/.c`, `seq_core.c`, `seq_ui.c`, surface map, act-2 plan.

**2026-07-17 (cont. 3) — 1stp grid: the step-grain break chopper**

- User asked "would it be pointless to add 1stp?" — no: 1-step slices are the
  classic sampler chop (one hit per position). Step-grain reorder/stutter,
  painted order = full step-level resequencing on the ORDR face. Known
  casualty: REV is inert at 1stp (a 1-step slice has no order to reverse) —
  documented, not fenced.
- **GRID values REMAPPED for clean dial order: 1..5 = 1/2/4/8/16 steps**
  (was 1..4 = 2/4/8/16; appending 5=1stp would have put the finest grid after
  the coarsest). Sessions are disposable (user's standing rule) and the act-1
  HIL pins are ours: `test_slicer.py` GRID_* constants shifted +1, the clamp
  pin now expects 5/0x85. Anything saved with the old encoding loads one grid
  notch FINER than it meant (raw byte unchanged, meaning shifted) — today's
  test sessions only.
- 256-step tracks at 1stp exceed SLICE_MAX_SLICES (128): positions past 128
  fall back to identity — accepted, real material is 16-64 steps (comment
  updated at the cap).
- Files: `seq_cc.h/.c` (clamp 5), `seq_core.c` (len = 1<<(grid-1) ×3 sites +
  header comments), `seq_ui.c` (dial hi 5, formatter "1stp", len decodes ×5),
  `tests/apps/seq_v4/test_slicer.py`, surface map. Compiles clean.

**2026-07-17 (cont. 4) — Punch-chop: "record the pads, real MPC style"**

- User ask: record the manual slice triggers, MPC-style. THE INSIGHT: a
  recorded chop performance IS a painted order — no event recording, no
  replay machinery, no phase capture. While the loop runs, an ORDR pad punch
  quantizes to the NEAREST slice boundary and paints SlcOr there; what you
  hear is the map, the map is the recording (loops forever, overdubs pass by
  pass, bare cells on the face, captures/bounces for free).
- **ORDR pads re-read as SOURCES (the MPC semantic), not positions**: punch
  pad 3 = "play slice 3 now". The bar stays LOCKED — the content moves, not
  the playhead (an MPC chop keeps the one); phase-slip performance stays on
  the CHOP plane's jump pads. Stopped transport: pad = jump into region K
  (preview). The audition-jump-while-playing on ORDR is GONE (flagged to
  user as the one veto-able change).
- Quantize feel: front half of a slice repaints the CURRENT position (its
  remaining steps re-render to K's tail — late-hit forgiveness), back half
  lands the next boundary; 1stp = next-16th quantize; loop-around punch
  lands on the One. Punches respect Str like every paint (dial sovereignty).
- Shared adopt: SliceOrdEnsureLayer extracted from the encoder path — first
  punch on a layerless track adopts too. SliceOrderPaint's position bound
  widened 16 → SLICE_MAX_SLICES (punches can land beyond the first window on
  long loops; the VALUE stays 1..16 within-window, matching the map build).
- Display truthfulness fix the feature exposed: a painted-but-GATED position
  (low Str) now shows parens (what actually sounds), not a bare number —
  the color-2 LED still marks the paint. Bare = engaged, always.
- Files: `seq_ui.c` (SlicePunch + EnsureLayer + pad branch + cell notation),
  `seq_core.c` (paint bound), surface map. Compiles clean; by-ear pending
  with the rest of act 2.

**2026-07-18 — The bouncing read head (ORDR wink = source being read)**

- User mused: "what if we moved the playhead to access the slices? so it would
  be visual? ... bouncing around, then a capture would make it linear."
  Assessment delivered: a traversal engine would be audibly identical to the
  permute (same map) but breaks mirror-=-heard (§3's core invariant, the
  reason the slicer is render-stack) and collapses the two independent play
  axes (content map × head position — the CHOP pads' slip-over-chop compound
  play). Capture-makes-it-linear already exists (the mirror IS the flattened
  chop). What the idea genuinely wanted = the VISUAL — and that is a display
  feature: we know map[current position] every tick.
- **ORDR's wink is now the READ HEAD**: map[current output position] = the
  source slice being read right now, winking full-dark (both colors) so
  painted pads bounce visibly too. The LED row jumps in map order — the
  permute's virtual playhead, honest (it IS where the sound is read from).
  Symmetry rule: wink-meaning matches pad-meaning per plane — CHOP (pads =
  positions) winks the sounding POSITION, ORDR (pads = sources) winks the
  sounding SOURCE.
- Zero cost: the LED branch already computes the preview map; the wink just
  indexes it. Beyond window 1 (long loops) the head is dark — same limit as
  the cells. A slice-grain DIRECTION mode ("WpSlice", real playhead jumps,
  phase-slip character) noted as a possible future act ONLY if the ear asks —
  it would be a second chop engine (WpHop precedent exists).
- Files: `seq_ui.c` (LED branch), surface map. Compiles clean.

**2026-07-18 (cont.) — Momentary interject: "play the slice, then jump back"**

- User ask (hedged "maybe that doesnt make any sense" — it does): trigger a
  slice, hear it, then RETURN to where the playhead was. In the permute
  architecture the return is FREE: the playhead never leaves — a momentary
  interject is a TRANSIENT map override (while set, every position reads the
  held source; clear = the loop resumes exactly in phase). No return-jump
  machinery, no phase math, no event state.
- The slicer's performance ladder completes: **slip** (CHOP pads — relocate
  the head, phase bends) → **interject** (ORDR hold — play it, come back) →
  **record** (ORDR tap — play it, keep it).
- **ORDR pad gesture is now press/tap/hold** (the Grve 350ms idiom): PRESS
  sounds source K at once (override on, no paint); QUICK release = the punch
  COMMIT (paint at the press-time quantized landing — yesterday's recording,
  commit moved press→release, audibly earlier not later since the press
  already sounds); HOLD = ride the repeat, release returns unpainted. Legato:
  a new press commits a still-quick previous pad (fast rolls don't drop
  paints). Read-head wink parks on the held pad. Stopped: preview jump
  (unchanged).
- Implementation: `seq_core_slice_hold[16]` RAM-only performance state (no
  CC, not persisted, NOT in the map hash — captures grab the interject
  as-heard via the mirror, by design), `SEQ_CORE_SliceHoldSet` re-renders on
  both edges; slice_map_build overrides m (un-reversed) while held,
  painted_out still reports the layer (skeleton stays on the color-2 LEDs);
  UI stash pad/track/target/t0 (track stashed — the datawheel can walk
  mid-hold); PROC page exit clears a live hold.
- Files: `seq_core.c/.h`, `seq_ui.c`, surface map. Compiles clean; by-ear
  pending with the whole act-2 stack.

**2026-07-18 (cont. 2) — The ext-CC travel gap (capture→switch "sounds different")**

- User report: capture to the same track / different pattern, switch to it →
  sounds different. Investigation found a GENERAL defect: `slottrk_src_cc[128]`
  and every CC copy loop in the capture/bounce/copy family stopped at 0x7F —
  **the fork ext block (0x80..0xAF: voicing + slicer dials) never travelled
  with ANY copy verb.** A deposit's ext CCs were whatever the borrowed DST
  pattern had, minus the reset's zeroing. The seq_core comment even
  acknowledged it ("the fork CCs at 0x80+ are not in the 0x00..0x7f inherit")
  — acceptable when every 0x80+ CC was generative (reset-covered), broken
  since the voicing dials (2026-07-11): they are deterministic SHAPING the
  reset deliberately PRESERVES (the seq_layer expansion contract), but the
  inherit never brought them — chord-track captures came back voiced with the
  DST slot's stale/neutral dials. The LIVING save verb (keep-gen by design,
  no reset) silently dropped the whole block — a living chopping track lost
  its chop on recall.
- **Fix: SEQ_CORE_CC_INHERIT_COUNT 0xB0** — full CC space incl. the ext block
  everywhere: new SEQ_CORE_CcInherit(dst,src) helper for the two direct
  Get→Set paths (CaptureToTrack, CaptureSpanPrepDst); slottrk_src_cc bumped
  to 0xB0 with <0-clamped snapshots (unmapped headroom 0xAB..0xAF reads -1)
  at all four snapshot sites (flatten-to-slot, chord, canvas, living) + the
  three array replays. Flatten paths still ResetGenerativeForBounce AFTER the
  inherit: generative ext CCs (chordmask/arp/slicer) zeroed, shaping (voicing)
  survives. Span path: voicing inert on its baked note material (expansion
  already in the tape) — no double-apply.
- Whether THIS was the user's audible difference depends on the track
  (chord/voicing → yes exactly; plain slicer flatten should have been
  byte-faithful — then suspect Motn epoch freeze, or the by-design
  echo/LFO strip on copies). Re-test requested on the fixed build; HIL pin
  for ext-CC travel to ride the act-2 pin pass.
- Files: `seq_core.c` (define + helper + 9 sites + comments). Compiles clean.

**2026-07-18 (cont. 3) — FTS survives the bounce reset (the copy stays in the field)**

- User confirmed the capture→switch difference involves "pitch settings
  getting dropped". Split delivered: Semi/Oct/ChordMask-Str zeroing is
  CORRECT (baked into the mirror notes — keeping them would double-apply);
  but clearing FORCE_SCALE made the frozen copy DROP OUT of the global
  harmonic field — Scle/Root/Deg/Shade performance moves kept bending every
  live track except the fresh capture. FTS is IDEMPOTENT on the baked tape
  (re-snapping in-scale notes under the same scale = no-op), so preserving it
  costs zero fidelity and restores the follow. **Principle: the capture
  freezes the track's own generation, not its membership in the global
  field** (the groove-preservation reasoning, extended to harmony).
- ResetGenerativeForBounce now preserves the FORCE_SCALE bit through the
  trkmode_flags clear (playmode still -> Normal: the transposer key is a live
  gesture, and a kept Transpose playmode would re-apply the current key on
  top of the baked pitch — not idempotent). GRAVITY grip stays zeroed for the
  same reason (a kept grip would re-bend the baked bend).
- AWAITING user re-test on the fixed build (ext-CC inherit + FTS-keep both
  in) + the discriminator: same notes right after the switch (difference only
  appears under pitch PERFORMANCE) = this was the whole story; wrong notes
  immediately = dig the legacy-pitch fences (arp playmode / drum) next.
- Files: `seq_cc.c`. Compiles clean. HIL: the capture-family baseline pins
  assert content (unchanged); an FTS-follow pin joins the act-2 pass.

**2026-07-18 (cont. 4) — Drum transpose travels through capture (the user's case, nailed)**

- User's facts closed the case: DRUM track, Semi+Oct set, FTS on — the copy
  had neither the dials NOR the pitch baked. Root cause: drum event mode is
  legacy-pitch-FENCED (the render mirror never bakes drum pitch) AND drum
  steps cannot store notes at all — gates+vel per instrument, the note lives
  in per-drum config. So on EVERY capture path drum pitch normalizes back to
  the untransposed instrument, and the reset then zeroed Semi/Oct: the only
  carrier of the kit's tuning. (The tape path bakes pitch for melodic
  material; for drums the emitted notes map back to instruments — nothing to
  bake into.)
- **Fix: the chord re-apply idiom, generalized — drum (and chord, on the
  mirror verbs that lacked it) sources re-apply the src's static Semi/Oct
  through the reset at all four flatten sites**: CaptureToTrack,
  CaptureSpanPrepDst (drum-only: the tape bakes everything else — a chord/
  melodic re-apply there would double-transpose), CaptureToSlotTrack, and the
  canvas (src_is_chord || src_is_drum; the span route's values arrive via the
  scratch snapshot, already restored by PrepDst — consistent either way).
- Rule of thumb now explicit in the comments: **where the material can't
  carry the pitch (drum steps: no notes; chord steps: indices), the config IS
  the pitch and must TRAVEL; where the material carries it (melodic mirror/
  tape), the config is BAKED and must reset.** FTS survives everywhere
  (idempotent, cont. 3).
- Files: `seq_core.c` (4 sites). Compiles clean. HIL pins owed in the act-2
  pass: drum-capture transpose travel + FTS survival + ext-CC travel.

**2026-07-18 (cont. 5) — Drum tape capture gets per-instrument write-back**

- Follow-up to cont. 4: pitch travel confirmed working by user, but "on the
  capture only one note/instrument is playing". Root cause is OLDER than
  today: **the tape write-back was melodic-mono by construction** — pass 1
  gated instrument 0 unconditionally, pass 2 last-write-wins into the single
  note/vel layer. A drum source's 16 interleaved instruments all funnelled
  into instrument 0, overwriting each other: one drum survives, the rest
  come back empty. Drum-over-tape was documented "by-ear out-of-scope" in
  the 2026-06-26 ReSim comment and never refused — it just mangled.
- The tape is a passive MIDI-out wire tap (no instrument identity in the
  package; cable = track only), so instead of plumbing instrument through
  the scheduler: **rebuild the mapping at grab time** — each drum's EXPECTED
  emitted note = lay_const row-A note through the same static emission chain
  the window heard (Semi/Oct sign-decoded per SEQ_CORE_Transpose + the FTS
  snap via SEQ_CORE_FTS_GetScaleAndRoot/SEQ_SCALE_Note). Exact match first,
  else NEAREST (absorbs limit folds / humanize-note wobble); ties -> lowest
  instrument. Assumes dials static across the window (same assumption
  melodic content capture makes).
- Drum dst write-back: gates per mapped instrument (pass 1); taped velocity
  into the kit's Vel par layer when present (else the fixed lay_const
  velocity plays — gate-only capture); NO length chains (drum gates are
  one-step). Melodic path byte-identical to before. STOPPED drum re-sim
  stays out-of-scope (mono) — the while-playing tape is the jam path.
- Files: `seq_core.c` (capture_drum_instr_for_note + expected-note table +
  both write-back passes). Compiles clean. HIL pin owed: drum tape capture
  with transpose+FTS armed -> per-instrument gates match the source pattern.

**2026-07-18 (cont. 6) — First HIL run against act 2: 275/278 → 278/278**

- Full suite vs the act-2 firmware: **275 passed, 3 failed — all three the
  SAME root**: legacy pins asserting "FORCE_SCALE must be RESET on the frozen
  copy" (test_capture_force_scale.py ×2, test_pitch_chain.py planed-groove),
  i.e. the exact contract the cont.-3 field-membership decision deliberately
  inverted. NO content assertion failed anywhere — baked notes byte-faithful
  suite-wide, shifted GRID pins + all 10 slicer pins green on the remapped
  firmware. The capture-fix batch drew zero collateral reds.
- Pins flipped to the new contract (flag must SURVIVE; docstrings updated —
  the old "immune to later key changes" rationale is now the explicitly
  unwanted behavior). Re-run: 4/4 green → **suite = 278/278 on act-2
  firmware = the working baseline** (same count, three pins now guard FTS
  SURVIVAL instead of FTS reset).
- Still owed (unchanged): act-2 behavior pins (choke/motion/jump/ORDR/punch/
  interject) + ext-CC travel + drum transpose travel + drum tape
  per-instrument pins; by-ear verdicts per thread.

**2026-07-18 (cont. 7) — Capture-fidelity determination: the two-family law, and the flatten family stops over-resetting**

- Session-long deep dive (user call: "make the captured MIDI sound exactly
  like what was captured, synth AND drum"). Read the whole engine end-to-end:
  tape tee, re-sim sink, all five deposit verbs, both resets, render stack,
  emission chain. **The determination — every capture path belongs to one of
  two families with opposite bake contracts:**
  - **FLATTEN (mirror copy)** — CaptureToSlot / CaptureToTrack /
    CaptureToSlotTrack (the STOPPED half of the pattern-capture gesture) /
    the chord-index route / ProcessorBounce. The deposit holds render-stack
    output ONLY; the emission chain never touched the bytes.
  - **TAPE (emission recording)** — live tape + stopped re-sim
    (CaptureSpanPrepDst). The deposit holds what SOUNDED: echo repeats, LFO
    pitch, groove timing, traversal, delays, rolls, Nth gating all baked in.
  - **The law: a deterministic emission shaper is PRESERVED on a flatten dst
    (re-applies identically to the un-baked bytes) and RESET on a tape dst
    (re-applying would double). Generation (randomness) resets in both.
    Render-stack dials reset in both (baked into the mirror).**
- **Shipped 1 — the reset split**: SEQ_CC_ResetGenerativeForBounce is GONE;
  SEQ_CC_ResetGenerativeForFlatten / ...ForTape wrap one common body.
  Flatten now PRESERVES: echo (7 CCs), LFO (9 CCs), FX-MIDI duplicate,
  SUSTAIN flag, SYNCH_TO_MEASURE, deterministic direction modes + progression
  params (Random_Dir/Step/D_S still -> Forward), and the
  Delay/Roll/Roll2/Nth1/Nth2/Root/Scale lane ASSIGNMENTS (values already
  travel in the full par copy — stripping them was the "frozen drums lose
  their rolls / painted micro-timing dies" hole). Probability + random
  gate/value triggers strip in both. Tape flavor byte-identical to the old
  reset. Call sites: ToSlot/ToTrack/ToSlotTrack + canvas post-tile = Flatten
  (canvas serves both routes: chord route is mirror-family; span route
  inherited scratch CCs already ForTape-reset — outcome-identical), PrepDst
  = Tape. Closes OPEN_ITEMS "copies strip Echo/LFO by design" — the ear
  raised it.
- **Shipped 2 — stream lanes reach span deposits**: the tape tee + re-sim
  sink hear NOTE events only, so CC/PitchBend/ProgramChange/Aftertouch par
  lanes arrived memset-0 (painted curve lost + a spurious value-0 event at
  the loop head). New SEQ_CORE_CaptureCopyStreamLanes: direct lane copy from
  the source's rendered MIRROR (slice permutes travel), rotated to the
  window phase (HEARD rot = (win_start/tps)%loop, GRID/re-sim rot 0). Ctrl
  lanes deliberately NOT copied — their heard effect is baked in the
  recorded notes; a copied Ctrl lane would re-modulate the frozen copy.
- **Shipped 3 — drum tape capture is pitch-faithful when the kit opts in**:
  when the dst kit carries a Note par lane (the fork's per-step drum-note
  path), the write-back now stamps the taped pitch per (step, instrument) —
  ChordMask/Tension/PitchGen drum wobble is finally capturable WHILE
  PLAYING, not only via the stopped mirror copy. The lane carries the FINAL
  emitted pitch, so PrepDst's static Semi/Oct travel is undone on these kits
  (double-apply guard); FORCE_SCALE stays (idempotent + field membership).
  Kits without a Note lane keep normalize-to-config + CC travel — the lane
  is the opt-in. Also: taped gate -> the kit's Length lane when present
  (memset-0 was clipping every captured hit to 1/96), capped at 94
  (terminating — no drum Gld chains). Attribution stays nearest-expected-
  note (tape events carry no instrument) — documented limit: a pitch push
  past a neighbor's expected note buckets to the wrong drum.
- **Shipped 4 — chord fence on the RAM grab**: SEQ_CORE_CaptureSpan refuses
  CHORD event-mode sources (-13, "chord: use Pat-cap") — chord steps store
  INDICES; the note materialize left gates+vel over an all-0 chord lane =
  silent garbage deposit. The slot verb keeps its faithful chord-index
  window route.
- **Open forks boarded, not built**: groove-on-tape (tape quantize strips
  the swing and PrepDst zeroes groove -> straightened; re-inherit would
  re-swing but negative-delay grooves already mis-bucket a step early —
  by-ear fork); drum->note-track deposit verb (rung 2 of the drum ladder — a
  re-instrument mode, new gesture surface); re-sim drum sink per-instrument
  port (stopped drum grabs still collapse to instrument 0); RAM chord-window
  path (lift the -13); drum LIMIT travel (emission fold on kits is neither
  baked nor travelled — rare combo, noted).
- HIL blast radius checked before building: NO existing pin asserts the old
  echo/LFO/direction reset state (the FTS flip was the only pinned contract)
  — zero expected reds; new pins owed for all four ships.
- Files: `seq_cc.c/h` (split + law comment), `seq_core.c` (call sites,
  helper, tape write-back, fence), `seq_ui.c` (-13 msg), `seq_layer.c`
  (comment). Compiles clean, zero warnings. Flash/by-ear/HIL pending — joins
  the act-2 validation batch.

**2026-07-18 (cont. 8) — Deterministic capture validation: pins + kit verb + the diff diagnostic**

- User call: "we need a more deterministic way to do this than my by-ears" —
  the answer is the harness we already own (SysEx testctrl reads CCs and
  par/trg bytes back), not MIOS Studio dumps. Built the owed instrumentation:
- **`test_capture_fidelity.py` — 7 pins that assert the cont.-7 law
  byte-for-byte**: flatten preserves echo/LFO/direction + Roll-lane asg while
  notes stay byte-identical; flatten still resets Random_Step (generation);
  tape resets the same shapers (the family contrast, while playing); tape AND
  stopped re-sim carry a painted CC lane (stream-lane copy, GRID rotation 0);
  drum tape bakes pitch/gates/length per instrument (Semi undone on the dst,
  source untouched, ungated cells empty); chord RAM grab refuses 0x1D.
- **New testctrl verb `CMD_TRACK_DRUM_KIT_INIT` (0x41)** — a REAL playable
  kit: par 32×2×16 (A=Note — the pitch-capture opt-in, B=Length), trg 32×1×16
  (per-drum gates via trg_byte_set's instrument arg), config notes 36+d
  (distinct -> exact-match attribution), vel 100, LENGTH 15. The old
  `track_drum_init` stays as the generator-dispatch skeleton (its trg has ONE
  instrument — per-drum capture can't be exercised on it).
- **`diag_capture_fidelity.py`** — the mid-jam instrument: snapshot src,
  capture (transport state picks the family), snapshot dst, print
  PRESERVED/RESET/CHANGED shaper CCs + lane/gate diffs with a law-check
  footer. "Sounds different" now produces a pasteable table instead of a
  hunch.
- HIL note: pins collect 7/7; they need the cont.-7 firmware FLASHED before
  the first run (the fresh project.hex includes the kit verb). Firmware
  rebuilt zero-warning with the verb.
- Files: `seq_testctrl.c` (+verb), `tests/harness/{sysex,board,__init__}.py`,
  `tests/apps/seq_v4/test_capture_fidelity.py`, `tests/diag_capture_fidelity.py`.

**2026-07-18 (cont. 9) — Capture-fidelity batch VALIDATED on hardware: suite 285/285 = new baseline**

- User flashed cont.-7+8 firmware and staged the real case (T5 kit, pattern
  A1 -> A2, dedicated bank 1). The slot-capture diff on THEIR material came
  back byte-identical on every snapshot axis (per-drum gates, Velocity lane,
  all shaper CCs) — and the kit's own tuning (Semi -2 / Oct -3, sign-nibbles
  14/13) plus FORCE_SCALE TRAVELLED, proving the cont.-4 "config IS the
  pitch" rule on user content. Note: their kit has NO Note par lane (A=Vel
  only) — the dynamic-drum-pitch story needs the lane opt-in (TrkEvnt).
- **All 7 fidelity pins green first run (16 s)**, then the FULL suite:
  **285 passed / 0 failed (9:11) = the new baseline** (278 + 7). The reset
  split (the largest semantic change: flatten now preserves echo/LFO/
  direction/lanes) drew ZERO collateral reds across the capture family.
- Remaining for this batch: the by-ear FEEL pass only (stopped freezes of
  echo/LFO/swung material should now sound identical — the preserved-dial
  behavior is new to the ear even though the bytes are pinned), plus the
  boarded forks (groove-on-tape, drum->note verb, re-sim drum sink, RAM
  chord window, drum LIMIT travel).

**2026-07-18 (cont. 10) — The user's ear was right: emitted-stream A/B finds the octave-fold clamp + kills the re-sim drum mono-collapse**

- User verdict on the validated batch: "still not correct — Save matches, the
  N-bar capture doesn't." The 285/285 suite was green because no pin covered
  the case. Built the decisive instrument instead of arguing: an EMITTED-
  STREAM A/B (host records the box's real MIDI for the source, runs the
  Capture page's exact verb, loads the deposit, records again, folds both on
  the loop grid with rotation search). On the user's T5 kit it showed:
  - PLAYING grab: drum note 11's whole pattern MERGED onto the note-9 drum
    (dst 9-row = union of src 9+11 rows; 11-row empty); 4 other drums exact.
  - STOPPED grab: everything collapsed onto instrument 0 (the boarded re-sim
    melodic-mono hole — live in the user's actual gesture).
- Attribution probe pinned the arithmetic: kit tuned Semi-2/Oct-3 (shift
  -38); drum cfg 36 -> -2. **Live emission OCTAVE-FOLDS negatives
  (SEQ_CORE_TrimNote: -2 -> 10, FTS -> 11) but the grab's expected-note table
  HARD-CLAMPED to 0** (-> FTS ~2), so note-11 events had no exact match and
  nearest-merged onto the 9-drum. One-line class of bug: any model of the
  emission chain must use the chain's own primitives.
- **Fix 1**: expected-note builder hoisted to `capture_drum_expected_notes`
  (shared) and the clamp replaced with `SEQ_CORE_TrimNote(nn, 0, 127)` — the
  exact fold emission uses.
- **Fix 2 (re-sim drum parity)**: the sink now writes PER INSTRUMENT for drum
  dsts — same expected-note mapping, gates/vel/Note-lane pitch bake (dst
  Semi/Oct zeroed when the lane exists, FORCE_SCALE stays)/measured Length
  (capped 94, no chains), open-note entries carry the instrument, per-(step,
  instr) supersede, no Gld tail marking for drums. Stopped kit grabs now
  behave like playing ones; the OPEN_ITEMS "melodic-mono re-sim" line closes.
- **Pins**: +2 in test_capture_fidelity.py (9 total): the octave-fold
  attribution regression (shift -37 kit: folded drum keeps its own row +
  folded pitch in the Note lane) and the stopped per-instrument contract
  (gates/pitch/length per drum, Semi zeroed). Collected 9/9; PENDING FLASH —
  both new pins exercise firmware-side fixes.
- Instrumentation kept: scratchpad A/B + attribution-probe scripts promoted
  conceptually into the session record; diag_capture_fidelity.py remains the
  mid-jam tool. Firmware rebuilt zero-warning.

**2026-07-18 (cont. 11) — Attribution fixes VALIDATED: suite 287/287 = new baseline; T5 A/B = MATCH both transports**

- User flashed cont.-10. All 9 fidelity pins green (21 s). The emitted-stream
  A/B on the REAL T5 kit: PLAYING grab = MATCH on all 7 emitted pitches (the
  formerly-merged note-11 drum keeps its own row); STOPPED grab = all 6 drums
  on their own gate rows, byte-identical to source (instr-0 collapse gone).
  Full suite: **287 passed / 0 failed (9:01) = the new baseline** (285 + 2
  attribution pins). Capture-fidelity thread closed except the by-ear FEEL
  pass (preserved echo/LFO/dir on stopped freezes) + the remaining forks
  (groove-on-tape, drum->note verb rung 2, RAM chord window, drum LIMIT).
- Post-flash gotcha for the log: the reboot reloaded the session with tracks
  2-14 MUTED (T5 silent -> the first A/B rerun recorded zero notes,
  "MATCH" vacuously). Unmuted T5 only via the MUTE page. When an emission
  probe reads 0 events, check muted_mask FIRST.

**2026-07-18 (cont. 12) — BY-EAR GO on the capture thread ("that got it!")**

- User confirmed by ear on the real rig: the Capture-page N-bar grab now
  sounds like the source on the T5 kit — the thread that opened as "we have
  regressions… captured MIDI should sound exactly like what was captured"
  closes GO. Full ladder held: determination (cont. 7) → deterministic
  instrumentation (cont. 8) → baseline validation (cont. 9) → the ear
  overruling a green suite → emitted-stream A/B → root causes → fixes →
  287/287 + MATCH (cont. 10-11) → by-ear GO (this).
- Still open from the thread: the flatten-family FEEL pass (stopped freezes
  of echo/LFO/direction material now KEEP those dials — listen when it comes
  up in a jam) + the boarded forks (groove-on-tape, drum→note verb rung 2,
  RAM chord window, drum LIMIT travel).
