# MBSEQ V4 — Generative Platform Design

Living design document for turning this MBSEQ V4(+) fork from a trigger sequencer
with generative features into a **live generative-musical instrument** — a control
plane for sculpting, capturing, and reshaping MIDI in performance.

This doc owns the *model and decisions*. Memory keeps preferences and in-progress
mental state; the [reference catalog](MBSEQV4_REFERENCE.md) owns derived facts about
the existing codebase. When a decision changes, **update this doc** — especially
§9 (Decisions log) and §10 (Open questions), which are the multi-session spine.

**This document is in two parts.**

- **Part I (§1–§11) — Committed spine.** The validated-or-cheap-to-validate core.
  This is what the instrument *is*. It stays small on purpose.
- **Part II (§A1–§A5) — Design-ahead reference (PROVISIONAL, NOT COMMITTED).**
  Worked out in an exploratory session ahead of any build. Preserved because it
  surfaced real requirements (the v3 format need, per-class stacking, the budget
  ceiling) — but it is **unvalidated**, its numbers are **provisional**, and per
  §2.2/§2.6 **none of it is built until the first musical build (§8) has proven the
  core by ear.** Treat Part II as a map of where we *might* go, not a plan of record.

Status: design phase. POC code exists and is **disposable** (see §7). Nothing here
is committed to firmware yet except the parts already shipped, which are flagged.

Hardware target: midiphy MBSEQ V4+ build, MBHP_CORE_STM32F4 (STM32F407: 128KB main
SRAM + 64KB CCM; see §A5).

Sections:
1. North star — what we're building and why
2. Design principles (the discipline)
3. The spine — core architecture (two nouns, one verb, one control)
4. MIDI-as-sample mental model
5. State, capture, and morphing
6. Platform realities (verified hardware constraints)
7. Current fork state (POC inventory: reusable vs disposable)
8. The first real musical build (music-first build order)
9. Decisions log (what we decided and why)
10. Open questions (unresolved forks)
11. Glossary
--- Part II: design-ahead reference (provisional) ---
A1. Processor catalog
A2. Render-cache mechanism
A3. Generator UI page layout
A4. HIL test plan
A5. RAM budget (corrected)

---

## 1. North star — what we're building and why

The goal is **live psychedelic/experimental sculpting — discovery *with* the listener.**
Be the pilot on a journey the passengers experience in real time. The destination
is not known in advance; discovery and sculpture *are* the pursuit. (~30 years
playing live techno; this builds on the live-modular-patching ethos: risky, not
always pretty, but expressive and dimensional.)

The pilot loop: **sculpt to an interesting place → capture it → travel off → return or
divert → capture → repeat.** Revisit states, take them new directions, blend.

Two qualities are non-negotiable and shape everything:

- **Repeatable states.** It must not feel random all the time. States are real,
  namable, returnable objects — not "wherever the knobs happen to be."
- **Falls apart and comes back into focus — unexpected yet *identifiable*.** The
  return is recognizable, not a coincidence. This is the hard requirement; see §5.

MBSEQ is the **control plane for the whole rig**, not a feature dump. The point is
to build the thing other platforms (including the Hapax we own and love) are
missing: genuine *musicality* — pitch and harmony as performable dimensions — not
a better trigger sequencer. This is a craft/self-expression project on a platform
with deep personal history; that *reorders priorities toward the differentiated
musical core*, away from re-implementing what a Hapax already does.

No timelines. One-man project, own pace, free to change course. That freedom is
real — and it is exactly why the discipline in §2 matters more here, not less:
nothing external forces the ear back into the loop, so the design must.

---

## 2. Design principles (the discipline)

1. **POC is disposable.** Existing generative code is a *finding*, not a
   foundation. Nothing new must be backward-compatible with it. This is a license,
   not a loss — the ride is part of the enjoyment.

2. **Live-evaluable only.** The thing being tested is *playability of the process* —
   what it feels like to steer at tempo, where surprises come from, whether grabbing
   the wheel mid-phrase produces something or fights you. A rule that reads correctly
   in a buffer can feel dead under the hands. Prove musical ideas **live**, not on
   paper, and **before** building the infrastructure that makes them performant. The
   build order in §8 enforces this.

3. **Constraints are *materials*, not guardrails.** The harmonic rules (mask,
   voice-leading) must be *performable across their full range, including off*.
   Dissonance must be reachable on purpose. A mask that's always "correct" is the
   death of the aesthetic — tasteful wallpaper. Model it like a filter cutoff you
   sweep into self-oscillation: the interesting place is in motion, between the
   extremes, under the thumb. The edge *is* the dimension.
   *(Refined 2026-06-09: **bipolar dials put the pass-through at a center detent**,
   with opposed forces on either side — "must include off" generalizes to "the rest
   position is the effect silent." First instance: the GRAVITY field, §8 second
   build.)*

4. **Work with the hardware grain.** Shape the model around what the box does well
   (§6), not against it. Group-granular states, drum-vs-normal track tradeoffs, and
   the bus model are facts to exploit, not fight.

5. **Trust TK's code; lean on the harness.** The Python HIL test harness (§7) is
   what makes aggressive surgery on timing-sensitive paths *safe to attempt*. It is
   load-bearing for everything that touches the tick loop.

6. **Forcing-function awareness.** Build toward *music*, not capability for its own
   sake. The gravitational pull is real and it operates at *every* layer — code,
   and also **design.** (Case in point: a single high-capacity session produced a
   full processor catalog, a three-tier render-cache, a v3 format, conditional
   triggers, and an 8-test harness — all before the first pitched note made a sound.
   That work is preserved in Part II precisely because it must not masquerade as the
   committed plan.) The rule: the next thing built is a *sound you can hear*, not the
   next piece of capability. When in doubt, build less and listen sooner.

7. **The unit of validation is a workflow, not a feature.** *(Added 2026-06-09.)*
   A dial without its companion gestures **under-reads by ear** — a false NO-GO from
   an incomplete rig costs as much as unheard infrastructure. "Build less" means the
   smallest *playable loop*, not the smallest diff: every build ends at a surface that
   can be set up in ~two minutes, swept, released, captured, and returned to; GO/NO-GO
   gates sit at the **workflow level**. This was latent in the log before it was
   named: phases G/H shipped as one bundle on listen-test demand; the F.3 auto-jump
   survived feature-level review and fell only to whole-workflow play; the §8 step-3
   GO was itself a workflow (author → hear → judge). Corollary, refining #6:
   **infrastructure is licensed exactly when it sits on the critical path of a
   bundle** — design-ahead is "masquerading" only when no bundle needs it.

---

## 3. The spine — core architecture (two nouns, one verb, one control)

The whole instrument reduces to **two nouns, one verb, and one control**. Everything
discussed — masks, voice-leading, windowing, capture-reprocess loops — is one of
these. This is the canonical model; simpler than any source/transform-graph framing.

### Buffer (noun)
A buffer holds **committed MIDI events**. Unlike audio, frozen MIDI stays
*structured and transparent* — every event keeps its identity (pitch, velocity,
timing, length individually addressable forever). The canvas. In MBSEQ terms a
buffer is realized in track trigger/parameter layers (and captures).

### Generator (noun)
A generator **deposits material into a buffer.** Conceptually equivalent to playing
notes in and recording them. Properties:

- **Always overwrites at its destination.** No "additive vs replace" mode; that
  distinction is the **destination choice** (see the control below).
- **Typed writer.** Emits one kind of data and may only write destinations of that
  type. Rhythm generators (Euclid/CA/Poly/Sub/LSys) emit *gates* → trigger layers.
  Pitch generators emit *note values* → note par-layers. The destination control
  offers only legal targets.
- **Commits immediately.** Generators don't need bounce; depositing *is* committing.
  So generators default to writing **fresh space** — "mutate material I love into
  something else" is a *processor* job, not a generator job. That boundary means a
  generator can never accidentally vaporize a loved capture.

### Generator commit model: ENGAGE / ROLL / BOUNCE (decided)
Generators are **continuously engaged**, not one-shot. (An earlier one-shot "RUN"
model was rejected: it left mutation-rate as an offline-only setting, defeating its
role as the §5 *journey* dial. Mutation must be live.)

- **ENGAGE / DISENGAGE** — connects/disconnects a generator to its destination.
  While engaged the generator continuously writes its loop into the destination's
  source layer; mutation, lock toggles, SNAP, contour changes are all heard live.
  On disengage the source remains as last written; the undo slot restores
  pre-engagement state.
- **ROLL** — forces an immediate full reroll of unlocked steps, bypassing the
  stochastic mutation cycle. Works even at mutation = 0 (advance a locked loop on
  demand).
- **BOUNCE** (the verb, below) — freezes current output to source and disengages.

### Processor (noun)
A processor **renders over a buffer non-destructively and continuously** — dial it
anywhere live, including back to zero, and the underlying material returns.
Robotize is the prototype. The chord-tone mask, voice-leading bias, and
window-through-recorded-steps are all processors. Processors transform existing
material; they do not create it.

**Born-as-processors rule** *(2026-06-09)*: **new musical transforms are born in the
render stack, not at emission time.** Emission-time effects are invisible to
`OutputActive`, so every one of them forces hand-written bake code at bounce (the §9
freeze-faithfulness saga); stack processors are bounce-faithful *by construction*.
**Done 2026-06-10:** the legacy emission chain (transpose → force-scale → limit)
migrated into the stack as Track 2 (§8 second build) and `SEQ_CORE_BakeForceScale`
was deleted — the bake program is over. Still emission-time (deliberately): groove
(timing), humanize/LFO/robotize (generation-axis randomness, reset on bounce
anyway), echo (scheduled repeats), and the three fenced pitch cases (arp playmode,
drum event mode, chord-layer expansion — §9 2026-06-10).

### Bounce (verb)
Bounce **commits the current processor (or engaged-generator) output into the buffer
as real notes**, so it stops being a live dial and becomes frozen material. *The*
freeze-vs-living line:

- Generators don't need bounce to commit (engagement already writes source); BOUNCE
  freezes + disengages.
- Processors need bounce: it turns continuous settings into committed notes. After
  bounce the dial is back at zero and the processed notes *are* the buffer — no clean
  underlying version unless one was saved separately.

### Destination (the one control)
The generator's **destination is a first-class, per-gesture control**, and it
*dissolves* the overwrite-vs-additive question:

- Write to an **empty** layer/track → behaves *additive* (new content, old kept).
- Write to an **occupied** layer/track → behaves *replace* (overwrites).
- No mode state, no merge rule, no hidden default. The consequence is *visible and
  chosen* (where the cursor points) — the live-safety property.

Granularities:
- **Layer** = parallel data on the *same* part (a second note lane, a chord, a
  modulation lane). **Free.** Cannot create an independent *rhythm* — all layers
  answer to one gate-per-step on a normal track.
- **Track** = an *independent part* (own rhythm/voice). **Costs a track** from the
  group/track budget (§6).

**UX = decorate existing controls, don't add surfaces.** Destination is already
`ui_selected_par_layer` + selected trigger layer + active track. Track-select picks
the track; layer-cycle, when a generator is active, skips type-illegal layers
(pitch generator stops only on Note par-layers); step LEDs already show occupancy
(dark = empty/additive, lit = occupied/replace), satisfying the visible-and-chosen
property with no new affordance. Add only: a small LCD destination readout
(`DEST: Trk3·NoteL2·[OCC]`) and the type-legality filter. On generator load, the
cursor auto-jumps to the first empty legal layer in the current track (predictable
default; override by navigating). Track-granularity is **not a mode** — point at an
unoccupied track and ENGAGE.

**Live-safety net:** one-deep auto-undo slot. When ENGAGE overwrites occupied
content, the prior content auto-saves; UNDO restores; next ENGAGE clobbers the slot.
Out-of-band; does not break "commits immediately."

### Recursion (the payoff)
The loop composes indefinitely:

> generators deposit → processors shape (continuous, non-destructive) → bounce
> freezes the result into a new buffer → the new buffer is again open to generators
> and processors → forever.

"Generation" was never special — it's the first turn of the crank (a source plus
transforms, bounced). Because frozen MIDI keeps its event structure, a capture can
be re-processed with the **full** generator + processor vocabulary (not just block
ops like audio); capture *opens a new processing chain with full access*, branching
as deep as you like.

### Composition rules (how the pieces interact)
Stated explicitly so they aren't re-derived, and so edge cases don't trip the model:

- **Capture stores both faces** (§5): the *recording* (source + a processor-output
  snapshot) and the *posture* (source + processor stack + params + generator state).
  Patterns and track slots persist both — see the v3 format note (§6).
- **FREEZE is the master mutation switch.** FREEZE = effective mutation rate 0%
  across all generators; loops hold, rerolls pause. Two-face recall (load + FREEZE
  held) is exactly "load posture, immediately freeze" → the frozen-tape face.
- **Soft return composes through windowing.** Dialing mutation to 0 with a window
  active: the loop goes static, processors transform, the window slices — soft return
  reaches the listener through the window. SNAP (hard return to anchor) is orthogonal
  to window position; both axes move independently.
- **Window source is performance state, not pattern state.** Pattern recall does not
  reset window-source choices.
- **Bounce waits for sweep quiet** (~50ms after a knob stops) so the committed thing
  is the thing you heard.
- **Generators write only live tracks**, never track slots — slots are captured
  output, not generation targets.
- **Type-class stacks compose additively for the listener** (note/gate/vel/len run
  side by side, affecting different aspects of the same event); no cross-class
  conflict.

### Real-time budget (pointer)
Non-destructive stacks cost CPU/RAM *per layer at playtime*; the STM32F4 tick budget
is finite. The model stays "non-destructive everywhere," realized via a render-cache
(bake stack output when idle, render live only while a knob moves). The full
mechanism, its cost model, and the corrected RAM budget are in **§A2 / §A5
(Part II, provisional)** — they are *not* a prerequisite for the first sound (§8).

---

## 4. MIDI-as-sample mental model

A structurally (not loosely) accurate lens:

- **Live stream** = process-in-flight. The engine; can surprise; never repeats
  exactly. (Generative groups.)
- **Sample / capture** = a *frozen, bounded, random-access* buffer. The bounded edges
  unlock sampler operations — window position, length, reverse, read-rate vs
  transport, jump-to-offset, layer/overlay — precisely *because* it's addressable.

**MIDI advantage over audio (why build, not buy an Octatrack):** audio freezes into
an opaque summed waveform; frozen MIDI freezes into a *structured, transparent*
object — each event stays individually addressable. So re-processing frozen MIDI gets
the entire generator+processor vocabulary again (transpose the window, re-quantize
off-grid notes, mask pitch while leaving rhythm, decimate by velocity, invert
contour). The chain *loops*; audio's terminates at the sample.

**Discipline:** a sample is *frozen* — it does not regenerate itself. Evolution lives
in how you *window and process* it. Expecting a captured buffer to "keep being
generative on its own" is wanting the *engine* — a different object. Re-processing
frozen MIDI is operating on *richly editable dead tape*: trustworthy *and* malleable.

**Polyphonic sampling:** multiple captured takes in multiple slots, windowed
independently and layered, is where "morph two recorded states" lives natively — two
takes, crossfade or interleave windows. No engine interpolation, no shared-voice
problem.

---

## 5. State, capture, and morphing

### Two faces of a captured state
A capture is **both** at the instant of capture (identical sound), but behaves
oppositely the moment you touch it:

- **The recording** — the literal notes that came out. Dead, reliable, repeatable.
  The *anchor*.
- **The generative posture** — the configuration (seed + transform/param positions)
  that *produced* them. Alive: re-enter it and it regenerates *fresh*. The *spring*.

The pilot loop needs both. Store a state as both; the performable choice is *which
face you grab*. (This is a recall-gesture choice on shared data — see §5.5 — not two
storage classes.)

### Recall semantics (decided)
Re-entering a saved state **relaunches the posture and regenerates** — it does *not*
resume runtime position. "Dipping back into the well": the well is the spring, not
the water already drawn. *Similar-but-not-identical* return is the **feature**, and
the cheaper thing to build. (Recall ≠ resume; confirmed against §6 save/recall.)

### "Identifiable yet unexpected return" — the mechanism
The disorder must be a **reversible transformation of an identity**, not a
*replacement*. The thing that fell apart and the thing that re-coheres share DNA:

- **Seed** = identity (loop/contour/rhythm; lockable; the return target).
- **Transform path** = the journey away (mask open/close, harmonic recenter,
  decimation, density).
- **Back into focus** = collapsing transforms toward zero while the seed reasserts.

The **Turing-machine model** (locked loop = identity + performable mutation = the
journey) *is* this structure. Per-step lock = the human-vs-machine symbiosis dial.

**Three orthogonal blur axes** *(2026-06-09)*: the journey away from an identity has
three independently performable dimensions — **pattern** (mutation rate; soft return
= rate→0, hard return = SNAP), **time** (the window over a captured buffer), and
**harmony** (the GRAVITY field, §8 second build — push toward RUB/SLIP, pull toward
chord/root; RESOLVE = the bar-quantized harmonic return). A state can fall apart
harmonically while the loop holds, or rhythmically while the harmony holds; the
return recipes compose.

### Morphing
- **Live posture morph** — two states at once = two groups playing, ride the balance
  (mutes/levels/macro). 8 tracks for a 2-state morph. *Interpolating two live engines
  on shared voices is not available* (§6 group binding): morph = balancing two
  concurrent group-states.
- **Recorded-state morph (preferred, cheaper)** — window through a captured buffer.
  "Morph two states" = bounce A then B end-to-end, window across the seam. Crosses
  *tape*, not springs — reliable and repeatable, exactly what identifiable-return
  wants. (Uses existing bounce mechanics; no new "concatenate" verb in v1, per §2.1.)
- **Phrase-posture morph (added 2026-06-13 — a third model the PHRASES snapshot
  library unlocks; candidate, not built).** Interpolate the live engine's *posture*
  (dials/CCs) between two stored phrase snapshots on a knob — a form of the "two live
  engines" morph the first bullet ruled out, now possible because the phrase holds the
  full committed state as values. Details + the posture-vs-note-content split + RAM
  gate in §10 ("Phrase morphing").

### Set structure (skeleton and muscle)
Two intensity time-scales, two different tools:

- **Skeleton (macro / set arc) = groups stacking.** The 4-group budget is the
  intensity ladder (1/2/3/4 active groups = four macro levels); transitions are group
  mute/unmute or per-group pattern change (existing measure-quantized machinery); the
  256-slot pattern bank and existing song mode handle pre-planned chains. **No new
  macro-level feature work is needed** — this rides entirely on upstream.
  *(Revised 2026-06-11: the skeleton becomes phrase-centric — phrases of track-grain
  refs are the scene system, and "no new macro-level feature work" is withdrawn to
  that extent. See §9 2026-06-11 block.)*
- **Muscle (within-group / moment-to-moment) = processor dials + generator params.**
  Density-thin/fill (gate), mask strength (note), mutation amount (generator),
  robotize (vel/len). This is where "live psychedelic sculpting" actually happens.

**Useful negative result:** every net-new piece in this design (processors, pitch
generator, mask, render-cache, track slots, windowing) is *within-group* muscle. The
macro skeleton is already built upstream. This bounds the build surface. Growing one
state past its 4 tracks (an 8/12/16-track organism) is authored as coordinated
groups via song-mode + the pattern bank — a planning problem, not a platform one.

Backbone = composed group-states with sequential recall. Playground = an improvise
area (open group(s) + the windowing buffer) to play in, process, capture, and layer
against the backbone.

### 5.5 Slots, captures, and windowing (decided)

- **Group slot = pattern.** No parallel mechanism. A saved pattern already carries
  *both* §5 faces: trg/par layers (the recording) **and** track CCs 0x80–0x95 (the
  posture — robotize mask, probabilities, loop control, anchor seeds). 4 banks × 64 =
  256 addressable group-states, 20-char names, per-group binding. (See §6 anchors.)
- **Two-face recall is a gesture, not separate storage.** *Posture recall* = load and
  let generators run (today's default). *Frozen-tape recall* = load with FREEZE held,
  so generators don't re-run (existing ROBOLOOP FREEZE does the work). Zero new mode.
- **Quick-capture** = SAVE-button single-press → save the current group state to the
  next free pattern slot in the active bank, auto-named `CAP_NNN`; long-press = the
  traditional save UI. Bank-full **wraps** the oldest auto-named slot (promoted/
  renamed slots are exempt) so the performer is never blocked mid-set. *No scratch
  ring in v1* — direct SD save; revisit only if a timing test (§A4 #8) shows the write
  bumps a tick.
  > **Superseded 2026-06-11:** quick-capture is replaced by **the tape** — an
  > append-only, session-scoped take list that never aims and never overwrites.
  > The `CAP_NNN` wrap-oldest scheme is withdrawn; bank-full refuse+flash survives
  > only for *aimed* gestures. Storage fork open. See §9 (2026-06-11 block) and
  > `doc/plans/2026-06-11-save-model-groups-performing-curating.md`.
- **Track slots (net-new).** A captured single-track buffer (trg/par + that track's
  CCs), layerable independently — this is what makes §4 polyphonic sampling concrete.
  Gesture: hold a track-select button + CAPTURE → next free RAM slot, auto-named
  `T01…` (the held button *is* the source indicator). **Hybrid persistence: RAM by
  default**, explicit *promote-to-SD* deferred (design the SD format when actually
  wanted). `CAPTURE + group-select` captures all 4 group tracks into 4 RAM slots at
  once, shared prefix.
- **Windowing** = a read gesture over *any* buffer (pattern / track slot / live
  track), uniform: position, length, rate (sync ratio), direction, wrap. **Always-
  available on live tracks** — with no capture loaded the window knobs ride the active
  track's buffer. Default playback is **verbatim** (§4: dead, editable tape). Engine-
  *reseed* (a buffer used as a seed re-coloured by a live posture) is a *different
  object* — a generator that consumes a buffer. **Window ≠ reseed.**
  - *Note (needs live validation, §2.2):* live-generate + live-window + sweeping
    position on the **same** buffer is a genuine concurrency hot-spot (active write +
    remapped reads + cache invalidation at once). Reasoned-clean on paper; treat as
    prove-by-ear, not designed-and-done.
- **Windowing controls** = four dedicated encoders (position / length / rate /
  direction+wrap), top-priority physical assignment — the literal §1 sculpting hands.
  **Per-track window source** (live / slot N / pattern N) for polyphonic sampling;
  **track-focus model** (the four encoders act on the focused track; per-track state
  persists). Step LEDs highlight the windowed range during sweep; a lower-LCD 4-track
  summary gives global oversight while step LEDs stay focused.
- **3-frozen-+-1-alive** is the canonical anchored-chaos recipe: track slots are dead
  tape (don't mutate); mix three windowed captures with one live Turing track → three
  stable layers, one degree of freedom under the thumb.

---

## 6. Platform realities (verified hardware constraints)

All verified against the fork source this session. **Line numbers are point-in-time
and drift** — function/symbol names are the durable anchors. See
[REFERENCE §3](MBSEQV4_REFERENCE.md) for the bus deep-dive.

### Groups bind 4 tracks; pattern selection is per-group
- `SEQ_CORE_NUM_GROUPS = 4`, `SEQ_CORE_NUM_TRACKS = 16` ([seq_core.h:21](../core/seq_core.h#L21)).
- Per-group pattern selection: `seq_pattern[SEQ_CORE_NUM_GROUPS]`
  ([seq_pattern.c:50](../core/seq_pattern.c#L50)); `SEQ_PATTERN_Load` fans one read
  across the group's four contiguous tracks (`group * TRACKS_PER_GROUP`).
- **Consequence:** changing a pattern changes all 4 tracks of the group *together*.
  A "state" = a group = 4 fixed tracks.
- **Concurrency** = up to **4 independent group-states**. No spanning / one-state-
  many-groups; copying makes an independent instance that drifts. States don't *grow*;
  you get more *of them* or more *contrast*. A whole-box organism is coordinated
  groups, not one expanded state.
  *(Revised 2026-06-11: groups demote to shelving — the performer-facing recall
  grain becomes the track, and "organism = coordinated groups" softens to "organism
  = a phrase." Switching machinery stays group-grain. See §9 2026-06-11 block.)*

### Save/recall (intentional)
Switching patterns without saving loses changes (jam freely, commit deliberately).
Recall brings a state back *as saved* and **relaunches/regenerates** — it was on disk,
not running in the background. Reconstructed continuity, not literal concurrency
(see §5 recall semantics — this is the desired behavior).

> **Reversed 2026-06-11** (direction; mechanism provisional): the lose-on-switch
> clause is overturned — the working state will always persist (auto-writeback on
> switch) and protection becomes the explicit CHECKPOINT/REVERT act. The
> recall-relaunches clause is **unchanged**. See §9 (2026-06-11) and
> `doc/plans/2026-06-11-save-model-groups-performing-curating.md`.

### Pattern infrastructure
- 4 banks × 64 patterns = 256 group-states; 20-char names
  ([seq_file_b.h:22](../core/seq_file_b.h#L22), [seq_file_b.c:294](../core/seq_file_b.c#L294)).
- A saved pattern persists trg/par layers + track CCs **0x80–0x95**
  ([seq_file_b.c:844](../core/seq_file_b.c#L844)) — i.e. the recording *and* the
  generative posture (robotize mask, probabilities, loop control, anchor seeds) in one
  object. Disk-resident ≈ 6KB/pattern today.
- Per-track live buffers: `SEQ_PAR_MAX_BYTES = 1024`, `SEQ_TRG_MAX_BYTES = 256`
  ([seq_par.h:25](../core/seq_par.h#L25), [seq_trg.h:23](../core/seq_trg.h#L23)) → 1.25KB/track.
  `seq_par_layer_value` already uses `AHB_SECTION` placement (§A5 matters).
- **Pattern format extends to v3 (backward-compatible)** to carry the spine posture
  the v2 format lacks: per-track processor-stack composition + params (per
  layer-class), and Turing-generator state (loop, anchor, per-step lock, per-step
  mutation multiplier) per active generator. New sections append after the v2 data
  behind a magic-byte + version; v2 patterns load with default-init spine state
  (playable, spine-empty). Track slots use the same per-track section format. Size
  growth ~6KB → ~15–20KB/pattern; 256 × 20KB ≈ 5MB on SD — comfortable. *(The format
  spec lives here; the implementation is gated behind §8.)*

### Bus tracks (inter-track transport)
- 4 loopback buses; each owns transposer + arp-sorted + arp-unsorted notestacks
  ([seq_midi_in.c:63](../core/seq_midi_in.c#L63)).
- **Same-tick following by design.** Two-round tick loop ([seq_core.c:849](../core/seq_core.c#L849)):
  round 0 = loopback-port tracks, round 1 = normal-port tracks (gate at
  [seq_core.c:859](../core/seq_core.c#L859)). Loopback emission is synchronous
  (`SEQ_MIDI_IN_BusReceive(..., 1)` "forward immediately"). **Rule: source on a Bus
  port, subscribers on normal ports** → consumers see this tick's chord. Caveats: both
  on Bus = ordering by track number; a Bus track is silent by idiom; external MIDI on a
  loopback-driven bus flushes its stacks.
- **Transpose collapses the cluster; arp already reads it.** Held chord is retained,
  but `SEQ_MIDI_IN_TransposerNoteGet` ([seq_midi_in.c:1149](../core/seq_midi_in.c#L1149))
  returns one note → `SEQ_CORE_Transpose` (~`:1961` in this fork; mainline `:1882`)
  applies a flat offset = parallel planing. The **Arpeggiator** branch already
  consumes the whole stack (`SEQ_MIDI_IN_ArpNoteGet`).
  - **Chord-context extension point:** a new playmode beside Transpose/Arp that reads
    the whole `bus_notestack[bus][BUS_NOTESTACK_TRANSPOSER].note_items[]` as a
    pitch-class set and *constrains* `p->note` into it. Structurally an arp-branch
    variant; mechanism already exists.
  - `V4.097` already drives global Root/Scale over loopback; track Root/Scale par
    layers since `V4.092`.
  - **Verified in this fork (2026-05-24):**
    - *Notestack sort order.* The transposer stack is `NOTESTACK_MODE_PUSH_TOP`
      ([seq_midi_in.c:327](../core/seq_midi_in.c#L327)) — insertion order, newest at
      `note_items[0]`, **not** pitch-sorted. Irrelevant for a chord-tone mask (it
      builds an unordered PC-set from `note_items[0..len-1]`). Capacity
      `SEQ_MIDI_IN_NOTESTACK_SIZE = 10` ([seq_midi_in.c:55](../core/seq_midi_in.c#L55));
      the whole held cluster accumulates (every Note On pushes; Note Off pops the
      specific note). **Bonus:** the `ARP_SORTED` stack
      ([seq_midi_in.c:332](../core/seq_midi_in.c#L332), `NOTESTACK_MODE_SORT`) holds
      the *same* notes pitch-sorted — a free ordered view if voice-leading (v2) wants
      bass/nearest-tone ordering.
    - *Scheduling intact.* The two-round loop is preserved
      ([seq_core.c:894](../core/seq_core.c#L894), loopback gate
      [seq_core.c:904](../core/seq_core.c#L904)); `SEQ_CORE_Transpose` is still called
      inside the round-loop body ([seq_core.c:1275](../core/seq_core.c#L1275)). The
      fork's robotize-loop additions run in a measure-boundary pre-pass *before* the
      round loop (~[seq_core.c:782](../core/seq_core.c#L782)) and touch only robotize
      seed/anchor/resync state — they do **not** sit between bus-populate (round 0)
      and bus-consume (round 1). The same-tick chord-context guarantee holds.

### Normal vs drum track (pitch-vs-rhythm tradeoff)
- **Normal track:** one rhythm, full pitch/velocity/length depth (melody, or a chord
  sharing one gate). Density costs one track per line. *The pitch generator now runs
  here too (mono, shipped + GO 2026-05-26 — see §9); the engine was nearly track-
  agnostic already.*
- **Drum track:** up to 16 instruments = independent gate timelines (cheap polyrhythm,
  one group slot), but **note is hardcoded to the per-instrument constant**
  `note = tcc->lay_const[0*16 + drum]` ([seq_layer.c:370](../core/seq_layer.c#L370)).
- **The drum-pitch unlock (key finding).** In the drum path, velocity/length/
  probability already come from par-layers via `link_par_layer_*`; **note is the only
  field not wired.** `SEQ_PAR_Type_Note` exists ([seq_par.h:38](../core/seq_par.h#L38))
  but the drum branch never reads it for the note. So the wall is **one resolution
  line** — wire a `link_par_layer_note` for drums (mirror `link_par_layer_velocity`).
  Then each drum instrument's rhythm carries its own per-step pitch line →
  polyrhythmic counterpoint (monophonic-per-line, polyphonic-across-lines) on one
  group slot, and pitch inherits the entire generator/processor/bounce spine.
  - **Costs/limits:** drum par-layers budgeted at 4/drum (`seq_layer_drum_cc[16][4]`,
    [seq_layer.c:45](../core/seq_layer.c#L45)) traded against instrument count → a
    *handful* of pitched lines/track, not unlimited. All drums fire on the track's one
    MIDI channel (`p->chn = tcc->midi_chn`); per-instrument channel is an unwired TODO
    (const D), defer to v2. Note value 0 suppresses the event (`if(note && velocity)`)
    — the pitch source must avoid 0.

---

## 7. Current fork state (POC inventory: reusable vs disposable)

Built in a few-day sprint. All **disposable** (§2.1) — but several pieces are
conceptually load-bearing.

### Conceptually load-bearing (ideas survive; code may not)
- **ROBOLOOP page** (shipped): reseed / freeze / freeze-q / reroll / master-sync,
  scale-aware robotize. *The freeze-edit-regenerate symbiosis loop.* FREEZE/REROLL are
  the human-takes-the-wheel handoffs; the locked-content + mutation idea maps to the
  seed+transform model (§5).
- **Capture / bounce** (shipped + matured 2026-05-31, see §9): the **bounce verb** (§3),
  now a unified lossless computed-output model on a deliberate PATTERN-hold gesture —
  same-group = save a variation (source untouched); cross-group = merge into the dst
  group's own bank + auto-load it on the bar. The capture half of the pilot loop is
  solid, and cross-group auto-load gives live recall *at the destination*; **recall back
  into the source's running generative process** (§5 sculpt→capture→travel→return) is
  the missing complement.
- **Python HIL harness + testctrl SysEx**: enables safe surgery on timing paths
  (§2.5). Keep and extend (§A4).
- **Drum-pitch unlock** (shipped 2026-05-24, §8 step 1): `link_par_layer_note` wired
  in the drum branch of `SEQ_LAYER_GetEventsPlus` ([seq_layer.c:369](../apps/sequencers/midibox_seq_v4/core/seq_layer.c#L369));
  value-0 falls back to `lay_const` preset. *Permanent — load-bearing for the
  whole step-1-onward stack; not disposable.*
- **PITCHGEN page** (shipped 2026-05-24, §8 step 2, disposable POC):
  `core/seq_ui_trkpitchgen.c`. Single-gesture GP1 = reroll active drum's Note layer
  with random C2-C6. Throwaway; replaced by the v1 generator page in step 6.
- **ChordMask playmode** (shipped 2026-05-24, §8 step 4, disposable in current form):
  `SEQ_CORE_TRKMODE_ChordMask` enum value + `SEQ_CC_CHORDMASK_STRENGTH` (0x96) +
  `SEQ_MIDI_IN_BusPCSetGet()` helper + branch in `SEQ_CORE_Transpose`. UI on TRKMODE
  page (5th mode, GP6 selects, GP7 dials strength, LCD overlay shows `Msk:NNN`).
  *Conceptually load-bearing; code migrates to a chord-mask processor under the
  spine (step 5 phase C).*

### Old-paradigm (rebuild under the spine)
- **GENERATE suite** (`seq_ui_trkeuclid.c`): EUCLID/CA/POLY/SUB/LSYS — **all trigger/
  placement** generators (emit gates, never pitch). They write **directly into the
  live buffer on knob-turn**, destructively, write-once-forget, no provenance. The
  *old* paradigm. Under the spine they become *sources that yield events on request*;
  the math survives, the **write target** changes.

### Docs (Claude-authored, this convention)
- [MBSEQV4_REFERENCE.md](MBSEQV4_REFERENCE.md), [MBSEQV4_MANUAL_FORK.md](MBSEQV4_MANUAL_FORK.md),
  [MBSEQV4_TODO_TRIAGE.md](MBSEQV4_TODO_TRIAGE.md) (the last is housekeeping, *not* the
  musical roadmap).

### Parked / cruft
- `feature/tm-generator` — parked, "needs runtime-hook redesign" (already hit this wall).
- Conditional triggers — roadmap (see §A1).
- Tracked `.DS_Store` (`git rm --cached`); bundled TK PDFs in `doc/` (redistribution
  consideration on a public repo).

---

## 8. The first real musical build (music-first build order)

The spine is settled and there is no architectural reason left to defer pitch. The
first build is the **drum-pitch triplet** — chosen over the normal-track version
because it composes with existing drum-rhythm tooling.

**The ordering principle (enforces §2.2):** prove the *music* with throwaway code
*before* building the architecture that makes it performant. The render-cache spine
(§A2) is **not** step 1. A sound comes first; infrastructure follows a sound you've
already heard and liked. Earlier drafts front-loaded the cache — that is corrected
here.

**The three pieces** (detail; built in the order below, not this listing order):

1. **Note-layer unlock for drums** — add `link_par_layer_note` to drum CC config
   (mirror `link_par_layer_velocity`); in the drum path ([seq_layer.c:370](../core/seq_layer.c#L370))
   read note from the Note par-layer when set, else fall back to `lay_const` (preserves
   current behavior). Handle the note-0 suppression edge.
2. **Pitch generator (Turing-style)** — per-drum-instrument; emits Note values. Params:
   seed (identity anchor), range, scale (existing root/scale plumbing), contour
   (walk/Brownian/random), mutation amount (the §5 journey dial, performable across
   full range incl. locked), per-step lock (locked steps survive reroll). ENGAGE/ROLL/
   BOUNCE commit model (§3). *Loop length is statically sized — see the §A5 decision.*
3. **Chord-tone mask processor** — per drum slot; params: drum filter, bus (per-
   processor selection), strength (0 = pass-through *mandatory* → mid = probabilistic
   snap → max = hard lock). Reads the held PC-set from the bus's transposer notestack
   via the new **bus chord-context playmode** (§6). Voice-leading bias = v2.

**Build order (music-first; each step ends in something audible or a go/no-go):**

0. **Measure base SRAM usage. ✓ DONE 2026-05-24** — 95.9KB used / ~32KB free main,
   64KB CCM virgin (§A5). First build fits trivially; full Part II fits but tight.
   New gating unknown: MSP high-water (§10).
1. **Note-layer unlock + hand-authored melody.** Wire the one line; manually author a
   Note par-layer; hear a drum play a melody. *No generator, no cache, no mask.*
   Validates the cheapest, most load-bearing assumption — does the unlock work and
   sound musical. Half-day surgery + harness regression test.
2. **Minimal pitch generator, OLD destructive write.** Write the Note layer directly
   the POC way (no spine, no cache, no ENGAGE plumbing yet). Get a *generative pitched
   drum line into your ears* in an afternoon. Deliberately disposable.
3. **GO / NO-GO.** Is the generative-pitched-drum core musically worth building out?
   Only proceed if yes. This is the §2.2 enforcement point — do not skip it.
4. **Bus chord-context spike + minimal mask** (still simple/destructive). The bus
   plumbing is pre-verified (§6, 2026-05-24): read the transposer stack as an
   unordered PC-set, same-tick contract holds. Wire the playmode; hear the constraint
   *sweep live* against a real chord. Proves constraints-as-material (§2.3) by ear.
5. **Build the render-cache spine** (§A2) — *now* that the music is proven worth the
   infrastructure. Migrate the generator + mask onto it (non-destructive, ENGAGE,
   undo). Heavily harness-tested (the timing tests in §A4 attach here).
6. **Generator polish** — per-step lock, contour shapes, mutation depth, full ENGAGE/
   ROLL/BOUNCE.
7. **Mask polish** — per-drum-slot scope, per-processor bus selection.
8. **Voice-leading bias** — v2 processor.

### The second musical build — the Tension Workbench (decided 2026-06-09)

> **SHIPPED + by-ear GO — 2026-06-10.** Track 1 (the Workbench) is built, flashed, and
> confirmed by ear: full pull collapses to the One, push reads as tension, RESOLVE
> lands a cadence on the downbeat, the detent is home. The user deferred refinement
> ("not at a point to refine yet") — a **soft GO**. The whole bundle is HIL-green
> (processor · cockpit · RESOLVE · SHADE · GRIP persistence · rig tooling). What
> diverged from this plan, and the decisions that emerged, are in §9 (2026-06-10);
> codebase facts (symbols, verbs, gotchas) in the REFERENCE doc. **Track 2 (pitch-chain
> migration) is now licensed.** The plan file is marked executed.

The first build proved pitch as a *generated* dimension; the second makes harmony a
*performed* one. Per §2.7 it ships as **one playable workflow**, not a sequence of
dials. Durable summary here; full working detail in
[doc/plans/2026-06-09-tension-workbench.md](plans/2026-06-09-tension-workbench.md)
(plan = scaffolding, archived once executed).

**The GRAVITY field.** Force-to-scale and the chord mask are *projections* — a legal
set and a snap onto it; their dials only make material more correct. Tension needs a
**ranked field**: every pitch class scored on a stability ladder computed live from
masks the box already holds — root → root+fifth → chord tones (the bus PC-set,
performable by holding a chord) → scale tones (global FTS scale) → chromatic
neighbors-of-chord-tones. One bipolar knob (center detent = true pass-through, §2.3
refined) moves gripped notes along the gradient:

- **Pull (CCW):** tidy to scale → lock to chord → collapse toward root+fifth → the
  ONE. Force-to-scale and chord-mask-at-max become *regions* of this knob.
- **Push (CW):** **LEAN** (in-scale neighbors of chord tones — sus/add color;
  correct tension) → **RUB** (chromatic neighbors of chord tones — maximum
  dissonance lives a semitone from maximum consonance, so tense notes are
  automatically leading tones) → **SLIP** (rotate the whole constraint set ±1
  semitone — side-slipping; coherent alien).

Two structural properties carry the music: **push constrains** (it narrows to the
tense subset — structured wrongness reads as intent, loosening reads as grey; the §5
identity principle applied to harmony), and **the release gesture is a cadence**
(RUB tones resolve by semitone into chord tones, so a fast throw through the detent
performs tension→resolution regardless of material). The §2.3 filter-sweep image
made literal; a DJ-isolator hand transfers.

**The cockpit (one page — the harmonic sibling of ROBOLOOP):**
- **GRAVITY** — the global bipolar field knob.
- **GRIP** — per-track field strength: *routing, not performance* — the send-amount
  that makes the **room** lean under one hand. Mirrors the FTS global-value /
  per-track-opt-in pattern (independent per-track fields read as noise, not force).
- **RESOLVE** — bar-quantized ramp of GRAVITY to the detent, landing on the one.
  Resolution *timing* separates an instrument from an effect; in the bundle, not
  deferred.
- **SHADE** — modal-brightness ladder (Lydian→…→Locrian, one flattened degree per
  step, parallel modes) over the existing scale table; the *terrain* GRAVITY moves
  across. Darkening under a static held chord makes the ground itself tense —
  listen before designing that away.

**Bundle contents (ships together):** the `SEQ_PROCESSOR_ID_TENSION` processor
(render-stack, beside chord-mask — bounce-faithful by construction, and itself a
test of the bounce model: freezing a pushed groove must need **zero new bake
code**); global+GRIP plumbing; **deterministic grip** (hash, not live RNG —
returnable states, exact HIL); RESOLVE; SHADE; the **ext-CC persistence fix** (the
parked 0x96 bug is load-bearing here — and is wider than recorded: CCs 0x96–0x99
all sit past the persisted range); a 2-minute rig recipe (chord track on a bus + a
pitched-drum counterpoint track); HIL tests; the listen protocol. **Out of bundle,
named so they don't creep:** inter-voice compression, register-spread
(sample-mapped tension), the rhythmic/metric sibling, voice-leading-aware snapping,
per-voice normal-track polyphony, TENSE-hit.

**POC rule:** FTS off on gripped tracks (the emission snap would re-correct every
pushed note); gravity's pull side contains scale-snap anyway. Track 2 dissolves the
conflict properly.

**Two-track sequencing:**
1. **Track 1 — the Workbench** (days): everything above, ending at the cockpit page
   and a workflow-level GO/NO-GO by ear.
2. **Track 2 — pitch-chain migration** (gated on the GO; the licensed §2.7-corollary
   rewrite): transpose → force-scale → limit move from emission into the render
   stack **together** (FTS-after-transpose is what keeps bus-planing in-scale, so
   they are entangled; the stack re-renders on transposer change the way chord-mask
   already does on chord change). Deletes `SEQ_CORE_BakeForceScale` and ends the
   per-effect bake program by construction; stack ordering becomes explicit (§9
   wanted this); TRKMODE/FTS UX preserved via the phase-C slot-sync bridge; the
   existing capture HIL tests are the regression net.

   > **SHIPPED + by-ear GO — 2026-06-10 (HIL 108/108; "works so well").** Built in four staged
   > checkpoints (plan `doc/plans/2026-06-10-pitch-chain-migration.md`). Slot
   > order: PITCH(0) → CHORD_MASK(1) → TENSION(2) → LIMIT(3) — FTS upstream of
   > TENSION **retires the Track-1 POC rule**: FORCE_SCALE and GRIP coexist on a
   > track, a push survives the snap (HIL-pinned at emission). Three fences keep
   > the legacy emission chain: **Arp playmode** (multi-arp runtime state; user
   > call), **Drum event mode** (0-byte = lay_const fallback, not a rest), and
   > **Chord par layers** (the byte is a chord index). The bake is deleted —
   > capture = mirror copy + the existing generative reset, and **capturing a
   > planed groove is now faithful** (the old bake only reproduced static
   > Normal-mode transpose). Two latent Track-1 bugs fixed en route: stranded
   > RESOLVE flag eating GRAVITY at next play's downbeat (STOP now lands an
   > in-flight ramp at 0), and preset-import/capture-restore paths that bypassed
   > the slot bridges. Decisions + accepted edges in §9 (2026-06-10, Track 2);
   > symbols/gotchas in the REFERENCE doc.

**Listen protocol (the GO/NO-GO):** does full pull feel *inevitable*; does LEAN
read as harmony, not error; does the detent feel like home; does push→RESOLVE land
as a cadence on the one; does the *room* lean when three tracks share the knob; does
a banked tense variation recalled cold still feel like the same place.

---

## 9. Decisions log (what we decided and why)

Append-only-ish; revise an entry only with a dated note.

**Foundational**
- **Build, not buy.** Self-expression on a personally-significant platform; reorders
  toward the differentiated musical core.
- **Live-focused, discovery-driven.** No predetermined destination; the instrument
  must be able to *surprise* the pilot → favors the live engine.
- **POC disposable; work with the grain.**

**The spine (§3)**
- **Two-noun/one-verb spine** over any source/transform-graph. Recursive, composes.
- **Destination control dissolves overwrite-vs-additive** — a per-gesture target
  choice, visible, not a mode. Generators always overwrite at their target.
- **Generators add; processors transform.** "Mutate loved material" is a processor
  reach.
- **Generator commit = continuous ENGAGE + ROLL + BOUNCE**, not one-shot RUN.
  Supersedes the earlier RUN draft — mutation rate must be a *live* dial (it is the
  §5 journey). ROLL = on-demand reroll; BOUNCE = freeze + disengage.
- **Destination UX = decorate existing controls** (track select / layer cycle / step
  LEDs) + small LCD readout + type-legality filter; one-deep auto-undo as the
  live-safety net; default destination = first empty legal layer.
- **Bounce = the freeze verb.** **Recall = relaunch/regenerate, not resume.**
- **Composition rules made explicit** (FREEZE = master mutation switch; soft return
  composes through window; SNAP orthogonal to window; window source is performance
  state; bounce waits for sweep quiet; generators write only live tracks; type-class
  stacks compose additively).

**State / capture / morph (§5)**
- **Group slot = pattern**; it already carries both §5 faces (recording = layers,
  posture = CCs). **Two faces are a recall *gesture*** (load+FREEZE vs load+run), not
  separate storage.
- **Track slots are net-new**, hybrid persistence (RAM default, explicit SD-promote
  deferred). Justified by §4 polyphonic sampling, which patterns can't express.
- **Quick-capture = SAVE-button** (single→next free `CAP_NNN` in active bank, wrap
  oldest auto-named; long→traditional UI). No scratch ring in v1. *(Superseded
  2026-06-11: replaced by the tape — append-only take list, wrap-oldest withdrawn.
  See the 2026-06-11 block.)*
- **Windowing rides any buffer incl. live tracks**, always-available; default
  verbatim. Engine-reseed is a separate object. Four dedicated encoders, per-track
  source, track-focus model.
- **3-frozen-+-1-alive** = canonical anchored-chaos recipe.
- **Morph-across-seam uses existing bounce**, no new verb in v1.
- **Concurrency is group-granular, 4 max.** *(Revised 2026-06-11: still true of the
  switching machinery, but groups demote to shelving — recall grain = track. See the
  2026-06-11 block.)*
- **Set-density is two-scale** (closes old open-Q): macro = groups stacking (existing
  tooling, **no new macro features**); within-group = processor/generator dials (the
  muscle, where the fork's value lives). *(Revised 2026-06-11: macro becomes
  phrase-centric; "no new macro features" withdrawn to that extent. See the
  2026-06-11 block.)*

**Platform / build (§6, §8)**
- **Drum-pitch via a Note par-layer is the first build** — one-line unlock, inherits
  the spine, yields polyrhythmic counterpoint.
- **Drum Note par-layer: value 0 = use preset (lay_const), not silence.** Empty Note
  par-layer behaves like today's drum (preset pitch from `lay_const[0*16+drum]` fires
  when the gate fires); a non-zero step overrides to that pitch. Per-step silence is
  the trigger layer's job, not the note layer's. *Confirmed live 2026-05-24: the
  earlier "0 = silence" reading killed the drum the moment a Note layer was assigned
  before any step was authored, breaking the assign-and-hear UX. The fallback also
  gives the Turing pitch generator (§8 step 2) a sensible interpretation if it ever
  emits 0.*
- **§8 step 3 — GO (2026-05-24).** Pitched-drum confirmed musically real by ear. The
  throwaway POC (`PITCHGEN` page, GP1 fills active drum's Note layer with random
  C2-C6) is enough to validate the idea; proceed to step 4 (bus chord-context + mask).
- **Sample-mapped destinations are a first-class target.** When the listener is an
  Ableton Drum Rack or any sample-mapper, MIDI note = which cell plays, so wide pitch
  range becomes a *sound-variation* generator (not just a detune). This is a "free"
  musical dimension that emerges from the unlock — not designed for, but exactly the
  §1 "control plane for the whole rig" thesis in action. *Implication for v1 mask /
  voice-leading: the user-meaningful range can be huge (full keyboard) for sample-
  mapped destinations and tiny (semitone or two) for monotimbral drums — so range
  min/max must be a performable dial, not a fixed config.*
- **Range is the dominant musical dial for the pitch generator.** Narrow ≈ detuning
  feel; wide ≈ sound shuffle (in sample-mapped destinations) or wild leaps (in
  monotimbral). Confirms §A3's range min/max as a Day-1 v1 dial, not optional polish.
- **Robotize-pitch becomes audible on drums for free.** The existing monolithic
  robotize (§7) already perturbs pitch; before the Note-layer unlock there was nothing
  to perturb on drums, so the effect was inaudible there. Now it works without new
  code — a free preview of the §A1 typed-robotize-pitch plan. *No need to rebuild
  robotize-pitch until the spine arrives — keep the monolith until then.*
- **§8 step 4 — GO (2026-05-24).** ChordMask playmode + per-track strength dial
  confirmed musically real by ear. POC: new `SEQ_CORE_TRKMODE_ChordMask`, reads bus
  transposer notestack as 12-bit PC-set, probabilistically snaps emitted notes to
  nearest in-set semitone (strength 0–127 = pass-through → hard lock). Local
  chord-source pattern (chord-event-mode track outputting to a Bus port) confirmed
  as the easier setup than external-Ableton routing for first listen.
- **Force-to-Scale + ChordMask stack productively.** Two complementary harmonic
  constraints — global scale clamp vs. local chord-context — layer cleanly without
  fighting each other. ChordMask narrows within whatever scale FTS has already
  imposed. *Implication: the v1 spine should make these stack-orderable explicitly
  (chord-mask before/after scale-force is a musical choice, not a mechanical one).
  Today's incidental ordering — FTS in `SEQ_CORE_Transpose`'s post-trim, ChordMask
  inside the playmode dispatch — happens to give the right feel; preserve when the
  spine migrates these into processors.*
- **Normal-track pitch gen (mono) — shipped + GO (2026-05-26).** The whole
  phase E–H generator stack (Turing loop, ENGAGE/ROLL/BOUNCE, ANCHOR/SNAP/MULT,
  LOCK, contour) generalized from drum-only to **normal (melodic) tracks**.
  Scope was "mono line first" (one generator → the track's single Note line =
  generative melody/bassline); per-voice polyphony deferred. **Confirmed
  musically real by ear** — a generative line on a normal track reads as a
  *line*, and the range dial sweeps detune-feel → leaps as predicted (§8 step 3
  finding transfers).
  - **As-built — it was nearly free.** The generator write path was already
    track-agnostic: it transcribes `loop[]` via `SEQ_PAR_Set(track, step,
    link_par_layer_note, instr, v)`, and `link_par_layer_note` is set on normal
    tracks too (the `seq_cc.c` link scan, last Note layer wins). The only
    blockers were **two `event_mode == SEQ_EVENT_MODE_Drum` guards** (in
    `SEQ_GENERATOR_Engage` and the static `write_loop_to`) — both replaced with
    the existing `link_par_layer_note >= 0` check. New UI helper `gen_instr()`
    in `seq_ui_trkpitchgen.c` returns `ui_selected_instrument` on drum tracks,
    `0` on normal (the single line; drum cursor would be stale). LCD row 0 reads
    `Trk N Note` on normal vs `Trk N.D nn` on drum. The relocate / find-empty-
    drum BOUNCE branches never fire on a normal track (only instr 0 exists), so
    BOUNCE is in-place freeze there — correct for a single melodic line.
  - **value-0 edge is already safe** — `normalize_range` clamps loop values to
    ≥1, so the generator never emits the note that means "no note" on a normal
    track (it would on drums fall back to `lay_const`).
  - **Sizing:** flash **+408 B**; CCMRAM and main RAM **unchanged** (no slot
    growth, no pool re-key, no new state). Harness **65/65 green** — drum path
    untouched.
  - **Deferred (post-listen refinements):** (1) **cursor-aware target** —
    ENGAGE into the Note layer named by `ui_selected_par_layer` if Note-type,
    instead of always `link_par_layer_note` (which picks the *last* Note layer,
    wrong on a hand-authored multi-voice chord track); squares with the
    cursor-wins principle from the F.3 auto-jump withdrawal. (2) **per-voice
    polyphony** — one generator per Note layer = generative chords/counterpoint
    on one track; needs the pool re-keyed from instrument→line-index. (3)
    **normal-track harness coverage** — the drum-init testctrl setup has no
    normal-track sibling yet; add a `CMD_TRACK_NORMAL_INIT` before pinning the
    normal path in the HIL suite.
- **Bounce unification — one lossless capture model (2026-05-30).** Two competing
  "bounce" mechanisms had grown up in parallel: (A) an **emission tape** —
  `seq_capture.c`, a 16 KB/16-track ring tapped on the MIDI hot path in
  `SEQ_CORE_ScheduleEvent`, replayed into a pattern slot — *lossy* (notes+vel+len
  only; CC/PB dropped; NoteOff-match heuristic length; grid-snapped onsets) and
  with an unsynchronized producer/consumer race on the ring walk; and (B) the
  **computed-output bounce** (`SEQ_GENERATOR_Bounce`, `SEQ_CORE_ProcessorBounce`,
  `…BounceRelocate`, `…ProcessorBounceCapture`) which freezes the rendered
  `OutputActive` (exact par/trg, CC included). **Decided: unify on B, delete A
  entirely.** Rationale: the thing the user actually wants to capture — a
  *generative sequence* — lives losslessly in `OutputActive`, already computed
  each tick; the tape's only unique catch (echo/transpose baked at emission) it
  loses anyway by grid-snapping, and it carried the race + the RAM + the hot-path
  branch.
  - **Engine:** one shared primitive `SEQ_CORE_CaptureTrackOutput(track, par_dst,
    trg_dst)` forces a full *quiet* render (sweep-safe — see below) then snapshots
    the post-processor par/trg. Two verbs on it: `SEQ_CORE_CaptureToSlot` (reuses
    the proven snapshot/restore-source skeleton, sourced from the primitive instead
    of the ring) and `SEQ_CORE_CaptureToTrack` (mirrors COPY/PASTE-TRACK: inherits
    src event-mode/geometry/lower-48 CCs onto dst, so the raw layer copy reads back
    correctly across mismatched configs — the decisive correctness item).
  - **Sweep-staleness bug fixed by construction.** The old in-place
    `SEQ_CORE_ProcessorBounce` read `OutputActive` without forcing a render — in
    sweep regime only a ~4-step slice is fresh, so a bounce right after a dial touch
    froze a partially-stale buffer. Routing it through the primitive (which
    force-renders) eliminates the bug; the cross-track path already had the guard.
  - **GP8 collapsed to in-place freeze only** (gen freeze / processor freeze /
    guidance). The cursor-aware *auto-find* relocate + cross-track-capture branches
    were deleted — they guessed a destination, which violates the
    deliberate-destination / no-smart-default rule (see the F.3 auto-jump
    withdrawal). Destination captures are now explicit gestures.
  - **Trigger: the PATTERN-hold gesture** (midiphy V4+ layout, confirmed +
    refined with the user, by-ear). **Source = visible track**; while PATTERN is
    held you pick a destination **slot** via the top row (**GP1-8 = group A-H**,
    default current; **GP9-16 = pattern number → COMMITs**, persisted to SD), and
    optionally a destination **track within that slot** via the **lower select
    row** (the 16 `DirectTrack` buttons stash a dst track). On the GP9-16 commit:
    **always** `SEQ_CORE_CaptureToSlotTrack(src, dstTrk, bank, pat)` — dstTrk is the
    select-row pick, or the **source's own track index** if none was picked (render
    source into just that track of the slot, other slot tracks preserved, **saved to
    SD**). One consistent operation for every cell (same/different track,
    same/different group). Bare PATTERN tap → Pattern page. This is the arrangement-
    building move: render generative tracks into specific slots' tracks, persisted.
    - **Same-group save-only, cross-group auto-load (final, by-ear 2026-05-31).**
      One rule decides what the commit does, steered purely by which destination
      track you aim at:
      - **Destination in the SOURCE's own group** (incl. the default dst==src track):
        **save only, no load.** The capture is merged into the chosen pattern on SD;
        the source/generator keeps playing, untouched. The variation-library move —
        *tweak → bounce → tweak → bounce*, banking variations without disturbing what
        you're tweaking; select one deliberately to play it.
      - **Destination in a DIFFERENT group:** the capture is written into the
        **destination group's OWN bank**, then that group is auto-loaded
        (`SEQ_PATTERN_Change` force=1) **and re-synched to the next measure**
        (`SEQ_CORE_ManualSynchToMeasure`) so the merged capture drops in **on the bar,
        locked**, immediately — for auditioning a variation in a spare group, or
        building a multitimbral canvas (each canvas track can carry its own
        port/channel; capture leaves CCs ≥0x46 alone). The source group is never the
        one loaded, so it is never jumped.
      Reached by ear, each step a real bug found by playing: (1) auto-load-everywhere
      clobbered the live source in the same-group cell; (2) never-auto-load-everywhere
      (an AskUserQuestion pick) fixed the clobber but left cross-group bounces invisible
      — the capture lands in a pattern you must then manually load, breaking "the act of
      bouncing should merge AND load"; (3) re-added auto-load **only when dst group ≠ src
      group** (can't clobber the source); (4) cross-group then loaded but stayed **silent
      until a manual switch** — the immediate (force=1) load skips the boundary handler's
      track-position restart, so the dst tracks kept the prior pattern's stale step phase
      (`FIRST_CLK=0`) and never fired; fixed with `ManualSynchToMeasure` (bar-aligned
      restart, mirroring `SEQ_SONG_FetchHlp_PatternChange`); (5) cross-group capture was
      **lost on switch-away-and-back** — written to the *source's* bank, but each group
      navigates only its own dedicated bank (bank-change `#if 0`'d, dedicated bank = group
      index), so it was stranded unreachable; fixed by addressing the write+load in
      `seq_pattern[dst_group].bank`. Bugs (4) and (5) were each pinned by an adversarial
      source-trace workflow before fixing. Pinned by `test_bounce_gesture_{lands_in_
      selected_slot_and_persists, cross_group_auto_loads_destination, same_group_leaves_
      source_live, cross_group_persists_in_dst_bank}` + the button_state straddle test.
    - **Why CaptureToSlotTrack and not the RAM-only CaptureToTrack:** found by
      ear — `CaptureToTrack` writes a track's *live RAM* only; switching that
      group's pattern away **loses it** (not on SD). `CaptureToSlotTrack` does a
      read-modify-write of the target slot file (reads it into the dst group with
      `PatternRead(remix_map=0)`, replaces track N, `PatternWrite`s it back,
      restores the dst group's live RAM so playback isn't disturbed; src captured
      first in case it shares the dst group). +~6 KB main SRAM for the 4-track dst
      group snapshot; CCM unchanged. `CaptureToTrack` (RAM) is retained as an
      engine/testctrl verb but no longer on the gesture. src==dst freeze is GP8.
      Robomold's bounce action removed (page kept). NOTE: still **no UNDO** for an
      overwrite (the dst slot's track N is overwritten on SD; UNDO is a follow-on).
  - **`num_measures` dropped** — a capability change, not just a refactor: the
    computed-output model captures one full rendered loop (the loop is the unit),
    which has no multi-measure-tape analogue. Correct for a loop instrument.
  - **No `CaptureToTrack` undo yet** — flagged: the deferred UI must snapshot dst
    before calling (UNDO is the live safety net; no global per-track undo covers an
    arbitrary dst overwrite today).
  - **Bugfix (2026-05-30, found by ear):** `SEQ_CC_ResetGenerativeForBounce` was
    clearing `trg_assignments.ALL = 0`, which wiped the **GATE** trigger-layer
    assignment too. A track with no gate-layer assignment plays *every* step
    (MBSEQ's gate-defaults-to-on), so bounced patterns fired a note on every step
    — gates appeared unset, CLEAR had no effect. The trigger assignments are
    **structural** (which layer is Gate/Accent/Glide/Roll/Skip), like the
    Note/Velocity/Length par-layer assignments the function already preserves, so
    only the genuinely-generative `random_gate`/`random_value` are now neutralized;
    the rest are preserved. HIL coverage extended (`test_capture_to_slot_preserves_
    gate_assignment`) — the prior round-trip test checked trigger-layer *bytes*
    (which round-tripped) but not the gate *assignment*, so it missed this.
  - **Sizing:** the 16 KB ring + hot-path tap removed from `.bss`/`SEQ_CORE_Tick`
    (replaced by ~1.3 KB of capture snapshot buffers); CCM unchanged at 52.9/64 KB
    (the ring was main-SRAM, not CCM). **Verified on device 2026-05-31: full HIL
    suite 73/73 green** (incl. the final bounce gesture — same-group save-only /
    cross-group auto-load, see below — slot-persistence, computed-output, and
    note/drum parity tests); by-ear confirmed.
- **Note vs drum tracks: bounce/render parity confirmed; the differences are
  essential, not bugs (2026-05-31).** Investigated "why do note and drum tracks
  perform differently" via a 4-subsystem adversarial map (data model / capture-bounce
  / render-processors / gesture-UI-harness). Result: **20 divergences — 16
  essential, 3 harness-only, 1 accidental.** The capture/bounce engine, the
  PATTERN-hold gesture, the render loop, and the processor stack are all
  **mode-agnostic** — note and drum tracks take the identical path, and a suspected
  cross-mode-bounce corruption was *refuted* on source inspection (geometry is
  inherited onto the dst before a fixed-size buffer copy, so it round-trips). The
  real divergences are **inherent to the data model** and correct as-is: a drum track
  is up to 16 one-shot instruments each with a constant note + a few par-layers; a
  melodic track is one voice with many expression layers (Note/Vel/Length/CC…).
  Unifying them would be wrong — it's what makes a kit a kit and a line a line.
  - **The one accidental divergence: accent velocity.** A note-track accent is
    hardcoded to `0x7f`; a drum accent reads a per-drum constant
    ([seq_core.c:2143-2149](../core/seq_core.c#L2143)). **User decided keep-127 is
    intended** (classic accent = full velocity for leads/bass) — left as-is, no
    follow-up.
  - **The perceived distinction was mostly a test-harness gap.** All bounce tests
    used drums because the harness could only read drum par-layers (the drum-only
    `cmd_track_drum_par_get/set` via `link_par_layer_note`). Closed by adding a
    general mode-agnostic par read/write verb (`CMD_TRACK_PAR_GET/SET` →
    `SEQ_PAR_Get/Set`, layer-explicit, no event-mode gate) + `board.track_par_get/set`
    + `test_note_track_bounce_matches_drum`, which **proves** a melodic-track bounce
    is byte-identical to a drum bounce rather than asserting it. (This is the §6
    "normal vs drum" tradeoff confirmed from the bounce side: the spine is
    track-agnostic; only the data model differs.)
- **Freeze faithfulness: split the generation axis from the shaping axis; full-config
  inherit for the slot-track gesture (2026-06-07, found + fixed by ear, HIL 75/75).**
  Reported live: a drum beat with only Groove, frozen via the PATTERN-hold gesture, played
  back "groove off, pattern wrong, almost too fast." Two coupled root causes, both in the
  *destination* handling of a capture:
  - **Deterministic shaping was being stripped.** `SEQ_CC_ResetGenerativeForBounce` zeroed
    `groove_style`/`groove_value` alongside the genuinely-generative CCs. But groove is an
    EMISSION-time, DETERMINISTIC shaper (per-step swing/velocity/length keyed to step
    position; [seq_core.c:1869](../apps/sequencers/midibox_seq_v4/core/seq_core.c#L1869),
    [:2113](../apps/sequencers/midibox_seq_v4/core/seq_core.c#L2113)) — never baked into the
    captured `OutputActive`, so the *only* thing that re-applies the swing on playback is the
    preserved CC. Re-applying reproduces the sound exactly (incl. groove's negative timing
    delays, which can't be baked into per-step delay params). Fix: stop resetting groove. The
    reset now splits the **generation axis** (reset) from the **deterministic shaping axis**
    (preserve), mirroring the existing structural-trg-assignment carve-out.
  - **The slot-track gesture under-copied the source config.** `SEQ_CORE_CaptureToSlotTrack`
    (the PATTERN-hold verb) and `SEQ_CORE_CaptureToTrack` inherited only the **lower-48 CCs**
    (lay_const) + geometry — mirroring `PASTE_Track`'s *minimal* "avoid garbage" branch, NOT
    the faithful `PASTE_CLR_ALL` branch. **Length (0x4d), clock divider (0x4c), groove
    (0x52/0x53), and the trigger-layer assignments (0x60..0x68) all live above 0x2f**, so the
    frozen track kept the *destination slot's* defaults: wrong length/clock = "too fast",
    wrong gate assignment = "pattern wrong", no groove. Fix: copy the source's **full
    `0x00..0x7f` CC space** (mirror `PASTE_CLR_ALL`), then `ResetGenerativeForBounce` strips
    the generation axis. `CaptureToSlot` (the in-place testctrl verb) was already faithful (it
    snapshots the whole source `tcc`), which is why the first HIL groove test passed while the
    real gesture stayed broken — an adversarial **path-completeness audit** caught that gap
    before the user's ears had to.
  - **Reverses the earlier "cross-group capture leaves CCs ≥0x46 alone" sub-decision** (which
    aimed to let canvas tracks keep their own port/channel). Faithful reproduction won: the
    copy now also carries the source's MIDI port/channel. Re-routing for a multitimbral canvas
    is a manual follow-up; revisit if it bites.
  - **HIL:** `test_capture_to_slot_preserves_groove` (in-place verb) +
    `test_capture_to_slot_track_inherits_full_config` (gesture verb — pins length + groove;
    reproduced the bug on the pre-fix firmware, `cc_get(GROOVE_STYLE)==0`). Full suite
    **75/75 green**; by-ear confirmed — freeze reproduces the beat, and freezing *over the
    live pattern* is now sonically transparent (was the second half of the confusion).
  - **FORCE_SCALE — SHIPPED via BAKE, not flag-preserve (2026-06-07; code + HIL pins, by-ear
    PENDING the user's flash). User chose Option B.** A frozen force-to-scale track was playing its
    RAW off-scale notes: the snap is EMISSION-time (the tick, after transpose;
    [seq_core.c:2142](../apps/sequencers/midibox_seq_v4/core/seq_core.c#L2142)), NEVER in the captured
    `OutputActive`, and `ResetGenerativeForBounce`'s `trkmode_flags.ALL=0` cleared FORCE_SCALE. Two
    faithful options: (A) preserve the flag and re-snap LIVE — cheap, but a later global-key change
    would silently re-pitch the frozen copy; or (B) **bake the heard pitch into the captured notes**
    so the flag can stay reset (immune to later key changes). User chose **B** (the §9 mandate: a
    frozen copy must sound like what was heard, full stop). `SEQ_CORE_BakeForceScale` (new) snaps each
    non-drum Note par-layer value to the per-step resolved scale/root, reproducing the DETERMINISTIC
    pitch chain `noteLimit(forceScale(transpose(raw)))` — transpose reproduced only when it was
    deterministic (Normal playmode, global transpose off), and the post-snap note **limit** baked too
    (both are zeroed by `ResetGenerativeForBounce`, so the heard offset + register-fold must be
    committed; note `SEQ_CORE_Limit` octave-FOLDS via `SEQ_CORE_TrimNote`, it does not min/max clamp).
    Called in all three freeze verbs after the captured notes land, before the reset.
    - **Two gotchas a two-pass adversarial review caught (both fixed before any by-ear):**
      (1) `SEQ_PAR_Get` reads the double-buffered **output mirror** while `SEQ_PAR_Set` writes the
      **source** ([seq_par.c:258](../apps/sequencers/midibox_seq_v4/core/seq_par.c#L258) vs
      [:285](../apps/sequencers/midibox_seq_v4/core/seq_par.c#L285)) — so the bake must read AND write
      `seq_par_layer_value` (source) DIRECTLY; a freshly-written capture destination's mirror is stale
      until the next render. (2) emission applies `SEQ_CORE_Limit` AFTER the snap — omitting it let
      out-of-range notes into the frozen copy.
    - **Scope (documented limits):** drum tracks (a drum note can fall back to a shared per-drum
      CONSTANT — per-step scale would snap it to different notes, unbakeable) and Chord par-layers
      (value is a chord INDEX, not a note) are NOT baked. Transpose for NON-force-scaled tracks is
      still its own deferred item (here transpose is only reproduced as part of a force-scaled note's
      heard pitch). Random/generative shapers stay reset (groove is preserved-as-CC; humanize/LFO/
      robotize/probability/random-gate/morph re-apply would diverge).
    - **HIL:** `tests/apps/seq_v4/test_capture_force_scale.py` — gesture + in-place verbs assert the
      C-Major snaps AND that FORCE_SCALE is RESET on the copy (proving bake-not-preserve); a third pins
      the snap→limit chain. New `CMD_GLOBAL_SCALE_SET` testctrl verb pins a deterministic key
      (`board.reset` doesn't touch the global scale).
  - **Deferred — the shaping-axis pass continues:** transpose (for non-force-scaled tracks),
    echo/LFO/direction, each confirmed by ear. Random/generative effects (robotize, humanize,
    probability, random-gate, echo Rnd modes, morph — its global `morph_value` isn't captured) stay
    reset; re-applying would diverge. The two-faces posture-persistence model (v3 format) and the
    unified four-move destination gesture remain the larger deferred pieces.
- **§8 step 5 (the spine) is broken into phases A–F.** User agreed 2026-05-24:
  ship the full design-doc spine as a multi-PR sequence, infrastructure first.
  Each phase is a buildable+harness-testable unit.
  - **A: Output buffer + identity renderer. ✓ DONE 2026-05-25.** Per-track
    output par/trg buffer (CPU-only, CCM-placed via section attribute);
    per-track render-dirty flag; synchronous identity renderer (output =
    source on dirty). Switch tick reads from source-direct to
    output-via-renderer. Single-buffer in phase A; double-buffering arrives in
    phase D. *Decided sub-questions:* `CCM_SECTION __attribute__((section
    (".bss_ccm")))` macro added in `mios32_config.h` (mirrors `AHB_SECTION`
    pattern). Renderer trigger = **tick-prologue batch** (one
    `SEQ_CORE_RenderTracks()` call at the top of `SEQ_CORE_Tick`, renders any
    dirty track via identity memcpy). RAM cost measured: **+20.0 KB in CCM
    exactly** (1024 par + 256 trg × 16 tracks = 20480 bytes), main RAM 95.9
    KB unchanged. Harness 20/21 pass — `test_polyrhythm_3_in_8` baseline
    flake remains the only failure; `test_datawheel_changes_step_value`
    actually started passing on this run, so net zero new failures.
  - **B: Processor stack scaffolding. ✓ DONE 2026-05-25.** `seq_processor_slot_t`
    (id, enabled, strength, bus); per-track 4-slot stack array, CCM-placed
    alongside the phase A output mirrors (+256 B exactly = 16 × 4 × 4). Renderer
    iterates after the identity copy; empty slots short-circuit via
    `id == NONE || !enabled`, so observable behavior is identical to phase A.
    Explicit zero-init loop in `SEQ_CORE_Init` is load-bearing — without it
    `-fdata-sections` + constant-prop strips the bss section while the loop
    has no real readers/writers. Harness 21/21 unchanged. Phase C replaces
    the `continue` with a `switch(p->id)` dispatch.
  - **C: ChordMask migration. ✓ DONE 2026-05-25.** `SEQ_PROCESSOR_ID_CHORD_MASK`
    + `chord_mask_render()` added; `SEQ_CORE_RenderTrack`'s phase-B `continue`
    replaced with `switch(p->id)` dispatch. The processor rewrites note-bearing
    bytes in the output par buffer (drum: the one `link_par_layer_note` layer
    across all drums; normal: every `SEQ_PAR_Type_Note` layer), skipping
    zero-valued bytes so the §9 drum-lay_const fallback survives. Snap
    algorithm (probabilistic gate at strength, nearest-PC outward search,
    lower wins on tie) is byte-identical to the removed `SEQ_CORE_Transpose`
    branch. *Decided sub-questions:*
    - **Live-chord update semantics:** per-tick implicit dirty for any track
      carrying an enabled chord_mask slot, inside `SEQ_CORE_RenderTracks`.
      Cheap brute-force (1.25 KB memcpy + walking only note-bearing layers ≤
      ~16 × 32 bytes per track); phase D's sweep/quiet detection will
      optimize. Bus-dirty-signal approach considered and rejected for phase C
      (would touch `seq_midi_in.c` push/pop paths; overkill until measured
      need).
    - **TRKMODE UX migration:** kept as a shortcut → slot bridge.
      `SEQ_CORE_ChordMaskSlotSync(track)` mirrors slot 0 from
      `tcc->playmode` + `tcc->chordmask_strength` + `tcc->busasg.bus`; called
      from `SEQ_CC_Set` on `SEQ_CC_MODE`, `SEQ_CC_CHORDMASK_STRENGTH`,
      `SEQ_CC_BUSASG`. v2 pattern persistence is unchanged (tcc is the
      persistent truth; slot is runtime mirror). The known-musical TRKMODE
      page + GP7 strength dial work exactly as before. Stack-editor UI
      deferred to a later phase.
    - **Slot 0 convention:** chord_mask is the only processor in phase C, so
      slot 0 is its conventional home. The proper allocator arrives with
      phase E's generator pool.
    RAM unchanged from phase B (CCM 20.25 KB, main 95.9 KB) — phase C added
    only code. Harness 21/21 against phase C firmware on hardware.
  - **D: Sweeping regime + double-buffering.** Per-track "knob-quiet" timestamp;
    within 50ms of last change → render current step + small lookahead live,
    bypassing cache. After 50ms quiet → background render to inactive buffer
    half + atomic swap at tick boundary. RAM cost: +1.25KB/track for the
    second buffer half (~20KB across 16 tracks).
    - **D.0: MSP stack-paint. ✓ DONE 2026-05-25.** §10 gating measurement.
      `SEQ_CORE_MSPPaint()` runs at the top of `APP_Init` (smallest live
      frame = maximal painted extent); fills 32 544 B of free MSP region with
      sentinel `0xA5A5A5A5`. Readback via `SEQ_CORE_MSPHighWaterBytes()`
      exposed through `CMD_MSP_QUERY` (testctrl 0x59) + `board.msp_query()`.
      Peak after full harness: **592 B** (~2 % of region). §10 MSP-gating
      concern closed favorably; phase D's +20 KB lands in CCMRAM, never
      competed with MSP. *Decided sub-questions:* knob-moving detection =
      per-track "last touched" timestamp on slot-relevant `SEQ_CC_Set` fields
      (D.1); render scheduling = tick prologue + sweep-window live render,
      defer FreeRTOS background task until §A4 #5/#6 measure a real miss
      (D.2/D.3).
    - **D.1: Per-track touched timestamp. ✓ DONE 2026-05-25.**
      `seq_render_touched_ms[track]` bumped by `SEQ_CORE_RenderTouched()`,
      which `SEQ_CORE_ChordMaskSlotSync` calls on every slot-relevant CC
      write (MODE / CHORDMASK_STRENGTH / BUSASG). `SEQ_CORE_RenderSweeping()`
      returns 1 if the last touch was within `SEQ_RENDER_SWEEP_MS = 50` ms,
      using `MIOS32_TIMESTAMP_GetDelay()` for wrap-safe arithmetic. +64 B bss
      (16 × u32), zero new CCM. Future processors will use the same helper.
    - **D.2: Sweep-window live render. ✓ DONE 2026-05-25.** During sweep
      (touch < 50 ms), the renderer copies only `[current_step,
      current_step + 4)` from source → active output across every par/trg
      layer, then runs the processor stack on the same slice. Dirty stays
      set so each subsequent tick re-renders the slice until the touched
      timestamp expires. Lookahead constant (`SEQ_RENDER_SWEEP_LOOKAHEAD = 4`)
      chosen so a 50 ms sweep at 96 BPM 32nds covers ~4 ticks of playhead
      travel — the playhead never reads a stale step within the sweep
      window. `chord_mask_render_range()` is the shared body; the quiet
      path passes `(0, num_steps)` for the whole buffer.
    - **D.3: Double-buffered output. ✓ DONE 2026-05-25.** `seq_par_output_value`
      and `seq_trg_output_value` gained an outer `[2]` half-buffer index;
      `seq_render_active_buf[track]` (1 byte/track) selects which half the
      tick reads via `SEQ_PAR_OutputActive()/SEQ_TRG_OutputActive()` inline
      accessors. Quiet render writes the inactive half, then a single-byte
      XOR flips `active_buf` (atomic on Cortex-M, so the tick path never
      sees a half-rendered output). Sweep render writes to the active half
      directly (no flip — tearing during knob motion accepted; the playhead
      only reads the window we're actively maintaining). RAM cost: **+20 KB
      in CCMRAM exactly** (1024 par + 256 trg × 16 tracks × 1 extra half),
      bringing CCMRAM to 40.25 KB used / 23.75 KB free of 64 KB; +16 B main
      bss for `seq_render_active_buf`. Matches §A5 prediction. Harness 23/23
      unchanged against phase D firmware. *Cosmetic until phase E:* with
      synchronous render in tick prologue the flip is functionally redundant
      vs single-buffer, but the structure is now in place so a phase-E
      background renderer can fill the inactive half without colliding with
      the tick read. *Behavioral test deferral:* dedicated sweep ↔ quiet
      transition tests would need new testctrl probes (active_buf, regime);
      deferred until phase E generators give the distinction musically-
      observable consequences. The existing 23-test harness exercises both
      paths (any CC write enters sweep; ticks after 50 ms enter quiet).
  - **E: Generator workflow basics. ✓ DONE 2026-05-25.** Spine-form Turing pitch
    generator landed as a single phase (no E.0…E.x split). New module
    `seq_generator.{h,c}`: cap-64 static pool in CCMRAM (refuse-with-message on
    full; LRU deferred), per-(track, instrument) sparse index map, one-deep
    global auto-undo slot (full 1 KB par-buffer snapshot). PITCHGEN page rewritten
    onto the new API — GP1 ENGAGE toggle (LED mirrors engaged state), GP2 UNDO,
    GP3/GP4 range min/max encoders, GP5 mutation rate (0–127, sweepable; §2.3
    pass-through at 0 verified live). Per-measure-boundary mutate-then-rewrite
    runs from a new `SEQ_GENERATOR_Tick()` prologue *before* `SEQ_CORE_RenderTracks`
    in `SEQ_CORE_Tick`, using a per-track `last_seen_step` wrap detector (sentinel
    0xFF means "fire on first call so measure 1 carries mutation too"). Source
    mutation sets `seq_render_dirty[track]`; phase D's renderer picks it up in
    the same prologue. Initial ENGAGE seeds the loop with a fresh reroll and
    transcribes immediately so the pitched line is audible without waiting a
    measure. *Decided sub-questions:*
    - **Pool eviction:** refuse-with-message (UI prints "pool full"). LRU
      deferred until play behavior demands. Closes §10 phase-E sub-decision.
    - **DISENGAGE semantics:** stop mutating; source stays as last written.
      Slot stays allocated (loop survives DISENGAGE→ENGAGE), so re-engage
      without re-snapshotting undo. Matches §11 glossary.
    - **Auto-undo scope:** one-deep, global. Most recent first-time ENGAGE
      wins. UNDO restores the snapshot and disengages every generator on the
      restored track so source isn't immediately overwritten next measure.
    - **Background-render task: still deferred.** Per-measure rewrite is one
      ≤64-byte transcribe per engaged generator per measure — single-digit µs
      against the existing tick prologue budget. §A4 #5 will measure if a real
      worst case appears.
    - **Per-step LOCK / MULT / ANCHOR / SNAP / ROLL / contour shapes:**
      explicitly NOT allocated yet (build less, §2). §8 step 6 (the polish
      pass after phase F) grows the struct from 72 B → ~184 B per slot,
      bringing the pool toward §A5's 12 KB target as those fields land.
    RAM cost measured: **CCMRAM +5.76 KB** (pool 4608 B + index 256 B + undo
    slot 1026 B), bringing CCMRAM to 46.0 KB used / 18.0 KB free of 64 KB;
    **main RAM +~100 B** (`last_seen_step[16]` + alignment, well inside §A5
    envelope). Harness 23/23 unchanged against phase E firmware. Listen test
    confirmed live 2026-05-25 — Turing line on drum, mutation rate sweeps from
    locked to skittish, range narrow→wide gives detune-feel→leaps, UNDO
    restores cleanly.
  - **F: BOUNCE verb — done 2026-05-25.** PITCHGEN GP8, **cursor-aware**
    destination per §3 (extended over the F.1 in-place-only first cut after a
    listen-test revealed in-place silently overwrote the source pattern):
    - **In-place gen BOUNCE** (`SEQ_GENERATOR_Bounce`) — cursor IS on the
      engaged gen → freeze + free the pool slot. Source stays as last
      written; loop array discarded so next ENGAGE rerolls fresh. The §3
      "destination occupied → replace" branch.
    - **Relocating gen BOUNCE** (`SEQ_GENERATOR_BounceRelocate`) — cursor on
      an empty drum slot while a gen is engaged elsewhere on the visible
      track → restore src par-buffer from the global one-deep undo, then
      transcribe the gen's loop into the cursor's Note par-layer; free the
      gen slot and clear undo. The §3 "destination empty → additive" branch.
      User workflow: ENGAGE on a drum, listen, navigate cursor to empty
      drum, BOUNCE — original drum returns, the iteration lands on a fresh
      slot, user re-ENGAGEs for another variation.
      *Refuses with `-3` if the undo slot covers a different track* (one-deep
      undo, last first-ENGAGE wins — global, not per-gen). *Whole-track
      restore also disengages every other gen on the src track*: the undo
      can't preserve independent gens, so relocate is effectively a
      one-gen-per-track gesture.
    - **Processor BOUNCE** (`SEQ_CORE_ProcessorBounce`) — fallback when no
      gen is engaged on visible track and any slot is enabled. Copies
      active-output → source (par + trg), clears every slot, untangles the
      chord_mask tcc-mirror (`playmode → Normal`, `chordmask_strength → 0`)
      so the next `SEQ_CORE_ChordMaskSlotSync` does not re-arm slot 0.
      *Relocate semantic deferred for processor* — current behavior is
      in-place only (overwrites source). Phase F.3 candidate.
    - **Neither** → guidance message "nothing to bounce".

    Resolution order: cursor-engaged gen → other-gen-on-track relocate →
    processor → nothing. The "live overlay" composes on top of the "buffer
    transform"; bouncing the overlay first leaves the processor still
    bounceable.

    LCD row 1 RHS now switches contextually: "GP3/4=range … GP8=bnc" when
    cursor IS the gen, "GEN on Dnn  GP8 relocates here" when cursor parked
    on an empty drum while a gen is engaged elsewhere on the track, default
    hint otherwise.

    No new state added — phase F is code-only end-to-end; CCMRAM stays at
    46.0 KB used, main RAM unchanged.

    **Harness automation (added same session, 2026-05-25).** Three LCD-scrape
    tests in `tests/apps/seq_v4/test_pitchgen_bounce.py` pin the *dispatch*
    layer: in-place vs relocate vs stale-undo refuse. New testctrl commands
    `CMD_TRACK_DRUM_INIT` (0x5b — programmatic drum-mode track setup, no
    saved pattern required) and `CMD_UI_INSTR_SET` (0x5a — cursor parking).
    `SEQ_GENERATOR_Init(0)` now also runs in the test-state reset path so
    engaged generators don't leak between tests. Listen-test still owns the
    audible "src restored, dst plays loop" contract. Full harness **26/26
    green** against the F.2 firmware.

    **Latent bug fix in `seq_ui.c`** (surfaced by the harness tests).
    `ui_msg_ctr` counts down on the 1 ms periodic tick but did not request
    a display redraw when it hit 0, so popup-overlay text remained in the
    LCD buffer until the next time the page's LCD_Handler was otherwise
    scheduled. The neighboring `ui_hold_msg_ctr` already set
    `seq_ui_display_update_req = 1` on expiry; matched the pattern.
    Cosmetic in normal use (live users rarely notice); blocking for the
    harness's LCD-scrape assertions which read the LCD right after a
    popup nominally cleared. Pre-existing, predates this fork.

  - **F.3: ENGAGE auto-jump + cross-track processor capture — done
    2026-05-26.** The two phase-F deferred items, shipped together.
    - **ENGAGE auto-jump.** §3 "default destination = first empty legal
      layer in the current track." `SEQ_GENERATOR_FindFirstEmptyDrum`
      scanned drums 0..15 for "no engaged gen + Note par-layer all-zero";
      GP1 disengaged branch jumped `ui_selected_instrument` to the first
      empty before engaging when the cursor drum had Note content. Popup
      was `"ENGAGED on D 5"` vs plain `"ENGAGED"`.

      **Withdrawn 2026-05-26** during phase H listen-testing — the
      auto-jump fought the deliberate gesture "I want the gen on *this*
      drum" by silently landing elsewhere. The UNDO snapshot taken inside
      `SEQ_GENERATOR_Engage` *before* the first source write already
      protects against accidental overwrites, so the heuristic was net
      cost. Replaced with: GP1 always engages on the cursor drum, user
      pick always wins. `SEQ_GENERATOR_FindFirstEmptyDrum` removed; the
      auto-jump test file (`test_pitchgen_engage_autojump.py`) deleted.
      §3 "default = first empty" still applies in spirit — "default"
      means "absent a deliberate cursor placement," which we can't
      reliably detect, so deferring to the cursor is the right call.
    - **Cross-track processor capture.** `SEQ_CORE_ProcessorBounceCapture`
      copies src's post-processor output → dst's source par+trg, leaves
      src untouched (processor stack + tcc preserved). The §3 "empty
      target → additive" half of processor BOUNCE; symmetric with gen
      `BounceRelocate`. New GP8 dispatch branches: cursor on empty
      visible track + exactly one other track has an enabled processor
      → capture; multiple other-track processors → refuse "multi proc".
      LCD row-1 RHS: "Proc on T 3  GP8 captures here" hint when
      eligible. The capture function forces a quiet (full-buffer) render
      of both src (before copy, so it reads a complete `OutputActive`)
      and dst (after copy, so `SEQ_PAR_Get(dst)` returns the captured
      bytes through the output-mirror redirection §A2).

    **Harness automation (added same session).** 3 LCD/dispatch tests in
    `tests/apps/seq_v4/test_pitchgen_engage_autojump.py`; 3 dispatch +
    bytes-match tests in `test_processor_capture.py`. New testctrl
    commands: `CMD_UI_TRACK_SET` (0x5d — park visible track for cross-
    track tests), `CMD_TRACK_DRUM_PAR_SET` (0x5e — seed a drum's Note
    layer without engaging a gen), `CMD_TRACK_DRUM_PAR_GET` (0x5f —
    symmetric readback for bytes-match assertions). Hygiene: three
    existing testctrl commands (`UI_INSTR_SET`, `UI_TRACK_SET`,
    `TRACK_DRUM_PAR_SET`) now set `seq_ui_display_update_req = 1` so
    LCD scrapers see content changes within SETTLE rather than racing
    the ~250ms cursor-flash periodic. Full harness **44/44 green**
    against the F.3 firmware.

    No new persistent state — CCMRAM stays at 46.8 KB used / 64 KB,
    main RAM unchanged. Flash +1208 B (helpers + 3 testctrl commands +
    dispatch + LCD hint).

- **Step 6 — Generator polish (phase G). DONE 2026-05-25.** Per-step LOCK,
  ROLL gesture, mutation depth, contour shapes. Sub-phased G.0–G.4 but
  shipped as one commit since each piece was a single small wiring change
  on top of the phase E generator engine.
    - **G.0 slot growth.** `seq_generator_t` 72 → 84 B: 12 B header
      (added `mutation_depth`, `contour_shape`, 3 B reserved pad) + 64 B
      loop + 8 B `locks` bitmap. Pool: 64 slots × 12 B delta = **+768 B
      CCMRAM** (47.1 → 47.9 KB used / 64 KB; ~17.2 KB headroom).
      Defaults preserve phase E behavior exactly: `mutation_depth = 127`
      (full reroll), `contour_shape = UNIFORM`, all locks 0.
    - **G.1 ROLL gesture.** Per §9 line 1199, ROLL collapses into
      ENGAGE-while-engaged ROLL. New `SEQ_GENERATOR_Roll(track)` reroll-
      every-unlocked-step honors contour and ignores rate/depth (it's the
      "fire one variation now" trigger). GP1 dispatch:
        - disengaged   → ENGAGE (alloc + seed + snapshot undo)
        - engaged      → ROLL
        - SEL + GP1    → DISENGAGE (the rarely-used escape — live use
                         prefers BOUNCE-in-place or UNDO).
    - **G.2 mutation depth.** GP6 encoder, 0..127. Upgrades `mutate_loop`
      from pure-reroll to depth-controlled: 0 = no-op (frozen even at
      rate=127), 127 = full reroll (phase E behavior), in between =
      perturb existing value by ±depth semitones clamped to range.
      Two-dimensional control: rate = "how many steps touched per measure",
      depth = "how far each touched step moves". rate=high+depth=low gives
      shimmer; both high gives chaos; both low/zero gives a frozen loop.
    - **G.3 contour shapes.** GP7 encoder cycles UNIFORM / LOW_BIAS /
      HIGH_BIAS / TRIANGLE. Biases the *full-reroll* path only (depth=127
      or ROLL); perturb path ignores contour. Cheap distribution shaping
      via min/max/sum of two uniforms — no trig or tables. Audibly skews
      the loop distribution most when range is wide and ROLL is pressed
      a few times per shape.
    - **G.4 per-step LOCK.** Datawheel scrolls `ui_selected_step` cursor
      (clamped to LOOP_LEN-1=63). GP6 button-press toggles lock on the
      cursor step. `SEQ_GENERATOR_LockToggle(track, instr, step)`. Locks
      survive mutation in BOTH paths (perturb-while-mutate AND reroll
      from ROLL). Persist across DISENGAGE→ENGAGE (slot stays allocated).
      Cleared on slot recycle (BOUNCE-in-place, relocate, UNDO restoring
      to a different gen's track).
    - **LCD layout phase G.** Row 1 LHS: `Lo:C 2 Hi:C 6 R:008 D:127 Ct:Uni`
      (Lo/Hi = range, R = rate, D = depth, Ct = contour). Row 1 RHS when
      engaged: `Stp:NN [L]/[ ] GP6=LOCK SEL+GP1=disen`. Row 0 RHS:
      `GP1=ENGAGE/ROLL GP2=UNDO  GP8=BOUNCE`.
    - **As-built encoder layout.** GP1 ENGAGE/ROLL, GP2 UNDO, GP3/4 range
      min/max, GP5 rate, GP6 depth (button = LOCK toggle), GP7 contour,
      GP8 BOUNCE. Datawheel = step cursor. MULT/ANCHOR/SNAP NOT
      allocated at phase G — listen-test demand surfaced same session
      and they shipped as phase H below (GP3/GP4 buttons = ANCHOR/SNAP,
      GP7 button = MULT cycle).

    **Harness automation (added same session).** 7 LCD-scrape dispatch
    tests + 4 behavioral tests in `tests/apps/seq_v4/test_pitchgen_step6.py`.
    Dispatch covers ROLL/DISENGAGE/LOCK/cursor/depth/contour LCD effects.
    Behavioral tests use new `CMD_GENERATOR_QUERY` (0x5c) to read live
    `loop[64]` + `locks` bitmap + dial values from a slot, pinning: ROLL
    preserves locked steps, ROLL actually rerolls unlocked steps, locks
    survive DISENGAGE→ENGAGE, BOUNCE-in-place clears locks for the next
    slot. The new query returns 9 header bytes + pack7(72 raw bytes) =
    ~97 wire bytes; harness exposes it as `Board.generator_query() →
    GeneratorState | None`. Also added 1 regression test in
    `test_bounce.py` for the PATTERN-held + GP-letter + GP-number bounce
    gesture (seq_ui.c:568-616) — confirms the gesture-path dst lands on
    SD and survives a pattern-load to a different group. Full harness
    **38/38 green** against the G firmware.

    **Was: still listen-test only (deferred):** mutate-path LOCK
    preservation across actual measure-boundary mutation, depth=0
    freezing, contour distribution shape. **Closed by phase-H coverage
    pass (2026-05-26, commit `954af895`)** — see phase H "Coverage
    follow-on" below.

  - **H: ANCHOR / SNAP / MULT — done 2026-05-26.** The three §A3 dials
    that step 6 phase G left explicitly unallocated. Shipped as one
    follow-on commit on top of phase G (no listen-test gating between —
    they share the same slot growth and live in the same UI surface).
    - **Slot growth.** `seq_generator_t` 84 → 180 B: header gains
      `anchor_valid` (1 B; reserved pad shrinks 3→2 B), payload gains
      `anchor[64]` + `mult[32]` (packed 4-bit nibbles, low=even step,
      high=odd). Pool: 64 slots × 96 B delta = **+6.0 KB CCMRAM**
      (46.9 → 52.9 KB used / 64 KB; ~11.1 KB headroom).
    - **Auto-anchor at ENGAGE.** First-engage path copies seeded loop
      into `anchor[]` and stamps `anchor_valid = 1`, then memset's
      `mult[]` to `MULT_PACKED_DEFAULT` (0x22 = code 2 both nibbles =
      1× = no scaling). Re-engage on a disengaged slot leaves the
      anchor untouched (the captured identity survives DISENGAGE →
      ENGAGE). Slot recycle (BOUNCE, relocate, pool re-init) memset's
      everything to 0.
    - **SNAP gesture.** `SEQ_GENERATOR_Snap` restores `loop[]` from
      `anchor[]` and rewrites source so the snap is audible without
      waiting for the next wrap. Refuses with -2 if the slot has
      never been anchored (which only happens via a future code path
      that allocates without seeding — current code paths always
      anchor at seed). Does NOT disengage (orthogonal to mutation
      state, per §5.3). Bound to **GP4 button** on the PITCHGEN page.
    - **ANCHOR gesture.** `SEQ_GENERATOR_Anchor` copies current
      `loop[]` into `anchor[]` — refresh the captured identity to
      "what's playing right now." After ANCHOR, subsequent SNAPs
      return here, not to the original ENGAGE seed. Bound to **GP3
      button**.
    - **MULT per-step multiplier.** 4-bit code per step in
      `mult[32]`. Codes 0..3 cycled by **GP7 button** at the
      datawheel-cursor step: 0 = 0× (mute mutation), 1 = 0.5×,
      2 = 1× (default), 3 = 2×. Codes 4..15 reserved (treated as
      1× by the mutate path) so future expansion is non-breaking.
      Mutate threshold becomes `mult_threshold(rate, code)` —
      MULT=0 short-circuits via `continue`, MULT=3 saturates the
      threshold at 255. **ROLL ignores MULT** (matches the existing
      ROLL-ignores-rate-and-depth shape; ROLL is the on-demand
      override that bypasses gating).
    - **GP button layout (final for step 6).** GP1 ENGAGE/ROLL,
      GP2 UNDO, GP3 enc=range_min / btn=ANCHOR, GP4 enc=range_max /
      btn=SNAP, GP5 enc=rate, GP6 enc=depth / btn=LOCK, GP7
      enc=contour / btn=MULT cycle, GP8 BOUNCE. Datawheel = step
      cursor.
    - **LCD.** Row 0 RHS now reads
      `G1=EN/RL G2=UN G3=ANC G4=SNP G8=BNC`. Engaged-state row 1 RHS:
      `Stp:NN [L] M:1x   G6=LCK G7=MULT` — adds the per-step MULT
      label alongside the lock indicator at the cursor.

    **Harness automation (added same session).** 9 tests in
    `tests/apps/seq_v4/test_pitchgen_step6h.py`: 5 LCD-scrape
    dispatch tests (GP3/GP4/GP7 popups, GP4 / GP3 no-slot guidance,
    M:label LCD field flips through full 0→1→2→3→0 cycle) + 4
    behavioral tests via the extended `CMD_GENERATOR_QUERY`
    (auto-anchor + mult defaults at ENGAGE, SNAP restores loop[] to
    anchor bytes after ROLL, ANCHOR refresh changes the snap target,
    MULT cycle touches only the cursor step with packed-nibble
    addressing). `CMD_GENERATOR_QUERY` extends by 1 header byte
    (`anchor_valid`) + 32 raw bytes (`mult[]`) → 104 raw bytes
    pack7→120 wire; harness `GeneratorState` gains `anchor_valid:
    bool` and `mult: tuple[int, ...]`. Full harness **55/55 green**
    against the H firmware.

    **Sizing.** Flash +432 B (after F.3 auto-jump withdrawal, see
    below); CCMRAM **46.9 → 52.9 KB used / 64 KB**; main RAM unchanged.

  - **H follow-on: F.3 auto-jump withdrawn — done 2026-05-26.** Live
    phase-H testing surfaced that the F.3 ENGAGE auto-jump fought the
    deliberate gesture "engage on *this* drum" by silently relocating
    the cursor when the drum had Note content. UNDO already protects
    accidental overwrites (it snapshots inside `SEQ_GENERATOR_Engage`
    *before* the first source write), so the heuristic was net cost.
    Removed: `SEQ_GENERATOR_FindFirstEmptyDrum`, the auto-jump branch
    in PITCHGEN GP1, the dedicated auto-jump test file. See phase F.3
    entry above for the withdrawal note.

  - **H follow-on: coverage pass — done 2026-05-26 (commit
    `954af895`).** Closes the step-6 "still listen-test only" gaps
    that phase G and the initial phase H entry left open. The
    production mutate path only fires on track-wrap, and ROLL bypasses
    rate/depth/MULT — so the rate-gated mutate contracts were
    previously faith-based. Three small testctrl commands expose the
    math deterministically:
    - `CMD_GENERATOR_TICK_FORCE` (0x60) — synchronous mutate cycle on
      a target slot, equivalent to one measure boundary.
    - `CMD_GENERATOR_DIAL_SET`   (0x61) — set range_min/max/rate/depth/
      contour to exact values without spamming encoder events through
      `SEQ_UI_Var8_Inc`.
    - `CMD_GENERATOR_MULT_SET`   (0x62) — stamp any 4-bit MULT code at
      a specific step, bypassing the GP7 cycle gesture.

    Public helper `SEQ_GENERATOR_ForceMutate` exposes `mutate_loop` +
    `write_loop_to_source` for the harness without making the static
    helper itself public.

    13 new tests in two files:
    - `test_pitchgen_mutate_path.py` (9): rate=0 pass-through, depth=0
      freezes (the §2.3 sweep contract), LOCK survives real measure-
      boundary mutation (vs phase-G's ROLL-only coverage), MULT=0
      freezes a step across 15 cycles (both even/low-nibble and odd/
      high-nibble packed-mult paths), MULT=3 mutates harder than
      MULT=2 (statistical 30 cycles), UNIFORM ≈ flat thirds, TRIANGLE
      peaks at mid, LOW/HIGH skew.
    - `test_pitchgen_lifecycle.py` (4): anchor + MULT survive
      DISENGAGE→re-ENGAGE (SNAP after the round-trip returns to the
      original auto-anchor, not the rolled state); anchor + MULT
      cleared on BOUNCE-in-place slot recycle; end-to-end techno-bass
      workflow ENGAGE → MULT pattern → force-mutate (frozen stay,
      wild drift) → ROLL bypasses MULT → SNAP restores anchor +
      preserves MULT → BOUNCE → source par holds snapped bytes.

    Full harness **65/65 green** (52 baseline + 13 new). Flash +368 B
    for the three new testctrl commands; no CCMRAM or main-RAM change.

- **Step 5 build discipline.** Each phase ships its own harness regression test;
  the §A4 timing tests (#3 same-tick, #4 knob-to-sound, #5 worst-case tick,
  #6 priority-inversion) attach progressively as phases land. Phase A passes
  iff the existing harness still passes unchanged (no behavior change). RAM
  budget tracked at each phase: phase A baseline + 21KB single-buffered output
  is acceptable; phase D's double-buffering adds another ~20KB in CCMRAM — verify
  against measured base (§A5) at each step. *MSP high-water measured 2026-05-25
  (phase D.0): peak 592 B vs 32 KB free region — closed and favorable (§A5/§10).*
- **Pattern format extends to v3** (versioned in-line, backward-compatible) to carry
  processor + generator posture.
- **Build order is music-first** (§8): hand-authored melody → throwaway generator →
  GO/NO-GO → mask spike → *then* the render-cache spine. The cache is **not** step 1.
  (Corrects an earlier infrastructure-first ordering.)
- **Bus chord-context plumbing verified in-fork (2026-05-24).** Transposer stack is
  PUSH_TOP/unordered (irrelevant for a PC-set mask), cap 10, full cluster accumulates;
  a free pitch-sorted view exists in the ARP_SORTED stack for v2 voice-leading. The
  two-round same-tick contract is intact — robotize-loop changes are a pre-round
  measure pass, not interposed. §8 step 4 is now confirmation, not discovery.
- **Base SRAM measured (2026-05-24, MBSEQV4P at `3d144ab2`).** Main RAM 95.9 KB used /
  ~32 KB free; CCMRAM **0 KB used / 64 KB free**. §A5 threshold (*fits iff base ≤
  ~104 KB*) met, and ~56 KB of existing CPU-only buffers (`ucHeap`, par/trg layers,
  capture ring) are CCM-relocation candidates if pressure emerges. §8 step 0 closed;
  budget moves from *unknown* to *base-verified*. Honest read: **first build fits
  trivially; full Part II fits but tight** (~8 KB static before the now-gating MSP
  high-water, §10). Numbers/tables in §A5.

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

---

## 10. Open questions (unresolved forks)

Closed this round and removed: sampler slot model, windowing playback, render-cache
design (now §A2, provisional), set-density shape (now §5 skeleton/muscle). §8 steps
1–4 all closed by ear 2026-05-24 (see §9).

**Gating (answer before/at the start of the relevant phase)**
- **Base SRAM — measured 2026-05-24 (CLOSED):** 95.9KB used / ~32KB free main, 64KB
  CCM virgin. First build fits trivially; full Part II fits but tight (~8KB static).
  See §A5.
- **MSP/handler-stack high-water — measured 2026-05-25 (CLOSED).** Phase D.0:
  sentinel paint of free MSP region from `APP_Init`, readback via `CMD_MSP_QUERY`.
  After full harness exercise: **peak 592 B** against **~32 KB free region** (~2 %
  usage; ≈ 32 KB headroom). Phase D's +20 KB double-buffer lands in **CCMRAM**, so
  it never competed with MSP anyway — separate budget. Numbers + table in §A5.
- **Max generator loop length** for static allocation — a real capability-vs-budget
  decision (§A5). Recommended v1 default: 64-step cap with tiling across longer
  tracks; revisit if a piece wants longer. Decides at step 5 phase E.

**Step 5 sub-decisions (answer as the relevant phase lands)**
- **CCM placement mechanism — CLOSED 2026-05-25 (phase A).** `CCM_SECTION
  __attribute__((section(".bss_ccm")))` macro added in `mios32_config.h`,
  guarded on `MIOS32_FAMILY_STM32F4xx` (no-op fallback elsewhere). Mirrors
  the existing `AHB_SECTION` pattern. Linker `.bss_ccm` / `.bss_ccm.*`
  sections in `etc/ld/STM32F4xx/STM32F407VG.ld` route to the 64 KB CCMRAM
  region.
- **Render task scheduling — CLOSED 2026-05-25 (phase D).** Tick prologue +
  sweep-window live render. No new FreeRTOS task. `SEQ_CORE_RenderTracks` in
  the tick prologue picks sweep vs quiet per track based on touched timestamp.
  FreeRTOS background task deferred until §A4 #5/#6 measure a real worst-case
  miss; today's chord_mask render is single-digit µs so a background task
  would buy nothing.
- **Knob-moving detection — CLOSED 2026-05-25 (phase D).** Per-track
  `seq_render_touched_ms` timestamp. `SEQ_CC_Set` cases for slot-relevant
  fields call `SEQ_CORE_RenderTouched()` via `SEQ_CORE_ChordMaskSlotSync`.
  Cheaper than encoder-input hooking; doesn't depend on hardware specifics;
  generalizes to future processor params.
- **Per-step vs whole-buffer cache invalidation** (phase D): whole-buffer default
  is fine; window position is the incremental special case (deferred to v2 windowing).
- **TRKMODE ChordMask UX migration — CLOSED 2026-05-25 (phase C).** Shortcut
  preserved: `SEQ_CC_Set` calls `SEQ_CORE_ChordMaskSlotSync` for MODE /
  CHORDMASK_STRENGTH / BUSASG. v2 pattern format unchanged; stack-editor UI
  deferred until more processors exist.
- **Generator pool allocation strategy — CLOSED 2026-05-25 (phase E).** Static
  cap-64 pool, per-(track, instrument) sparse index map (16×16 byte table, 0xFF
  = unallocated). Eviction policy: **refuse-with-message** on full ("pool full
  (64/64)" via `SEQ_UI_Msg`). LRU deferred — not needed yet (the spine ships
  with 16 max tracks × at-most-16 drums per track = 256 possible (track, instr)
  pairs, but a realistic live session engages 4–8; pool ceiling is far above
  observed use). Revisit only if play behavior demands.
- **BOUNCE destination semantic — CLOSED 2026-05-25 (phase F).** BOUNCE
  honors §3: cursor IS the destination. Cursor on engaged gen → in-place
  (replace). Cursor on empty drum slot while a gen is engaged elsewhere on
  track → relocate (additive at dst, src restored from whole-track undo).
  See §8 step 5 phase F for the resolution order and the one-deep-undo
  caveat (relocate disengages every other gen on the src track).

**Closed phase F.3 (2026-05-26)**
- **Processor BOUNCE cross-track capture — CLOSED.** Reframed: §3
  ¶176-178 actually wants in-place processor bounce to BE destructive
  ("the processed notes *are* the buffer"). What was missing was the
  symmetric *additive* half — capture src's output into an empty
  *other* track's source. Wired as `SEQ_CORE_ProcessorBounceCapture`
  with a visible-track + count-other-track-processors dispatch in the
  PITCHGEN GP8 handler; refuses if multiple other-track processors
  (ambiguous). Same-track BOUNCE retains the existing in-place /
  replace semantic.
- **ENGAGE destination auto-jump — CLOSED.** `SEQ_GENERATOR_FindFirstEmptyDrum`
  + a GP1-disengaged-branch check in `seq_ui_trkpitchgen.c`. Cursor
  jumps if its drum has any non-zero Note step AND no allocated slot.
  Confirmed live + bytewise-test green; closes §10's "ENGAGE destination
  auto-jump" item.

**Open bugs (pre-existing, surfaced 2026-05-25)**
- **`SEQ_CC_CHORDMASK_STRENGTH = 0x96` is outside the v2 persisted ext-CC range
  0x80–0x95** ([seq_file_b.c:62-64](../apps/sequencers/midibox_seq_v4/core/seq_file_b.c#L62-L64)).
  Consequence: chord-mask strength resets to 0 on every reboot or pattern
  reload — playmode persists but the dial does not, so the user sees
  TRKMODE=ChordMask with Msk:000 after a flash. Predates phase C (regression
  goes back to when the CC was added 2026-05-24). Fix is small: extend
  `SEQ_FILE_B_TRK_EXT_CC_LAST` to 0x96 (or the next clean boundary) and bump
  the v2 ext-tag — but format-bumps are a v3-format concern, so park here
  until the v3 format work lands.
  **Update 2026-06-09 — un-parked, promoted into the Tension Workbench bundle**
  (§8 second build): the bug is wider than recorded — CCs **0x96–0x99** (strength,
  bus, drum-mask L/H) all sit past the persisted range and reset on reload. The
  small ext-range bump (new ext tag, range to a clean boundary) ships with the
  workbench, independent of the larger v3 format; GRIP's CC lands inside it.
  **CLOSED 2026-06-10 — shipped with the Workbench.** A versioned **V3 ext-tag**, range
  widened to 0x80–0x9F (covers chord-mask 0x96–0x99 + GRIP 0x9A), with the V2 byte-count
  frozen separately so old V2 patterns still load. HIL round-trip pins it; chord-mask
  strength + GRIP now survive reboot/recall.

**Tension Workbench — BUILT + by-ear GO (2026-06-10).** Track 1 shipped as one bundle,
HIL-green, confirmed by ear. Decisions that emerged or refined the 2026-06-09 plan
(build narrative → REFERENCE doc):
- **Monotone pull, varied push.** The grip hash keys on `zone` only on the PUSH side
  (LEAN/RUB/SLIP each select a different tense set — the §2.3 "variety"); the PULL side
  collapses all zones to one grip class, so deeper pull only ADDS gripped voices and
  the band nests — pulling harder can never pop a voice back outward. Resolves the
  latent §2.2-monotone-vs-§2.3-variety tension in favor of what the ear expects
  (§8.1 "collapse, not dropout").
- **GRIP is per-track (CC 0x9A, mirrored into the slot's `strength`); it shares the
  chord-context bus + drum scope with chord-mask** — no separate tension-bus CC.
  GRAVITY/SHADE stay global (config-persisted, like the scale — performance state,
  not pattern state).
- **RESOLVE = per-tick glide sized to the ticks remaining to the next downbeat,
  pinned to exactly 0 in the `ref_step==0` branch; instant when the transport is
  stopped; a manual GRAVITY turn cancels it.** (Not the `SEQ_GENERATOR_Tick` pattern
  the plan named — that fires on per-track wrap, not the global musical measure.)
- **ext-CC fix shipped as a versioned V3 tag**, range widened to 0x80–0x9F, with the
  V2 byte-count *frozen separately* so old V2 patterns still load. Existing bank slots
  already had room for the wider block (layer data dominates `pattern_size`), so no
  bank re-format was needed.
- **Encoders have no felt detent (raised by the user): 0 is a value, not a position.**
  So RESOLVE (reliable return) and a bipolar LCD position meter (fill-from-center +
  zone-boundary ticks) are how you find home — the meter is the encoder-native
  feedback the gesture needs. Also clarifies the model: within a zone the dial
  *continuously* recruits voices (threshold), across boundaries the target band
  *steps* — not binary.
- **Throughput: a host rig-builder library + CLI** (`python -m harness.rigs tension`,
  one command after a flash) + a `pattern_save` verb — the §1 "setup friction kills
  ear-testing" answer; also the trap that masked the first listen (a rig built on a
  multi-layer session buried the gripped voice — fixed by loading a clean baseline).
- **The GO is soft:** it works; no refinement requested. The §10 by-ear tunables (zone
  widths, SHADE terrain, depth-vs-grip split) shipped at their starting values and are
  revisited only when the ear asks. **Track 2 (pitch-chain migration) is licensed.**

**Open puzzles (surfaced 2026-05-25, not gating, revisit with a clean baseline)**
- **EDIT-LCD vs tick-time gate read disconnect.** While diagnosing the
  `test_polyrhythm_3_in_8` baseline failure, the EDIT page LCD showed a
  correctly-sparse 3-of-8 gate trigger pattern after polyrhythm regen (gates
  at steps 3, 6, 8, 11, 14, 16) — but playback emitted NoteOns on **every**
  step at ~107 ms intervals. Both code paths use `SEQ_TRG_GateGet` →
  `SEQ_TRG_Get`, which after phase A reads `seq_trg_output_value` (the
  rendered mirror). If they read from the same place, they should agree.
  Reproducible across multiple runs; root cause not found. Proximate
  trigger appears to be multi-Note-layer par content on this device (the
  user's loaded pattern A1:1 has A1, A5–A8 all Note-typed with values at
  every step), which the test-session rig (`doc/plans/2026-05-25-test-
  session-rig.md`) will remove — but the LCD-vs-tick disagreement itself
  hints at something deeper (a parallel gate path? render-cache race? read
  from `seq_trg_layer_value` somewhere I missed?). Investigate once
  `AUTO_TEST` baselines exist and we can A/B a clean state against the
  current broken one.

**Tension Workbench (decide at the workbench, by ear — 2026-06-09)**
*Soft-resolved 2026-06-10: the by-ear GO confirmed the bundle works at these starting
values — nothing below needs changing yet; each stays a live dial, revisited when the
ear asks. Already resolved outright by the build: chord-root = ARP_SORTED lowest held
note (fallback global root); GRAVITY persistence = global config (performance) state;
GP map = GP1 GRAVITY / GP2 SHADE / GP3 GRIP / GP8 RESOLVE / GP16↔FX_SCALE (provisional);
SHADE = parallel modes. New from the build: a **depth-vs-grip split** (dial magnitude
currently drives both how-far and how-many — split only if the ear wants "few notes
very tense" vs "all notes slightly tense" as separate moves).*
- Zone boundaries/count on the bipolar sweep (proposed DRONE/CHORD/SCALE ←0→
  LEAN/RUB/SLIP; collapsing LEAN into RUB or widening the detent dead-zone are
  legitimate by-ear outcomes).
- Pull floor: root-only vs root+fifth at full CCW.
- Chord-root derivation for the ladder: recommended = lowest held note's PC via the
  ARP_SORTED stack (bass proxy — inversions then *move the gravity floor*, which is
  performable); fallback = global root when no chord held.
- SHADE under a static held chord (terrain-tension: the chord goes "wrong" against
  the darkened scale) — feature or confusion? Listen.
- SHADE mode family: parallel modes recommended (root fixed = the brightness move);
  relative variant rotates root — a different instrument, defer.
- Physical page + GP allocation (decide with hardware in hand, §A3 precedent).
- Global GRAVITY persistence: performance state (like window source, §3) vs session
  config — decide when saving feels wrong.
- ~~Track 2 edges: echo, the live/jam input path, per-step Root/Scale overrides.~~
  **CLOSED 2026-06-10** (Track 2 built): echo keeps its per-repeat re-snap (the
  snap is idempotent on mirror notes; pushed-note echoes resolve into the
  terrain); the live/jam path is untouched (live notes never traverse the render
  buffers — same primitives, same tcc truth); per-step Root/Scale overrides are
  read by the PITCH render from the buffer being built (`pitch_step_scale_root`).
  Accepted-edge list in §9 (2026-06-10).
- **Drum-track pitch chain** (new, from the Track 2 drum fence): drums keep the
  legacy emission chain because a 0 Note byte means "lay_const fallback, still
  plays" — mirror-rest silence and skip-0 are both wrong there. Migrating drums
  needs its own design pass (what does transposer-silence mean for a kit? does
  the fallback note transpose?). Pitched-drum GRAVITY work is unaffected
  (chord-mask/tension process drum note layers already).
- STOP-mid-RESOLVE semantics: now lands at 0 (boundary). If by ear the landing
  feels wrong mid-performance, the alternative is freeze-at-mid-ramp (one line).

**Save-model rethink (2026-06-11) — directions decided (§9), mechanisms open;
working detail in `doc/plans/2026-06-11-save-model-groups-performing-curating.md`**
- Tape storage: session file vs RAM+SD journal vs dedicated bank (the bank option
  breaks the hard-wired bank↔group identity — leaning against).
- ~~Checkpoint storage~~ — **RESOLVED 2026-06-13 (built + GO, Stage C)**: an
  internal fifth "bank" `MBSEQ_AN.V4`, lazy-created at first CHECKPOINT, reached
  by a **sentinel bank index** — not a `NUM_BANKS` bump (that would index
  `seq_pattern[4]` out of bounds in `SaveAllBanks` and surface the anchor in the
  bank UI). Reuses the record serializer wholesale (gen state rides the V4 tag)
  and stays out of every `for(bank<NUM_BANKS)` loop → non-navigable, untouched by
  session writeback. Grain = whole-organism (4 groups, one gesture), one-deep.
- ~~Gen-state extension-tag scope~~ — **RESOLVED 2026-06-12 (built + GO, Stage
  B)**: V4 ext tag = V3 payload + a fixed-stride gen sub-block (count + 4 entries
  × 177 B: instrument, par_layer, engaged, dials, loop/locks/anchor/mult). Every
  content-replacing load clears + re-seeds the track's generators from the record
  (resume ENGAGED); a gen-less slot kills them. Cap = `SEQ_GENERATOR_PERSIST_SLOTS`
  (4)/track; write degrades V4→V3→none per record.
- ~~Track-grain pull gesture~~ — **RESOLVED 2026-06-12 (built + GO)**: the
  track-hold mirror — hold select-row = destination, held+select = source column,
  GP letter + number commits, SELECT+CLEAR = one gesture back. Remaining opens
  carried on the bundle's watch list: two-face choice at pull time (deferred
  post-tape), multi-track chord-select shadowed while a track is held, slot
  NAME in the pull overlay (backlog).
- Phrase format: MBSEQ_S.V4 record-version bump vs new file (carrier verified
  friendly — `song_size` already parameterized).
- Performance fence for auto-writeback (Digitone II PERFORM KIT lesson) —
  **deliberately UNBUILT in v1 (FEARLESS, 2026-06-13)**: CHECKPOINT/REVERT bound
  the blast radius, so committed performance never traps you. The by-ear trigger
  to design one: if a live processor sweep mid-transition ever *edits* a state
  you meant to keep *playing*. On the watch list, not built.
- ~~Still-unblessed proposals riding the rethink~~ — **all blessed 2026-06-11**
  (the tape with its §5.5 supersession placed, row tension meter, chord hand,
  two-surface model, CHECKPOINT/REVERT verbs). After FEARLESS shipped (Stages
  A–C, GO 2026-06-13), the rethink's only still-open mechanism forks are **tape
  storage** and **phrase format**; checkpoint storage/grain and gen-state scope
  are resolved above, the performance fence is deliberately unbuilt, and the pull
  gesture shipped with RECOMBINE.

**Bounce north-star — capture ALL processing as-heard, reset on the copy (2026-06-12).**
The user's stated end-state for bounce/freeze: *apply every processor and generator,
capture the heard output into real notes at the bounce, then turn those processors
off on the bounced track* (the material is already "processed" — frozen as it
sounded the moment bounce fired). **This is already the realized model for everything
in the render stack** (PITCH / CHORD_MASK / TENSION / LIMIT render into the mirror,
bounce captures the mirror, `ResetGenerativeForBounce` disables them on the copy) **and
for generators** (the engaged loop is written to source → mirror → captured; FEARLESS
Stage B's `SEQ_GENERATOR_TrackClear` on the captured section turns the generator off on
the copy). The north-star is *total* only once the remaining deliberately-emission-time
effects join the stack: **robotize (the big one), humanize, groove, echo, LFO** — each
invisible to `OutputActive` today, so each captures un-processed (the §3 born-as-
processors rule names them).
- **Robotize → render-stack processor (deferred to its own session, 2026-06-12).**
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

**Phrase morphing — a third morph model unlocked by the snapshot library (2026-06-13,
candidate; its own future bundle, NOT built).** Surfaced by the user right after the
PHRASES Stage A by-ear GO ("does this open up morphing between phrases?"). Because a
phrase stores the full committed organism as *values* (not refs — see the §9
2026-06-13 snapshot-library entry), **both endpoints A and B are available as data**,
and the engine already re-renders the organism from source into the output mirror on
demand — so feeding it an *interpolated* state and re-rendering is the path recall
already uses. Endpoints are exact for free (`morph=0` IS A, `morph=1` IS B — the
§2/"every dial sweeps 0→max incl. true pass-through" ethos). Two kinds, and the split
is load-bearing:
- **Posture morph (the cheap, musical win).** The continuous state — processor dials
  (GRAVITY position, mask strength, TENSION, mutation rate/depth, robotize),
  transpose, groove, length, generator dials — interpolates as `live = A + morph·(B−A)`
  on a knob: glide the *sculpt/feel* from waypoint A toward B, immediate (not
  bar-quantized — like the GRAVITY isolator-throw). Tiny in RAM (read B's CC block,
  ~couple KB, into a target buffer; lean the live CCs toward it). This is the thing
  §5's Morphing model #1 said was unavailable ("interpolating two live engines on
  shared voices is not available") — the phrase model makes a *form* of it available:
  not two live engines, but the live engine's **posture interpolated between two
  stored postures**.
- **Note-content morph (harder, partial).** Trigger grid can't be half-on — only a
  *scheduled* crossfade (progressively flip steps A→B as the knob turns); per-step
  velocity/length interpolate fine; per-step **pitch** wants scale-quantizing to not
  sound wrong; generator loops/locks/anchor don't meaningfully interpolate (hold A's
  until a crossover, or freeze during the morph). **RAM is the gate:** full per-step
  morph needs two whole organisms resident (~35 KB each; only ~40 KB free) — too
  tight. So the engineering steer: **morph the posture continuously, swap the grid +
  generators discretely** (at a threshold, or hold-until-commit).
- **Where it fits:** makes *"a set is a path"* continuous, and is exactly §4's
  **"transitioning (performed, not dead bars)"** made real — sweep the posture *toward*
  the next waypoint, then commit the structural jump on the bar. The morph is the
  transition; the (shipped) bar-aligned recall is the arrival — two complementary
  gestures. Open: generators-during-morph; grid-crossfade ordering; the morph control
  surface (knob/fader assignment).

**Design-detail (defer until building the relevant piece)**
- Track-slot SD file format (defer until RAM-only slots prove useful).
- Window edge-wrap behavior (loop / freeze / hold-last / continue) — decide live.
- Track-slot recall target (source track only, or freely retargetable → voice-mapping
  questions).
- Per-instrument MIDI channel for drum-pitch (wire const D when pitched drum lines
  need separate timbres) — v2.
- Drum par-layer budget allocation in practice (pitched lines per track).
- Generator UI page layout details and per-step lock / mutation-multiplier gestures
  (provisional layout in §A3; step 6 polish).
- Window-source quick-cycle button placement; physical encoder allocation for the
  four window encoders on the midiphy V4+ panel (verify hardware).
- Automated firmware-upload path (`make upload` via `amidi -s file.syx` after
  bootloader handshake) — not gating any musical work; nice for AFK iteration.

---

## 11. Glossary

- **Buffer** — store of committed MIDI events; the canvas (track layers / captures).
- **Generator** — deposits material into a buffer; always overwrites at its
  destination; typed (gate vs pitch).
- **ENGAGE / DISENGAGE** — connect/disconnect a generator to a destination; while
  engaged it writes source continuously (live mutation heard). Replaces one-shot RUN.
- **ROLL** — force an immediate reroll of unlocked steps; works at mutation 0.
- **Processor** — non-destructive continuous transform over a buffer (robotize, mask,
  voice-leading, windowing).
- **Bounce** — commit current processor/engaged-generator output into the buffer as
  notes.
- **Destination** — a generator's write target (layer = parallel/free; track =
  independent/costs a voice); the add-vs-replace control.
- **Undo slot** — single-deep RAM buffer holding content overwritten by ENGAGE; UNDO
  restores. Live-safety net, out-of-band from the spine.
- **State** — a returnable configuration; on MBSEQ a *group* (= 4 tracks); has a
  *recording* face and a *posture* face.
- **Seed / Posture / Anchor** — the identity an instance returns to / the live config
  that regenerates on recall / a frozen snapshot of a generator loop (SNAP reverts).
- **Soft return / hard return** — dial mutation toward 0 (drifted-but-identifiable) /
  SNAP to anchor (identical).
- **Per-step lock** — per-step flag; locked steps survive reroll. The human-vs-machine
  handoff dial.
- **Mutation amount / per-step multiplier** — global reroll probability / per-step
  0×–2× scaling of it (provisional hybrid scope, §A3).
- **Window / Source view** — a position+length read over a buffer; sits at the bottom
  of every stack and remaps source reads (the morph-across-seam gesture). Always-
  available on live tracks.
- **Track slot** — a captured single-track buffer (layers + track CCs); layerable;
  RAM-by-default, optionally SD-promoted.
- **Quick-capture** — SAVE single-press → next free `CAP_NNN` pattern slot, auto-named.
- **Frozen-tape vs posture recall** — load+FREEZE (frozen face) vs load+run (posture
  face); gestures on the same data.
- **Skeleton / muscle** — macro set arc via groups stacking (upstream tooling) /
  within-group texture via processor + generator dials (the fork's value).
- **Bus / chord-context playmode** — loopback transport (source on Bus port → same-tick
  consumers); proposed playmode reading the whole held notestack as a PC-set vs
  Transpose's single-note planing.
- **v2 / v3 pattern format** — today (layers + CCs 0x80–0x95) / + spine posture
  (processor stack + generator state); versioned in-line, backward-compatible.
- **Source / Output layers; Processor stack; quiet/editing/sweeping** — render-cache
  terms (provisional, §A2).
- **Group / track budget** — 4 groups × 4 tracks = 16; the currency of concurrency.
- **GRAVITY field / tension processor** — bipolar render-stack processor moving
  gripped notes along the stability ladder; pull → scale/chord/root, push →
  LEAN/RUB/SLIP; center detent = pass-through (§8 second build).
- **Stability ladder** — live pitch-class ranking computed from masks the box
  already holds: root → fifth → bus-chord tones → scale tones → chromatic
  neighbors-of-chord-tones.
- **GRIP** — per-track strength of the global gravity field; routing (the
  send-amount that makes the room lean), not a second performance knob.
- **RESOLVE** — bar-quantized ramp of GRAVITY to the detent, landing on the one;
  the cadence gesture.
- **SHADE** — modal-brightness ladder dial (Lydian→…→Locrian, parallel modes)
  selecting the terrain the field moves across.
- **Workflow bundle** — the smallest *playable loop* judgeable by ear (§2.7); the
  unit GO/NO-GO gates sit on.

---
---

# Part II — Design-ahead reference (PROVISIONAL, NOT COMMITTED)

> Everything below was worked out ahead of any build, in an exploratory session. It
> is preserved because it surfaced real requirements and is a useful map. It is
> **not** a plan of record. Per §2.2/§2.6, **none of it is built until §8's GO/NO-GO
> confirms the musical core by ear.** Numbers are provisional; the RAM budget (§A5)
> in particular is gated on measuring base SRAM (§8 step 0). When a Part II item is
> actually adopted, promote it into Part I with a decisions-log entry.

## A1. Processor catalog (provisional)

**Organizing principles.** One stack per (track, layer-class); note→note, gate→gate,
vel→vel, len→len each separate. Strict ordering within a class; no cross-class
composition in v1. Window is a *source view* at the bottom of every stack, not a
mid-stack transform. Robotize migrates from a monolith into typed processors
(pitch/vel/len). Whole-track bounce default. **Every strength dial must be
performable across the full range incl. off (§2.3)** — pass-through at 0 is
mandatory; if a processor can't sweep 0→max it's the wrong abstraction.

- **Pitch (note→note):** chord-tone mask *(shipped; subsumed as a GRAVITY region)*;
  **GRAVITY/tension field** *(promoted to Part I 2026-06-09 — §8 second build)*;
  voice-leading bias *(v2)*; inter-voice interval compression *(v2 — scale-agnostic
  texture tension for the drum-counterpoint rig: force concurrent lines into
  semitone clusters or open fifths)*; register-spread *(the tension dial for
  sample-mapped destinations, where PC-tension is meaningless but range/constraint
  still read as intensity)*; octave-shuffle, inversion, retrograde-pitch
  *(speculative)*; transpose-region; note-decimate/stuck.
- **Gate (gate→gate):** conditional triggers *(special — per-step state; see below)*;
  metric-weight tension *(rhythmic sibling of GRAVITY — pull gates onto strong
  beats, push displaces them off; v2)*; density-thin; density-fill; stutter;
  reverse; probability-override.
- **Velocity / length:** robotize-velocity *(rebuild)*; dynamics-compress/expand;
  accent-pattern; robotize-length *(rebuild)*; staccato/legato.
- **Source view:** window (position / length / rate / direction / wrap).

**Conditional triggers** — gate-layer processor with a per-step condition array
(PROB-N / EVERY-N:M / FIRST / NOT-FIRST / PRE / NOT-PRE / FILL / NONE) evaluated
against a track pass counter, previous-step output, and global FILL. Pass-through
when all NONE. Per-step internal state (≈2 bytes/step — *budget scales with step
count; see §A5*). UI: a `COND` held-modifier overlay on step buttons (parallels
LOCK/MULT). NEIGHBOR (cross-track) deferred to v2 (conflicts with per-track-only v1).
Existing per-step probability par-layer stays as readable data, not migrated.

**Implementation waves:** (1) chord-tone mask, window; (2) voice-leading bias,
robotize migration, density-thin, conditional triggers; (3) stutter, reverse,
accent-pattern, transpose-region, octave-shuffle; (speculative) inversion,
retrograde-pitch — build only when a specific musical hole asks.

## A2. Render-cache mechanism (provisional)

**Three tiers per track:** *source* (par/trg as written by generators/bounce;
canonical; read-only to processors), *output* (what the tick reads; the stack's
current render), *processor stack* (ordered non-destructive transforms; strict
stacking).

**The load-bearing swap point:** the tick path's `SEQ_PAR_*Get` / `SEQ_TRG_*Get`
reads *output*, not source. That single redirection is where cache-read vs
live-render lives.

**Three regimes:** *quiet* (output clean; tick reads it — today's cost); *editing*
(param changed, knob still — background idle-prio pass renders source→stack→output
into the inactive half of a double buffer; atomic swap at a tick boundary); *sweeping*
(knob moving — tick renders current step + small lookahead (2–4 steps) live, bypassing
cache, bounded `O(window × depth)`; on knob-quiet >~50ms, kick a full background
render → back to quiet).

**Per-track only in v1** (cross-track effects go via Bus, §6). **Bounce** = copy
output→source, clear stack. **Migration:** today's destructive generators keep
working — they write *source*; with no processors active the renderer is an identity
copy (output = source), so processor-less tracks need no output buffer at all (a
pruning lever §A5 doesn't bank). The first processor to exercise the cache is the §8
chord-tone mask.

**§3c/§14 reconciliation (was a doc bug):** the render-cache *marginal* cost is the
**double-buffered output only** (~40KB across 16 tracks; see §A5). *Source is the
existing par/trg buffer, not a new allocation.* Earlier drafts' "~72KB" figure
double-counted existing source as new — do not budget it that way.

## A3. Generator UI page layout (provisional)

Provisional reference layout — Turing-style pitch generator across GP1–GP8:
ENGAGE (GP1, LED), Seed (GP2), Range min/max (GP3/4), Scale (GP5), Contour (GP6),
Mutation rate (GP7), BOUNCE (GP8). Dedicated gestures: ANCHOR, SNAP, ROLL; LOCK
(held modifier → step-button lock toggles, LEDs show lock state), MULT (held
modifier → per-step mutation multiplier, 4-level LED brightness). Step LEDs default
to a pitch-as-brightness gradient with a play cursor. LCD: DEST + param strip;
lower LCD a loop-content visualization.

**As-built through phase H (2026-05-26):** GP1 ENGAGE/ROLL (LED reflects
engaged; second-press = ROLL when engaged, SEL+GP1 = DISENGAGE), GP2 UNDO,
GP3 range min (encoder) + ANCHOR (button), GP4 range max (encoder) + SNAP
(button), GP5 mutation rate, GP6 mutation depth (encoder) + LOCK toggle
(button-press for cursor step), GP7 contour shape cycle (UNIFORM/LOW/
HIGH/TRIANGLE, encoder) + MULT cycle 0×/0.5×/1×/2× at cursor (button),
GP8 BOUNCE (cursor-aware — see §8 step 5 phase F). Datawheel scrolls
the LOCK/MULT cursor across loop steps 0..63. **All step-6 dials shipped
(phase G + phase H)** — anchor[64], packed mult[32], and the auto-anchor
at ENGAGE are live; per-step LOCK and per-step MULT share the cursor.

The BOUNCE control is the first place the §3 destination semantic is wired in
the UI: cursor IS the destination, occupancy decides additive-vs-replace.
Phase F.3 (2026-05-26) wired the ENGAGE auto-jump-to-first-empty-legal-layer
behavior (§3 "default destination = first empty legal layer") and the
cross-track processor BOUNCE-capture symmetric counterpart of in-place
processor BOUNCE — both halves of the §3 destination model are now
present in the UI for generators and processors alike.

**Turing model (mechanics):** loop array; global mutation rate; per-step multiplier
(4-bit, 0×–2× — *hybrid scope*); per-step lock bitmask; contour (walk/Brownian/
random); anchor snapshot (auto on init, ANCHOR overrides). Reroll per cycle/step:
locked → skip; else with prob `rate × multiplier` reroll per contour. Soft return =
dial rate→0; hard return = SNAP. **Templates over layer type** — pitch/vel/len/gate
variants share the page; only value semantics differ. Only the pitch variant is
in scope for the first build.

## A4. HIL test plan (provisional)

**Harness extensions** (~6 testctrl command pairs): source R/W (extend), output read,
processor-stack state R/W, generator-state R/W (loop/lock/multiplier/anchor), bus
notestack read, tick instrumentation (per-tick + per-round timestamps, per-track
render time).

**Categories:** unit, integration, regression (load every shipping pattern, 64 bars,
MIDI bit-for-bit identical pre/post), timing, long-run.

**8 timing tests (run every PR):** (1) tick-interval jitter (σ); (2) MIDI-out
scheduling jitter + worst-case; (3) **bus same-tick latency** (validates §6 — most
load-bearing chord-context test); (4) knob-to-sound latency (<50ms; validates sweep
regime); (5) worst-case tick (16 tracks, full stacks, all sweeping, lookahead +
background render in flight) < hard budget; (6) render-cache background-task
interference (priority-inversion probe); (7) 4-hour long-run stability (zero missed
ticks, stable RAM, no timing drift — gigability); (8) SD-write isolation on
quick-capture (failure ⇒ the deferred scratch-ring becomes necessary, §5.5).

**Load-bearing promises → tests:** §6 same-tick = #3; §A2 cache regimes = #4/#5/#6;
gigability = #7. Each failure is **stop-and-redesign**, not tune-and-retry. Tests map
onto the §8 build steps; each step's PR ships its primary tests; timing tests are
global.

## A5. RAM budget (corrected; base measured 2026-05-24)

**Measured base (2026-05-24, MBSEQV4P, commit `3d144ab2`):**

| Region | Used | Free | Note |
|---|---|---|---|
| Main RAM (128 KB @ 0x20000000) | **95.9 KB** (`.data` 1.1 KB + `.bss` 94.5 KB + `._usrstack` 256 B) | **~32 KB** | Free region (`_eusrstack` 0x20017fa0 → `_estack` 0x20020000) is also where the MSP grows at runtime (unmeasured high-water, bounded). Includes the 20 KB FreeRTOS `ucHeap` — all task stacks live there. |
| CCMRAM (64 KB @ 0x10000000) | **0** | **64 KB** | Completely unused today. |
| **Total** | **95.9 KB** | **~96 KB** | |

Cross-checked via `arm-none-eabi-size -A` and `nm --size-sort`.

**Top main-RAM consumers (informational; CCM-relocation candidates if pressure emerges, *not* an action item now):**

| Symbol | Size | DMA? | Notes |
|---|---|---|---|
| `ucHeap` | 20 KB | no | FreeRTOS heap; all task stacks. CCM-eligible. |
| `seq_par_layer_value` | 16 KB | no | Already at `AHB_SECTION` (= main RAM); CPU-only on tick — CCM-eligible. |
| `ring` (`seq_capture.c`) | 16 KB | no | Capture event ring; CPU-only — CCM-eligible. |
| `seq_trg_layer_value` | 4 KB | no | CPU-only — CCM-eligible. |
| `seq_core_trk` + `seq_cc_trk` | ~6.5 KB | no | Per-track runtime + CC config — CCM-eligible. |
| `USB_OTG_dev` | ~2.7 KB | **yes** | Must stay in main RAM. |
| `uip_buf` | ~1.5 KB | **yes** | Must stay in main RAM. |
| `aout_channel` | ~1.4 KB | **likely yes** (SPI DMA) | Verify before moving. |

Net: ~56 KB of existing main-RAM allocations are trivially CCM-relocatable if needed.
The lever below ("relocate CPU-only buffers to CCM first — highest-value, zero-cost")
is well-supplied.

**Threshold met** (base ≤ ~104 KB) and all 64 KB CCM is virgin — but "met" is the
*floor*, not comfort. The free main-RAM region is *shared with the runtime MSP/handler
stack* (grows down from `_estack`). See **Does it fit?** below for the honest
full-scale read: first build trivial, full Part II tight.

**MSP high-water measured 2026-05-25 (phase D.0, stack-paint):** sentinel paint of
the free MSP region at the top of `APP_Init`; readback via `CMD_MSP_QUERY` (0x59
testctrl). After full pytest harness (23 tests, 72 s, exercises encoders / buttons /
pattern loads / playback / track config / capture / robotize / chord-mask):

| Quantity | Value |
|---|---|
| Painted extent (`_eusrstack` → paint ceiling) | **32 544 B** (≈ 31.8 KB) |
| Paint floor (`_eusrstack`) | `0x20017FB8` |
| Paint ceiling (SP at paint − 256 B margin) | `0x2001FED8` |
| MSP usage at paint time (`_estack` − ceiling) | **296 B** |
| MSP growth past paint ceiling (high-water bytes) | **296 B** |
| **Total peak MSP usage from `_estack`** | **592 B** |
| Unused painted tail (headroom) | **32 248 B** |

Net: MSP consumes **~2 %** of the ~32 KB free main-RAM region. The §10 gating
concern is closed — phase D's +20 KB double-buffer lives in **CCMRAM** (not main
RAM), so it doesn't even compete with MSP. The "~8 KB static headroom" figure
below is *also* unaffected: it referred to total static allocation against 192 KB,
not MSP encroachment. Caveat: pytest doesn't exercise pathological ISR nesting or
all-16-tracks-with-full-stacks simultaneously, so 592 B is a lower bound on
real-world peak — but with 30+ KB of unused painted tail, even 10× growth fits.

**Hardware reality (corrected framing).** STM32F407 = **128KB main SRAM (DMA-capable;
SRAM1 112KB + SRAM2 16KB) + 64KB CCM (CPU-only, no DMA)** = 192KB, *not* 192KB
fungible. SD/MIDI DMA buffers must live in main SRAM. **All new spine allocations
below are CPU-only → CCM-eligible** (the tick reads them with the CPU; nothing is
DMA'd). The existing `seq_par_layer_value` already uses `AHB_SECTION` placement, so
section-aware allocation is established practice. **The CCM is the real, previously-
unmodeled headroom** — the budget must be a CCM-vs-SRAM split, not a flat number.

**Per-component (CPU-only, CCM-eligible), worst case:**

| Component | Sizing basis | Subtotal |
|---|---|---|
| Render output (double-buffered, *marginal*) | 2 × 1.25KB × 16 tracks | ~40KB |
| Track slots | 1.25–1.7KB × 16 | ~21KB |
| Generator pool (shared, see below) | 64 × ~184B (64-step loops) | ~12KB |
| Processor stack state | 256 × ~24B | ~6KB |
| Conditional-trigger arrays | ~2B/step × track lengths (worst ~256) | ≤8KB |
| Undo slot | 1 × ~1.5KB | ~1.5KB |
| **Total new (CPU-only)** | | **~88KB** |

**Phase-by-phase actuals (measured against base, cumulative):**

| Phase | CCMRAM used | Δ | Main RAM used | Note |
|---|---:|---:|---:|---|
| Base (2026-05-24) | 0 | — | 95.9 KB | §A5 base measurement |
| A (output mirror)  | 20.0 KB | +20.0 | 95.9 KB | par+trg single-buffered |
| B (processor slots) | 20.25 KB | +0.25 | 95.9 KB | 16×4×4 B |
| C (chord_mask)     | 20.25 KB | 0     | 95.9 KB | code only |
| D (double-buffer)  | 40.25 KB | +20.0 | 95.92 KB | +16 B `active_buf` |
| **E (generator)**  | **46.0 KB** | **+5.76** | **~96.0 KB** | pool 4.5 KB + index 256 B + undo 1 KB CCM; 16 B `last_seen_step` main |
| **F (BOUNCE verb)** | **46.0 KB** | **0** | **~96.0 KB** | code-only — `SEQ_GENERATOR_Bounce` + `SEQ_GENERATOR_BounceRelocate` + `SEQ_GENERATOR_FindEngagedOnTrack` + `SEQ_CORE_ProcessorBounce` + cursor-aware PITCHGEN GP8 dispatch; no new state |

Phase E lands ~6 KB below the §A5 forecast (~12 KB pool) because the 64-step
loop is the only generator state allocated for now — anchor / lock / multiplier
fields are deferred to the §8 step-6 polish phase. CCMRAM still has ~18 KB
headroom; the deferred fields fit trivially when they arrive. Phase F is
code-only and does not move either budget.

**Three corrections vs the prior draft (these were real faults):**

1. **Consistent step-count assumption.** The prior 200B/generator and 2KB
   conditional-trigger figures silently assumed ~64-step loops while track slots /
   render-cache were sized at the full 256-step (1.25KB/track) capability. Pick *one*
   assumption. **Decision (revisit freely): cap generator loops at 64 steps in v1**
   (loop 64 + anchor 64 + lock 8 + multiplier 32 + params ≈ 184B), tiling across
   longer tracks (musically natural for a Turing loop). This makes the ~184B/generator
   figure honest. *If a future piece wants long evolving lines, this is the dial to
   revisit — and it costs ~3× per generator.* Conditional-trigger per-step state
   genuinely scales with track length; budget it at the real worst case (≤8KB) or tie
   availability to a step cap.

2. **Generator count contradiction resolved.** The per-drum design allows up to 16
   pitch generators on one drum track, which contradicts a fixed "4/track" allocation.
   **Resolution: a shared static pool (cap 64 instances) drawn from across all tracks**
   — a track using many pitched drums draws more, leaving fewer elsewhere. Not
   per-track-fixed. 64 × 184B ≈ 12KB.

3. **Render-cache marginal cost, not double-counted.** New = the 2× *output* (~40KB);
   *source is the existing 20KB par/trg buffer.* (See §A2.)

**Does it fit? — yes, but read the margin correctly.** Two distinct answers:

- **The first build (§8 steps 1–5): fits trivially.** The note-layer unlock allocates
  *nothing new* (it reads `seq_par_layer_value`, already in the 95.9KB base); the
  throwaway generator + minimal mask are KB-scale. The measurement does not gate the
  first sound at all.
- **The full Part II ceiling: fits, but tight.** ~88KB new + 95.9KB base = ~183.9KB
  static against 192KB total ⇒ **~8KB static headroom**. Of the ~96KB free
  (32 main + 64 CCM), the full design wants 88, leaving ~8. Realizing it *requires
  actively using CCM* (88KB new > 64KB CCM ⇒ ~24KB must go to main RAM, where ~32KB is
  free; or relocate existing CPU-only buffers into CCM to make room — ~56KB available
  at zero design cost). And the 88KB is **worst-case-everything-on**, not steady state
  (render output only allocates for processor-bearing tracks; track slots could be 8
  not 16; conditional triggers are a ceiling) — realistic concurrent use is well under.
  The MSP high-water (592 B measured, see above) does *not* eat into this 8KB
  headroom because phase D's CCM allocations don't compete with main-RAM MSP.

**Net:** base-RAM unknown is *closed and favorable*; **MSP high-water also closed and
favorable** (592 B measured vs ~32 KB free region — see table above). The first build
is green-lit with huge room. The full vision fits but lands near the edge, and the
per-feature cost is met incrementally as each piece goes in — not committed up front.
The earlier "~22KB headroom against 192KB" was meaningless; disregard it.

**Constraints / levers (if measurement shows pressure):** static pre-allocation, no
malloc in critical paths; **relocate CPU-only buffers to CCM first** (highest-value,
zero-cost lever); only allocate output buffers for tracks with active processors
(processor-less tracks point the tick at source); drop track-slot count 16→8 (~10KB);
single-buffer the render-cache with a tick-side mutex (~20KB, loses lock-free
tearing safety); reduce the generator pool 64→32. **Do not cut features speculatively
(§2.6 / decisions log).** The MIDI File Player cut is **withdrawn** (tick-loop surgery
for ~1KB is poor ROI). **CV/AOUT is actively used and never a cut candidate.**

---

## Plan storage (meta)

Multi-session design plans live in `doc/plans/YYYY-MM-DD-<slug>.md` (git-tracked).
One-shot session plans stay in `~/.claude/plans/`. Plans are scaffolding — archive or
delete once executed into this doc/code. The durable design home is *this file* (and
the adjacent `MBSEQV4_*.md` references); plans are the workflow that gets there. A
root `CLAUDE.md` should carry a short version of this convention so future sessions
follow it.
