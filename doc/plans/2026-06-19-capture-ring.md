# Retroactive CAPTURE ring — buildable plan (2026-06-19, corrected)

Status: **building**. Supersedes the K=1 first-cut from the `capture-ring-design`
workflow (that plan had a confirmed materialize fatal — see below). User chose the
fuller north-star: **K bars retroactive, including generator wander.**

## The one thing this proves
Play a melodic generative track for several bars → hit CAPTURE → the **last K bars
you just heard** — generator wander + random traversal + robotize, exactly as played —
become a self-contained, editable, bounceable **static pattern**. End-to-end proof that
the keystone (ae3b6124) makes a span re-simulable, and that the lived timeline can be
frozen.

## The materialize fatal (verified against source — why the workflow's K=1 plan was wrong)
The workflow plan materialized via the OUTPUT MIRROR (`SEQ_CORE_CaptureToTrack` →
`SEQ_CORE_CaptureTrackOutput`, snapshots `SEQ_PAR/TRG_OutputActive`). That **loses the two
streams the re-sim exists to reproduce**:
- **robotize** mutates the *emitted event* at scheduling time
  (`SEQ_ROBOTIZE_Event(track, t->step, e)`, `seq_core.c:3110/3350`, perturbs `e` in
  `layer_events[]`) — it never touches the rendered mirror. Confirmed emission-time
  (design §3 "lone emission-time special-case").
- **traversal** only reorders *playback* (`t->step` order in `SEQ_CORE_NextStep`); the
  mirror is source-ordered, and `SEQ_CC_ResetGenerativeForBounce` forces
  `dir_mode = Forward` + zeros the whole progression section (`seq_cc.c`).

So an output-mirror capture == existing BOUNCE; it cannot "sound like what just played"
on any traversal/robotize track. **Fix: record the EMITTED MIDI stream** (the true
"Capture MIDI") during the re-drive, then quantize it into a flat forward pattern. The
firmware already captures the emitted stream in the MIDI exporter (`seq_midexp.c`: 4
hooks + send-package sink + per-tick drain), and `seq_record.c` already quantizes
notes→par/trg. CAPTURE = "MIDI export, to a RAM pattern, retroactively."

## Why K-bar wander needs a ring (and why it's now in scope)
`mutate_loop` rewrites `g->loop[]` *in place* every bar (`seq_generator.c:177-208`,
draw count off `g->seed` is data-dependent) and the seed advances — only the LATEST
bar's loop+seed survive in the pool slot. To rewind K bars you must have snapshotted
the generator slot state at each bar start. `sizeof(seq_generator_t)` ≈ 184 B
(12 hdr + 4 seed + 64 loop + 8 locks + 64 anchor + 32 mult). Reuse the proven
`SEQ_GENERATOR_TrackSnapshot/TrackRestore` (track-undo precedent).

Key fidelity fact: restoring a slot's `loop[]`+`seed`+`locks` at the window start and
re-driving with auto-mutate **ON** regenerates the *exact* wander forward (deterministic
from the seed — the keystone property), and `write_loop_to_source` regenerates the
source par buffer each bar, so the **ring does NOT need the source buffer** — only the
generative *frame*. (Limit: a mid-window manual ROLL/ENGAGE/SNAP advances the seed
off-ring → not faithfully re-simmable. That's the "lived/recording" face, deferred.
Autonomous wander IS faithful.)

## First-cut scope
**IN**
- **Single VISIBLE track**, melodic/normal (`SEQ_UI_VisibleTrackGet`). Ring is built for
  the visible track only and **invalidated on track-switch** (sculpt a track → capture
  it). Bounds the generator ring to one track.
- **K bars retroactive**, K = 1..16 chosen at CAPTURE (ring depth 16, indexed
  `robotize_measure_ctr & 0x0f` — shares the existing robotize-ring clock).
- **Replayable, re-simmed:** generator wander (per-bar `loop[]`+`seed` ring) + random
  traversal (`random_traverse_state` ring) + robotize (`robotize_seed_state` ring,
  already exists) + progression counters.
- **Materialize = emitted-stream record → quantize.** Re-drive recording emitted
  note-on/off (synthetic tick), pair into gate lengths, quantize to steps, write
  note/vel/length par + gate/accent trg into the dst track across K-bar length. Reuse
  `CaptureToTrack`'s dst geometry setup + `ResetGenerativeForBounce` (forward playback
  IS what we want for a flattened recording), replacing the mirror memcpy.
- **Destination = a spare track** in the current pattern (RAM, instant, UNDO is the net).
- **Re-sim runs with the engine RUNNING, inside TASK_MIDI under MUTEX_MIDIOUT**, via a
  deferred request flag (one engine driver, no +4 preempt). Full snapshot→drive→restore.

**DEFERRED**
- Whole-group capture; drum-track multi-gen ring (RAM); cross-session ring persistence
  (RAM-only, like keystone seeds — bank-format bump is design §10).
- Emission coin-flips (probability/humanize/random-gate/echo — global RNG, keystone
  deferred). Proof span: probability=100, none of those. They gate emitted events
  downstream; not reproducible by single-track re-sim until measure-axis-hash lands.
- chord_mask track (BAND reads live bus, `seq_core.c:451`) + loopback-LFO on the
  captured track (perturbs `bus_notestack`) — RECORDING face, deferred.
- Live-MIDI-in tape; mid-window manual generator gestures.
- Physical CAPTURE gesture — **harness-driven this cut** (gesture = a later user
  decision; NOT the PATTERN/SD surface, which commits to SD immediately).

## Components / build order (each compiles + has a pin)
1. **`CMD_CLOCK_STEP` 0x4b** — synchronous tick driver, engine **stopped** (so
   `SEQ_CORE_Handler` no-ops Tick = no +4 race), under MUTEX_MIDIOUT, `mute=1`:
   `for n: t=TickGet()+1; TickSet(t); SEQ_CORE_Tick(t,-1,1);`. Reply: final tick + trk0
   step. Advances NextStep/traversal/wander/ref_step==0 hook. **Closes the keystone's
   documented HIL gap** (traversal trajectory + SCRUB pins). `board.clock_step(n)`.
   *Pins: traversal-trajectory determinism, ring index alignment.*
2. **Per-bar generative-frame ring** (main SRAM, visible track only): traversal state +
   progression counters + generator `TrackSnapshot` per bar, depth 16; snapshot at the
   `ref_step==0` hook (`seq_core.c:2621-2645`, beside the robotize-ring write); seeded in
   `SEQ_CORE_Reset`; invalidated on track-switch. *Pin: discard-oldest.*
3. **Accessors** — `SEQ_RANDOM_StateGet/StateSet` (global RNG save/restore for
   non-destructiveness), `SEQ_GENERATOR_LastSeenStepGet/Set` (wrap detector). *Pin:
   round-trip.*
4. **`SEQ_CORE_CaptureSpanSnapshot/Restore`** — full live-state set (all 16 tracks' core
   state, globals, `random_value`, `seq_pattern_dirty`/`phrase_drift`, BPM tick,
   visible-track source par/trg + dirty flags, generator slots). *Pin: drive+restore =
   byte-identical (non-destructive).*
5. **Emitted-event recorder** — `(synthetic_tick, package)` buffer filled by the
   send-package hook during re-drive.
6. **`SEQ_CORE_CaptureSpanReSim(src, dst, K)`** — install 4 hooks → snapshot → rewind
   visible track to window-start (restore ring frame: gen slots + traversal + robotize +
   counters + step, set FIRST_CLK) → drive exactly K bars with wander ON (drain pool +
   record each tick) → materialize → restore → uninstall. Deferred into TASK_MIDI under
   MUTEX_MIDIOUT. *Pins: resim-vs-LIVE trajectory, off-by-one.*
7. **Materialize quantizer** — emitted stream → dst par/trg, K-bar length. *Pin: captured
   static plays back == heard span (note/vel/gate).*
8. **`CMD_CAPTURE_SPAN` 0x4c** + whole-measure refusal + `board.capture_span(src,K,dst)`.
   *Pins: end-to-end, whole-measure refusal.*
9. **BY-EAR GO/NO-GO** (needs flash): engage gen + random traversal on one whole-measure
   melodic track (probability=100; no echo/humanize/random-gate/chord_mask/loopback-LFO),
   let it wander K bars, `capture_span`, switch to the captured track. **GO** = sounds
   like the last K bars; source keeps running unaffected (no glitch/stale-note/phantom
   dirty); captured track editable/bounceable.

## Solid parts carried from the workflow plan (verified)
Concurrency (TASK_MIDI + MUTEX_MIDIOUT deferred path); the full snapshot/restore set
incl. `random_value` + `seq_pattern_dirty`; FIRST_CLK rewind re-anchor; whole-measure
constraint enforced in code; the 4 A8 render-stack fences honored by materialize; engine
RUNNING dissolves pool-drain + render-amplification. Full source anchors: see the
workflow output (scratchpad `capture_plan_final.md`) and the pre-build recon
(`2026-06-19-pre-build-recon.md`).

## RAM / cost
Generator ring (visible track, cap ~4 slots × 16 bars × 184 B) ≈ 11.8 KB **main SRAM**;
traversal+counter ring ~few hundred B; snapshot scratch (16 tracks) ~few KB — all main
SRAM (~32 KB free), **zero CCM** (the pool stays in CCM; we only snapshot it). New
testctrl verbs 0x4b/0x4c.

## Risks
- Live-tick budget: ~K×16×24 `SEQ_CORE_Tick` + pool drains in one TASK_MIDI burst (engine
  running → no render amplification). Could starve LCD/UI; measure on device, chunk if so.
- Materialize quantizer poly/overlap: first cut targets melodic/mono-ish; document poly +
  echo limits.
- Generator ring slot cap: if visible track has > cap engaged gens, refuse or grow.

## BUILT — status 2026-06-20

All 8 steps built and compiling (`project.hex`, TESTCTRL=1). RAM: ~16.6 KB main SRAM +
~8.6 KB CCM free. 8 HIL pins collect. Two refinements were locked in during the build:
- **Capture while STOPPED** (not while playing) — the re-sim drives the one engine
  exclusively (no +4 race, no live freeze, no BPM gap, no deferred-task marshaling).
  Reuses the clock-step "drive while stopped" basis. While-playing = follow-on.
- **Incremental note-on materialize with default gate length** (no event buffer); pitch +
  rhythm + velocity + traversal-order + wander captured. Precise gate length = refinement.

A 3-lens adversarial review of the diff caught a **FATAL + 5 must-fixes** (all now fixed):
1. *(FATAL)* whole-measure guard compared against `seq_core_steps_per_measure` which is
   stored as steps−1 → refused the common 16-step case. Fixed: `gspm = …+1`, exact `spm==gspm`.
2. src OUTPUT MIRROR left stale after restore → `ForceRewrite(src)` + re-render in restore.
3. residual note-offs (scheduled past the window) leaked to live out → `SEQ_MIDI_OUT_FlushQueue()`
   before uninstalling hooks (drains them through the velocity-0-skipping sink).
4. step rotation: FIRST_CLK plays-without-advancing → captured pattern rotated by one. Fixed:
   drive WITHOUT FIRST_CLK, phase-aligned to a measure boundary `B=gspm*96` (`ref_step`,
   `timestamp_next_step`, `bpm_tick_prefetched` set so tick B is processed and the first
   NextStep advance/draw fires; `last_seen[src]=frame->step` so the wander mutate fires on the
   natural tick). **← #1 hardware-validation item: confirm the captured downbeat == heard
   downbeat (no one-step lead/lag) by ear + the resim-vs-live HIL pin.**
5. window-start loop not pushed to source before the drive → `ForceRewrite(src)` after
   `TrackRestore`.
6. loopback tracks (round-0, not export-gated) wandered + corrupted their pool slots →
   `SEQ_GENERATOR_ReSimOnlyTrackSet(src)` gates auto-mutate to the captured track during the drive.
Plus should-fixes folded: clear dst groove (already baked into emitted positions → no double
groove); arp playmode refused (`-11`); explicit dst par/trg overflow guards (`-9`/`-12`);
snapshot/restore `bpm_tick_prefetched`.

**Deferred should-fixes (documented, off the proof span):** `seq_lfo[src]` phase advances
during the drive and isn't restored (inaudible with LFO off); `phrase_drift` for dst's group
is set by the sink (arguably correct — capture is a deliberate edit; use a separate group to
avoid flagging src); ring not invalidated on mid-window pattern-load; re-sim runs in the SysEx
RX context not deferred-TASK_MIDI (fine while stopped + harness-driven). Drum tracks allowed
(machinery sound; note-0 playback fence is a by-ear melodic-target caveat).

**Next:** flash `project.hex` → run HIL (8 capture/clock-step pins + full regression) →
**by-ear GO**, validating the step-phase (#4) first. Then fold to design §9/§10 + REFERENCE +
MANUAL, commit.
