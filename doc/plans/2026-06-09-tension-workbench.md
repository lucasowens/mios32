# Tension Workbench — plan (2026-06-09)

> **✅ EXECUTED — 2026-06-10.** Track 1 (the Workbench) is built, flashed, HIL-green
> (96/96), and by-ear GO'd (soft GO — works, refinement deferred). This plan is spent
> scaffolding; the durable record now lives in:
> - **design doc** §8 (SHIPPED banner), §9 (BUILT decisions 2026-06-10), §10 (0x96 bug
>   CLOSED + by-ear tunables soft-resolved);
> - **REFERENCE doc** §2 "Tension Workbench / GRAVITY field" (symbols, verbs, gotchas);
> - the code + the `test_tension*.py` / `test_rigs.py` HIL pins + `harness/rigs.py`.
>
> What diverged from the plan below: monotone-pull grip (zone-in-hash on push only);
> RESOLVE = per-tick glide to the downbeat (not the SEQ_GENERATOR_Tick pattern); ext-CC
> shipped as a V3 tag with the V2 count frozen; the encoder-detent insight → a bipolar
> LCD position meter. **Track 2 (pitch-chain migration) is now licensed but not built.**
> Kept (not deleted) for the working detail; safe to remove once Track 2 lands.

**Status: EXECUTED.** Original gating note (historical): direction confirmed with the
user by discussion 2026-06-09; the build was gated on the workbench GO/NO-GO **by ear,
at the workflow level** (design doc §2.7). This plan is scaffolding per the CLAUDE.md
convention — archived (above) now that it's executed into the design doc / code.

Durable homes this plan feeds: design doc §2.7 (workflow-unit principle), §3
(born-as-processors rule), §5 (the harmonic blur axis), §8 (second musical build),
§9 (decisions 2026-06-09), §10 (workbench open questions), §A1 (sibling catalog rows).

---

## 1. Why

Force-to-scale and the chord mask are **projections** — a legal pitch set and a snap
onto it. Their dials only ever make material *more correct*; there is no controlled
way to make it *more wrong*. Randomness makes things wrong but random-wrong is grey
noise, not tension. The goal ("force to tension") needs a different object: a
**ranked field** over the twelve pitch classes, with one bipolar knob moving notes
*along the gradient in either direction* — pull toward stability, push toward
structured dissonance, true pass-through at the center detent.

Per §2.7 this ships as **one playable workflow** (a cockpit page + everything that
makes the gesture loop honest), not as a sequence of isolated dials. A single-track,
single-knob slice would under-read by ear and risk a false NO-GO.

---

## 2. The model — the GRAVITY field

### 2.1 The stability ladder

Every pitch class gets a rank, computed live from masks the box already holds
(rebuild on bus-chord or scale change; 12 membership tests — trivial):

| level | set | source |
|---|---|---|
| L0 | chord root | lowest held note's PC (bass proxy, see §7) or global root |
| L1 | L0 + fifth | root+7 |
| L2 | chord tones | `SEQ_MIDI_IN_BusPCSetGet(bus)` — performable by holding a chord |
| L3 | scale tones | global scale+root; membership = PCs where `SEQ_SCALE_NoteValueGet(pc,…) == pc` (the table is a snap map, not a bitmask) |
| L4 | chromatic remainder | everything else; the **RUB** subset = L4 ∩ (chord tones ±1 semitone) |

Theory blessing: this is Lerdahl's tonal-pitch-space basin hierarchy, computed from
live performance data. **Degrades gracefully with no chord held** — L0/L3 come from
`seq_core_global_scale` / global root, so the field works solo; the bus chord is
enrichment, not a requirement (unlike the chord mask, which passes through on an
empty PC-set).

### 2.2 Zones on the bipolar knob

GRAVITY is a global s8, −64…+63, center 0 = **true pass-through** (§2.3 generalized:
bipolar dials put pass-through at the center detent). Proposed zone map — boundaries
are by-ear tunables (§10):

| zone | range | target band | feel |
|---|---|---|---|
| DRONE | −64…−49 | L0 (or L0+L1, §7) | everything becomes the ONE |
| CHORD | −48…−25 | ≤ L2 | chord-tone lock (today's mask@max becomes a *region* of this knob) |
| SCALE | −24…−1 | ≤ L3 | tidy to terrain |
| — | 0 | pass-through | the material as-is |
| LEAN | +1…+24 | in-scale non-chord, prefer chord-adjacent | sus/add color — correct tension (3rd→4th is literally sus4) |
| RUB | +25…+48 | L4 ∩ chord±1 | aimed dissonance — b9, maj7-against-root; tense notes are automatically leading tones |
| SLIP | +49…+63 | active band rotated ±1 semitone | side-slipping; coherent alien |

Pull-side bands **nest** (≤L2 ⊃ ≤L1 ⊃ L0) so the CCW sweep is monotone — each click
can only move notes further down the ladder. Push-side bands are *targets*, not
supersets; in-band notes stay put.

Two structural properties carry the music:

- **Push constrains.** It narrows the legal set to the tense subset — structured
  wrongness reads as intent; loosening reads as grey. (The §5 identity principle
  applied to harmony: disorder as a reversible transformation, not a replacement.)
- **The release gesture is a cadence.** RUB tones neighbor chord tones by semitone,
  so a fast throw through the detent performs tension→resolution regardless of
  material. The §2.3 filter-into-self-oscillation image made literal; a DJ-isolator
  hand transfers directly.

### 2.3 Mechanism (mostly existing primitives)

- **Snap:** `chord_mask_snap(note, pc_mask)` (seq_core.c) is *already general* — it
  takes any 12-bit mask. GRAVITY = compute the zone's band mask, gate per note by
  grip, snap with the existing outward search. SLIP = rotate the band mask. The new
  code is band-mask computation + the grip gate; the snap primitive ships as-is.
  (Snap *direction* preference — resolve-down vs resolve-up — is a free expressive
  parameter later; the FTS table itself encodes authored directional choices.)
- **Grip gate — deterministic, not live RNG:** decision per note =
  `hash(track, instr, step, zone) < threshold(|gravity| within zone, track GRIP)`.
  Same knob position ⇒ same notes, every render: returnable states (§1), exact HIL
  assertions, and the freeze "random shapers diverge" carve-out doesn't apply.
  Including `zone` in the hash makes the gripped set change *across* zone crossings
  (variety) but stay stable *within* a zone (returnable). The chord mask's live
  re-roll (`SEQ_RANDOM_Gen_Range`) is explicitly **not** carried forward.
- **Render integration:** new `SEQ_PROCESSOR_ID_TENSION`, dispatched beside
  `chord_mask_render_range` in both quiet and sweep paths; inherits the slot's
  `bus`, `drum_mask`, sweep-regime lookahead, and the per-tick implicit dirty that
  chord-mask-bearing tracks already get (extend the same condition). Skips 0-valued
  bytes (drum lay_const fallback idiom preserved).
- **Bounce:** nothing to do — it lives in the render stack, so the computed-output
  capture is faithful by construction. **This is also a test of the bounce model**:
  freezing a pushed groove must work with zero new bake code, or the model has a
  hole worth knowing about.

---

## 3. The cockpit (one page — the harmonic sibling of ROBOLOOP)

- **GRAVITY** — the global bipolar field knob.
- **GRIP (per track)** — how strongly each track is held by the field. This is
  *routing, not performance*: the send-amount you set so the **room** leans under
  one hand. Mirrors the FTS pattern exactly (global value, per-track opt-in) —
  independent per-track fields leaning random directions read as noise, not force.
- **RESOLVE** — button: bar-quantized ramp of GRAVITY to the detent, landing on the
  one. Tension is anticipation of resolution (Meyer/Huron) — a static dissonance
  decays into texture; resolution *timing* is most of what separates an instrument
  from an effect. **In the bundle, not deferred.** Implementation: per-tick prologue
  ramp (the `SEQ_GENERATOR_Tick` pattern), terminal value pinned to 0 exactly on the
  measure boundary.
- **SHADE** — modal-brightness ladder over the existing scale table, parallel modes
  (root fixed), one flattened degree per step:
  Lydian → Ionian → Mixolydian → Dorian → Aeolian → Phrygian → Locrian
  = scale-table indices **{15, 12, 16, 13, 17, 14, 18}** (Major 0 ≡ Ionian 12;
  Natural Minor 3 ≡ Aeolian 17). Sets `seq_core_global_scale`, so FTS tracks and the
  ladder's L3 follow together. The *terrain* GRAVITY moves across — and darkening
  under a static held chord makes the ground itself tense (the chord goes "wrong"
  against the new scale). Listen before designing that away; it may be the feature.

Physical GP allocation: decide with hardware in hand (§A3 precedent — provisional
layouts have always shifted at the panel). Sketch: GP1 enc=GRAVITY, GP2 enc=SHADE,
GP3 enc=GRIP-of-visible-track, GP8 btn=RESOLVE; lower track-select row + datawheel
for per-track GRIP editing; LCD row 1 = zone name + a 16-char grip bar.

---

## 4. Bundle contents (ships together; the workflow lies without any of them)

1. **`SEQ_PROCESSOR_ID_TENSION` processor** — §2.3 above.
2. **Global GRAVITY + per-track GRIP plumbing** — global s8 (performance state, like
   window source; see §7 for persistence question); GRIP as a track CC.
3. **Deterministic grip hash** — §2.3.
4. **RESOLVE** — bar-quantized ramp.
5. **SHADE** — brightness ladder, parallel modes.
6. **ext-CC persistence fix.** `SEQ_FILE_B_TRK_EXT_CC_LAST = 0x95`
   (seq_file_b.c:62) but the chord-mask CCs occupy **0x96–0x99**
   (`SEQ_CC_CHORDMASK_STRENGTH/BUS/DRUM_L/DRUM_H`, seq_cc.h) — *all four* reset on
   reload, wider than the §10 entry recorded (it named only 0x96). A GRAVITY-page
   GRIP CC would land at 0x9A and re-trip the same bug. Fix in-bundle: extend the
   ext-CC range to a clean boundary (e.g. 0x9F), add `SEQ_FILE_B_TRK_EXT_TAG_V3`
   with the wider count (tags are already versioned V1/V2 — the read path
   dispatches on tag, so old patterns stay loadable). Load-bearing for the
   workflow: a knob that resets on pattern recall breaks sculpt→capture→return,
   the very loop under test. Independent of the larger v3 *format* work (processor
   stack + generator posture persistence), which stays deferred.
7. **Two-minute rig recipe** (doc'd in the manual-fork doc when shipped + an HIL
   fixture): chord track in chord event-mode → Bus port; one drum track with 2–3
   pitched lines (the shipped counterpoint rig — the right material, a field is most
   audible when voices push against each other); optionally one normal-track mono
   line; GRIPs on; FTS **off** on gripped tracks (POC rule, §6). Setup friction is
   what kills ear-testing.
8. **HIL tests** (deterministic hash ⇒ exact assertions, via existing
   `CMD_TRACK_PAR_GET/SET` + `CMD_GLOBAL_SCALE_SET`; likely one new testctrl verb
   for GRAVITY/GRIP/zone set):
   - band masks per known (chord, scale) at each zone — pure-function pinning;
   - detent ⇒ byte-identical pass-through (§2.3 contract);
   - pull monotonicity (CCW never raises a note's ladder level);
   - grip stability within a zone across re-renders; change across zones;
   - freeze-faithfulness: bounce a pushed groove ⇒ pinned bytes, **zero new bake
     code** (the `test_capture_force_scale` shape, inverted expectation);
   - RESOLVE lands exactly 0 on the measure boundary;
   - ext-CC round-trip: chord-mask 0x96–0x99 + GRIP survive save/reload (pins the
     widened range);
   - SHADE index ↔ `seq_core_global_scale` mapping.
9. **Listen protocol** — §8 below.

**Explicitly out of the bundle** (named so they don't creep): inter-voice interval
compression; register-spread (the tension dial for sample-mapped destinations, where
PC-tension is meaningless but range/constraint still read as intensity); the
rhythmic/metric-weight sibling; voice-leading-aware snapping (ARP_SORTED gives the
ordered view when wanted); per-voice normal-track polyphony; TENSE-hit (jump to a
stored push position — RESOLVE's attack twin).

---

## 5. Sequencing — two tracks

**Track 1 — the Workbench** (days): items 1–9, ending at the cockpit page and a
workflow-level GO/NO-GO by ear.

**Track 2 — pitch-chain migration** (gated on Track 1 GO; this is the licensed
"major rewrite that sets up future features"):

- Migrate **transpose → force-scale → limit** from emission
  (`SEQ_CORE_Transpose` / the §9 deterministic chain) into the render stack
  **together** — they are entangled: snapping *after* transpose is what keeps
  bus-planing in-scale, so moving FTS alone changes semantics. Stack re-renders on
  transposer-stack change via the same implicit-dirty mechanism chord-mask uses for
  chord changes.
- TRKMODE/FTS UX preserved via the phase-C slot-sync bridge pattern (tcc stays the
  persistent truth; slots mirror).
- **Deletes `SEQ_CORE_BakeForceScale`** and ends the per-effect bake program by
  construction — bounce correctness becomes a property of the architecture instead
  of accumulating archaeology. Stack ordering goes explicit (§9 already wanted
  chord-mask-vs-FTS ordering to be a musical choice, not incidental).
- Regression net: the existing capture/force-scale/groove HIL tests pin behavior
  parity; add a planing test (bus transpose stays in-scale post-migration).
- Known edges to investigate (§10): echo (emission-scheduled repeats), the live/jam
  input path, per-step Root/Scale par-layer overrides.

---

## 6. POC interaction rule (Track 1 only)

Gravity and emission-time FORCE_SCALE must not both own a track's pitch — the
emission snap would re-correct every RUB/SLIP note and neuter the push (live *and*
bounce consistently, but neutered). Rule: **FTS off on gripped tracks**; gravity's
pull side contains scale-snap anyway. Track 2 dissolves the conflict properly.

---

## 7. Sub-decisions at build (recommendations attached)

- **Chord root for L0:** recommended = lowest held note's PC via the ARP_SORTED
  notestack (free pitch-sorted view; bass proxy). Consequence: inversions *move the
  gravity floor* — performable. Fallback to global root when no chord held.
- **Pull floor:** root-only vs root+fifth at full CCW. Recommend root+fifth zone
  then root-only at the extreme; by ear.
- **One knob vs knob+grip-macro:** grip is per-track routing (set-and-forget), so
  GRAVITY magnitude drives both depth and threshold ramp v1; split only if the ear
  asks for "few notes very tense" vs "all notes slightly tense" as separate moves.
- **SHADE family:** parallel modes recommended (brightness move, root fixed);
  relative variant (rotating root) is a different instrument — defer.
- **Global GRAVITY persistence:** performance state (not pattern state), like
  window source (§3 composition rules). Revisit only if recall feels wrong.
- **Zone boundaries/counts:** the table in §2.2 is a starting sketch; tune at the
  panel. Collapsing LEAN into RUB, or widening the detent into a dead-zone, are
  both legitimate by-ear outcomes.

---

## 8. Listen protocol (the GO/NO-GO)

1. Does full pull feel **inevitable** (drone collapse, not dropout)?
2. Does LEAN read as *harmony* (sus/add color), not error?
3. Does the detent feel like **home** — bypass you can trust mid-phrase?
4. Does a fast push → RESOLVE land as a **cadence on the one**?
5. Does the **room lean** when three tracks share the knob at different grips?
6. Does a banked tense variation (PATTERN-hold bounce), recalled cold, still feel
   like **the same place**? (Determinism + persistence working together.)

Any structural failure here is stop-and-redesign (§A4 spirit), not tune-and-retry;
boundary-tuning failures are just §7 dials.

---

## 9. Budget notes

Negligible state: band masks are 2 bytes × ~6 zones computed on demand or cached
per (chord, scale) — bytes, not KB; GRAVITY/SHADE/ramp are single globals; GRIP is
one CC per track (in tcc, persisted via the widened ext range). No new CCM
allocations; flash-only growth. No new RAM pressure against §A5.
