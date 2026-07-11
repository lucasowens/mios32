# Design-doc deep review & refine — working scaffold (2026-06-20)

A challenge-the-model review of `doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md` as it stands
today (3315 lines, ~33k words). Method: 8 independent adversarial lenses → synthesis
(dedup + down-rank doc-already-addressed) → completeness critic → 2 empirical claims
verified against the HEAD elf/source. Build discipline set aside — this critiques the
**model and the document**, not listen-test gates.

This file is the durable capture so the review isn't lost to context. Threads get
executed into the design doc / REFERENCE, then this is archived (plan-storage convention).

---

## 0. Verified facts (not opinion — checked against source this session)

- **Main-RAM budget is wrong by ~24 KB.** §A5 (L3148) and §10 (L2926) say main RAM is
  ~94.5 KB used / ~33 KB free. The HEAD elf (`project_build/project.elf`, built 21:03):
  `__ram_start 0x20000000`, `__ram_end 0x2001dbc0`, `_estack 0x20020000` →
  **118.94 KB used / 9.06 KB free** (`.bss` alone = 120,384 B). The free region is *also*
  where the MSP stack grows down from `_estack`, so effective static headroom is < 9 KB.
  - **CCM figure is correct:** `__ram_end_ccm 0x1000dda8` → 55.4 KB used / **8.6 KB free**.
  - **Consequence:** BOTH regions are ~9 KB free. The doc's "~40 KB of CPU-only main
    buffers relocatable into CCM" lever (§10 L2700, §A5 L3181/L3297) **cannot fire** —
    CCM has 8.6 KB to receive. Every "does it fit" verdict and the morph "two organisms
    resident" sizing (§10 L2795 cites ~40 KB, L2913 cites ~33 KB) are off by 3–4×
    (conclusions still hold, but the cited margins are fiction). Real levers are only the
    gated ones: gen pool 64→16–32 (~6 KB CCM back), render single-buffer (20 KB CCM back,
    loses lock-free), track slots 16→8.

- **testctrl command-byte namespace: stale "last free byte" claim, no authoritative
  free-list.** 63 `CMD_` defs. The high testctrl block **0x49→0x7f is packed solid (no
  gaps)**; §9 L1948's "CMD_PHRASE_META 0x7f — the LAST free 7-bit command byte" is now
  stale (0x7f allocated). NOT exhausted — ~60 bytes free in low holes (0x05–0x0f,
  0x12–0x1f, 0x21–0x2f, 0x33–0x3f, 0x41–0x48). But the SET sketch's "0x02–0x0f free"
  (L2454) is partly wrong (0x02/0x03/0x04 are LCD/CHARSET/LED). Every queued build needs
  new HIL verbs → wants a one-line authoritative free-range ledger in REFERENCE.

- **Determinism keystone + platform anchors verify in source** (source-check lens):
  per-slot xorshift minted at ENGAGE, `grip_hash` zone 0x20 shared CHORD_MASK/TENSION,
  per-track `random_traverse_state` save/restored around SCRUB, re-sim frame
  self-contained; `SEQ_PAR_MAX_BYTES=1024`/`SEQ_TRG_MAX_BYTES=256`, two-round same-tick
  loop, `link_par_layer_note` drum unlock. The determinism spine is real.

---

## 1. What the doc gets right (protect these in any refine)

- **The Buffer / Generator / Processor noun trichotomy survived contact with the build.**
  Born-as-processors killed the bake program (FORCE_SCALE migrated, bake deleted). The
  nouns aged best — protect them.
- **The dated-reversal-with-reconciliation discipline** is the strongest hygiene feature
  where applied (the 2026-06-20 delete-the-tape vs tape-came-back reconciliation; the
  keystone entry capturing a bug it would have shipped). Keep verbatim.
- **The §5.6 concept map / clarity audit** is a genuinely valuable artifact — correctly
  diagnoses surface proliferation as the felt fragility. Extend it, don't break it.
- **The content-vs-timeline joint** (BOUNCE freezes state-at-an-instant; the tape freezes
  behavior-over-time) carves at a real architectural seam — keep as the PERMANENT framing.
- **The CAPTURE lineage is the cleanest live proof of the spine's recursion thesis**
  (frozen MIDI stays structured + re-processable). Bank it as proof, then turn to return.

---

## 2. Load-bearing challenges (the design conversation)

### C1 — Center of gravity has drifted from RETURN to CAPTURE; the discipline was amended to permit it
§1's two non-negotiables are **repeatable states** and **identifiable return**. The last
five cycles (per-track-RNG keystone → capture ring → re-sim → live tape → precise gate,
2026-06-19/20) all made **capture more byte-faithful**; none made a **return feel more
identifiable**. Capture has crisp engineering sub-problems; return can only be judged by
ear — so the project is flowing toward the tractable. And the rule meant to restrain that
(§2.2 "prove by ear before infrastructure", §2.6 forcing-function warning) was *edited the
same day* the keystone shipped (CLAUDE.md §2 → "license infra to the degree it makes
sense") — the keystone being pure determinism plumbing that makes no new sound (its own
bar: "reproducible AND musical, NOT bit-identical" = "didn't break what existed"), committed
with by-ear PENDING. Plus SET / unified-undo / trigger-gens / self-bus are fully spec'd,
mechanism-verified, and UNHEARD — yet sit in §9/§10 (committed spine), not behind the Part
II quarantine §2.6 invented for exactly this.
- **The concrete return-feel target already exists, filed as "parked":** the **~1.3 s
  recall freeze** (§10 L2902 — ~290 ms SD save × dirty groups; generator wander dirties
  all 4). It's the most user-visible unsolved problem in the doc, it directly breaks
  "discovery WITH the listener" on stage, and it's a RETURN defect. The DRIFT-gated
  writeback fix was tried then reverted; no cure chosen.
- **Recommendation:** make the next cycle a return-feel workflow — fix the recall freeze
  (incremental-save, or re-try the DRIFT-gate now per-track-RNG defines "dirty" cleanly,
  or accept-and-document). State plainly what §2.2 still *forbids* post-amendment (honest
  version: "infra-first licensed when it's the unlock for a named by-ear workflow on the
  critical path"). Move the 4 unheard systems behind the Part II fence or say what proved
  them.
- **Sharp Q:** Is the next build capture #6, or return-feel? And when you amended §2, did
  you change what you'd *build* or only what you'd let yourself call compliant?

### C2 — FREEZE names two opposite operations, asserted canonical 40 lines apart
FREEZE = (1) the global **master mutation switch** (METRONOME button — holds the living
wander, reversible, commits nothing; §3 L261, §5.6 L468, shipped) AND (2) the proposed name
for the **unified retroactive bounce verb** (commits heard output to dead static material,
irreversible, destination-targeted; §9 L2126). Opposite acts. The text is self-refuting:
§9 L2072 states the law as "FREEZE means one thing (hold the wander) and BOUNCE means one
thing (freeze the heard result)" — then L2110–2136 names the heard-result-freezing verb
FREEZE and concludes "FREEZE = BOUNCE = one verb." Latest commit (f9f8c5f6) drives it
deeper. §5.6 overlap #5 names "CAPTURE and BOUNCE are one verb" but never notices it gave
the unified verb the name of its opposite. This is the one term collision §5.6 misses.
- **Recommendation:** FREEZE = the wander gate ONLY (shipped, physical button, intuitive).
  The unified retroactive verb = **BOUNCE** (the doc's own conclusion is "bounce is the
  lossless foundation; capture is the weaker base" — so calling it FREEZE buries that).
  Doc-wide rename "FREEZE → [track,pattern]" ⇒ "retroactive BOUNCE → [track,pattern]"; undo
  the f9f8c5f6 "unified FREEZE" naming. The ring/tape is the BUFFER bounce reads from.

### C3 — The gesture/play surface has never been laid flat the way the concept surface was (critic's load-bearing find)
§5.6 builds a beautiful 14-row CONCEPT map and declares "the spine earns its keep — surface
consolidations only." But the surface it audits is the named-CONCEPT surface, never the
PHYSICAL-GESTURE surface the hands touch — and §10 L2711 admits "the recon did NOT examine
the UI/LED/encoder layer." Pulled together:
- **SELECT alone is the modifier for 5 unrelated held-gestures:** SELECT-hold+source = THE
  PULL; SELECT+BOOKMARK = CHECKPOINT/REVERT; SELECT+CLEAR = track undo; SELECT+GP1 =
  generator DISENGAGE; SELECT+tap = morph-arm. Disambiguated only by the other hand + the
  current view.
- **tap/hold polarity is inconsistent:** PHRASE hold = capture/**commit**; CHECKPOINT hold
  = REVERT/**destroy-back-to-anchor**. "Hold" means opposite things.
- **FREEZE is a hidden mode that silently redefines recall:** posture-regen vs frozen-tape
  depends on whether FREEZE is up/down during recall (§5.6 L463). The player must track
  "is FREEZE up or down right now, and what will my next recall therefore do?" live, in
  the dark.
- **Why it matters:** §5.6's felt-fragility thesis most plausibly lives HERE (the hands),
  not in the noun count. A clean 14-noun map can still be unplayable if the verbs collide
  on hardware. The doc has no gesture table, no modifier-ownership map, no LED-state
  inventory — the one surface that decides playability is the one never laid flat.
- **Recommendation:** build a gesture-vocabulary table (gesture | modifier | view-scope |
  what it does | LED feedback | stale-disarm) parallel to §5.6's concept map; audit
  modifier ownership and tap/hold polarity for a consistent grammar.

### C4 — "Two nouns, one verb, one control" is an ENGINE claim sold as an INSTRUMENT claim
- The spine slogan (§3) is true of the render/commit engine but the **instrument** carries
  ENGAGE/DISENGAGE, ROLL, SNAP, FREEZE, CHECKPOINT/REVERT, UNDO/REDO, SAVE-SET/RELOAD-SET,
  recall, windowing reads as first-class verbs. "One control (Destination)" covers only the
  **deposit** path — the bulk of performance is now **source/recall selection** (THE PULL,
  PHRASES, CAPTURE-aiming, windowing) the spine never named. §9 even pitches a *different*
  count ("four nouns: organism/tape/anchor/waypoint") with no bridge to §3's two/three.
- **"Robotize is the lone emission-time exception"** is asserted 3×, but §3 L195 itself
  lists groove, humanize/LFO, echo + three fenced pitch cases, and §10 L2689 adds
  probability/random-gate/random-direction — ~8–10 emission citizens. "Lone" is true only
  if "exception" silently means "transform we intend to migrate."
- **Recommendation:** keep the noun trichotomy (it earned it). Reframe slogan as "one
  COMMIT verb"; enumerate the live-control verb set and write down N. Widen the spine to
  name the source/recall control beside Destination (§5.6's map IS the missing source-half).
  Retire "lone exception" → "robotize is the only intended-CONTENT transform still at
  emission" + a named class of PERMANENT emission residents (live keys, coin-flips, groove,
  echo).

### C5 — Re-sim reproducibility (the stopped-capture correctness claim) rests on 3 preconditions filed as a parenthetical
Retroactive CAPTURE-by-re-sim's whole value is "rewind K bars, re-drive WITH wander,
reproduce the span." Three correctness preconditions sit in a parenthetical at §10
L2606-2607: (a) **cross-generator per-tick ordering** must match live vs re-sim (per-slot
seeds make a single stream order-independent, but say nothing about multi-track/gen
processing order or shared global state one writes another reads); (b) **polymetric bar
alignment** — the ring is indexed by global `robotize_measure_ctr`, but a len-12 and a
len-16 track cross bars at different times, so "rewind to window-start frame" has no single
grid; (c) **global-RNG state** — the DEFERRED emission coin-flips still draw global
`jsw_rand` during re-sim, so a re-driven span advances the global stream by a coin-flip-
count-dependent amount. The doc's "restore byte-identical" (L2624) is about LIVE state
AFTER re-sim, NOT about re-sim REPRODUCING the original — two different invariants conflated.
- **Bigger reframe:** the live **tape** (records what sounded; "strictly more faithful", no
  determinism needed; shipped) is the primary capture path. Re-sim/determinism only earns
  its complexity for a span **never performed** (stopped transport) — a real but narrow
  niche, much narrower than "determinism is THE keystone." That framing is now stale.
- **Recommendation:** promote the 3 preconditions to an explicit "re-sim reproduction
  preconditions" list with a stated answer each (+ add config-grain self-route ordering /
  A7 one-tick lag — same risk class). Reframe: tape = primary; re-sim = stopped-span
  fallback.

### C6 — The shared dirty/writeback mask is the design's single point of failure, documented only as a "locus" (critic)
§5.6 L503 correctly names "the one shared dirty/writeback mask" as "the fragility locus" +
DRIFT as its refinement — but that's a diagnosis, not a risk assessment. EVERY return-family
op rides it: FEARLESS auto-writeback, CAPTURE, PHRASE recall/capture, CHECKPOINT/REVERT, THE
PULL undo, queued unified UNDO/REDO, queued SET (which L2057 explicitly rejects an overlay
scheme to "avoid adding logic to the fragile shared dirty plumbing"). C1's recall-freeze and
"return never improved" both trace HERE: the freeze IS dirty-groups × SD-save, the shared-
mask coupling firing. §1's two non-negotiables both depend entirely on this primitive being
correct, with no isolation, no per-feature dirty accounting, no failure-mode entry.
- **Recommendation:** treat the mask as a SPOF with a containment plan, not a locus. The
  recall-freeze is the first visible crack — fix it as "decouple/isolate the mask," not as
  an isolated timing bug.

---

## 3. Document-craft / polish checklist ("refine into something more polished")

Boundary issue (critic): CLAUDE.md says this doc owns the **model**; REFERENCE owns
**codebase facts**. §9/§10 have annexed REFERENCE's charter (function names, RAM offsets,
FatFs internals, command bytes, HIL test filenames) — which is *why* the RAM budget went
stale HERE instead of being re-verified THERE. The fix is structural ownership, not a
one-time prune.

- [ ] **Correct §A5 L3148 + §10 L2926** main-RAM to ~119 KB used / ~9 KB free; show the
      arithmetic (origins + `__ram_end`/`_estack`). Keep CCM (verified correct).
- [ ] **Retire the "~40 KB relocation lever" framing** (§10 L2700, §A5 L3181/L3297) — both
      regions ~9 KB; only the gated levers are real. Add a CCM running-balance for the queue.
- [ ] **Mark §A5 "Does it fit?" (L3273-3303) + §8 gating line (L2167) HISTORICAL** — they
      still assert "64 KB CCM virgin / ~56 KB relocatable / ~32 KB free", contradicting the
      section's own 2026-06-19 opening.
- [ ] **Update morph-fit numbers** (§10 L2795 "~40 KB", L2913 "~33 KB") to ~9 KB.
- [ ] **Prune ~70 lines of CLOSED bullets from §10** (rationale → REFERENCE); §10 should be
      live forks only. Add a "STILL OPEN — blocking-vs-someday" header so the recall-freeze
      rises above format-bump/tape-storage items.
- [ ] **Collapse SHIPPED §10 entries** (phrase-morph L2768, keystone L2717, SWITCH-QUANTIZE
      L2866, Tension Workbench L2252) to their open-follow-on residue; narrative → §9/REFERENCE.
- [ ] **Close the 2026-06-11 "tape storage fork" (L2352)** as moot-by-supersession (the ring/
      tape shipped main-SRAM-only, no persistence — silently answered).
- [ ] **Add a ~20-row STATUS DASHBOARD after §3:** feature | shipped/queued/deferred |
      section-pointer. The single thing reread cold (MEMORY.md became the de-facto one).
- [ ] **Bring §11 glossary current** — Bounce is now retroactive; add ring, tape, CAPTURE,
      FREEZE, CHECKPOINT, REVERT, SET, PHRASES, THE PULL, DRIFT. **Fold §5.6's 14-row table
      into §11** so there's ONE canonical definition surface.
- [ ] **Sweep stale Part-I prose** (back-annotation lag): §3 L233-234 ENGAGE auto-jump
      (BUILT + WITHDRAWN F.3, function deleted → cursor-wins); §6 L522 "state = group = 4
      tracks" (split "platform fact" from "performer model: organism-primary" — own glossary
      L2953 contradicts it); §6 save lead "switching loses changes" → auto-writeback ledger.
- [ ] **Part II §A3 asserts a DELETED feature as shipped UI** (L3102-3106 "ENGAGE auto-jump
      ... both halves now present in the UI") and describes a GP-encoder-page generator UI
      superseded by held-modifier gestures (UTILITY-CAPTURE, SELECT-combos, PHRASE-view).
      Quarantine fence failing as a stale RECORD. Fix or mark superseded.
- [ ] **Mark "division of labor: BOUNCE=content / CAPTURE=timeline"** (§10 L2591, §9 L2086)
      superseded by the 2026-06-20 "convergence, not duplication" correction.
- [ ] **Resolve "morph = 3 (now ~5) things"** (§5.6 #3 gate lifted — phrase-morph shipped):
      WINDOW keeps its name; "recorded-state morph" folds into WINDOW; "live posture morph"
      → CROSSFADE-TWO-GROUPS; only phrase-morph keeps "morph". Same for "anchor" (SNAP
      target vs CHECKPOINT) — disambiguate.
- [ ] **Promote seed-vs-hash policy + measure-axis requirement** (§10 L2727) to a first-class
      clause of the generative law in §3 — the rule a future trigger-gen/self-bus builder most
      needs (the doc nearly shipped its violation: the dead-static bug).
- [ ] **Re-scope "Part I stays small on purpose" (L14-15) to §1-§7 explicitly**, or split the
      §9/§10 log out from the durable model. (§9 = 1297 lines, §10 = 778 → 63% of the doc;
      §1-§7 is a ~650-line shell around a 2075-line log.)
- [ ] **Adopt a standing rule:** when a §9 entry reverses Part-I prose, EDIT the prose in the
      same pass; codebase facts in §9 entries become POINTERS to REFERENCE.
- [ ] **Add an authoritative testctrl free-command-byte range** to REFERENCE (0x05-0x0f,
      0x12-0x1f, 0x21-0x2f, 0x33-0x3f, 0x41-0x48; 0x49-0x7f FULL). Fix L1948 "last free byte".

---

## 4. Suggested sequencing (proposal — user directs)

1. **Align on C1 (trajectory).** Everything else is downstream of "capture #6 vs return-
   feel." If return-feel, the recall-freeze (C6) is the first concrete build.
2. **Cheap high-clarity model fixes:** C2 (FREEZE rename) + C4 (reframe the slogan, widen
   the spine, retire "lone exception"). Pure naming/framing, sound spine.
3. **Lay the play surface flat:** C3 gesture table + tap/hold polarity audit.
4. **Pin C5** re-sim preconditions (or accept tape-as-primary and downgrade re-sim).
5. **Polish pass** (§3 checklist) — do the verified RAM fix + stale-prose sweep + dashboard
   first; they're mechanical and high-value.
