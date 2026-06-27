# Lift the one-measure CAPTURE constraint (the self-bus ↔ CAPTURE tension)

**Status:** design-ahead / next-session kickoff. Nothing built. Created 2026-06-26.
**Home when done:** fold into design §9 (build narrative) + §10 (close the open question) + REFERENCE
§3 (codebase facts), then retire this plan.

---

## Kickoff prompt (read this first)

> We can self-modulate a track's direction/progression live (the `Ctrl` par-type self-bus — TK 2019,
> validated by ear 2026-06-26) and freeze the heard result faithfully with the UTILITY-held CAPTURE
> grab. But CAPTURE **refuses any track that isn't exactly one global measure long** — message
> `"capture refused: not 1 measure"`. Self-modulated re-phrasing is most interesting on a *longer*
> base loop, so this constraint is the main thing blocking the self-bus from being fun. Lift it.
>
> Start by re-reading the "Grounded recon" section below against live source (line numbers drift —
> the durable anchors are the function/field names). Then pick approach **B** (pragmatic, integer
> multiples) unless by-ear/scope says go straight to **A** (general). Prove by ear on a 2-bar
> self-modulating melodic track: static-freeze it, play the bounce on a fresh track, confirm the
> re-phrased multi-bar arrangement reproduced.

---

## The problem (one paragraph)

`SEQ_CORE_CaptureSpanReSim` and `SEQ_CORE_CaptureSpanTape` both gate on
`spm != gspm → return -8` where `spm = tcc->length + 1` (the source track) and
`gspm = seq_core_steps_per_measure + 1` (the global "Steps per Measure", OPT page, default 16). The
capture **ring** is framed once per *global* measure boundary (`ref_step==0`), so a span only lines
up with the frames when one track loop == one global measure. Any longer (multi-bar) or shorter
(sub-measure/polymeter) track is refused outright.

## Grounded recon (verified 2026-06-26 — re-verify names against source)

- **The refusal:** `seq_core.c:2217-2219` (re-sim) + `:2385` (tape). `spm = tcc->length+1`,
  `gspm = seq_core_steps_per_measure+1`.
- **`seq_core_steps_per_measure`:** global, default `16-1` (`seq_core.c:602`), user-settable in OPT
  ("Steps per Measure", `seq_ui_opt.c:311-318`), persisted as `StepsPerMeasure` (`seq_file_c.c`).
- **The ring is per-GLOBAL-measure:** `SEQ_CORE_CaptureRingTick` (`seq_core.c:420`) runs in the tick
  prologue at every global-measure boundary, indexing `seq_core_cap_ring[robotize_measure_ctr %
  SEQ_CORE_CAP_RING_BARS]` (BARS=17 = live bar + 16 grabbable). Also stamps
  `seq_core_cap_tape_bar_start[...]` (the while-playing tape's per-measure downbeat ticks).
- **The frame** (`seq_core_cap_frame_t`, `seq_core.c:211-230`) is self-contained: `traverse_state`,
  `robotize_seed`, `step` (= 0 for a whole-measure track; for a multi-measure track it is 0 only at
  the loop start, gspm/2gspm/... mid-loop), the 5 progression counters, and up to 4 generator slots.
  Note: it carries its OWN seed, so it does NOT touch the 16-deep robotize_seed_snapshots ring that
  FREEZE shares — **the capture ring is already decoupled from FREEZE/robotize**, which lowers the
  risk of re-framing it.
- **`FrameBack(track, k)`** (`seq_core.c:457`) returns `&ring[(robotize_measure_ctr - k) % BARS]`;
  valid `k = 1..filled-1`.
- **Re-sim drive:** restores the frame (`seq_core.c:~2273`), drives `dst_steps * tps` ticks where
  `dst_steps = k * spm` (`:2220`, `:2315`), records the emitted stream via the sink, materializes
  into consecutive dst steps, restores live state byte-identical.
- **dst caps:** `dst_steps > 256 → -7`; par/trg overflow → `-9`/`-12` (`:2221`,`:2237-2238`).

## Approach B — integer multiples (recommended first lift)

Smallest change that unblocks the common case (2/4/8-bar self-modulating loops). Keep the
per-global-measure ring; teach CAPTURE that a track can be **N global measures** long.

1. **Relax the gate:** `if( spm % gspm != 0 ) return -8;` and set `N = spm / gspm`.
2. **Land FrameBack on a loop boundary.** The window-start frame must be one where the track is at
   its loop start (`f->step == loop_start`). Step back `k * N` global-measure frames *and* ensure the
   landing frame is a loop boundary (search back to the nearest `f->step==0`/loop-start, then `k*N`).
   Decide: require the grab to align to loop boundaries, or snap to the nearest.
3. **Drive k loops:** `dst_steps = k * spm` already; the drive length follows. Re-confirm the 256-step
   and par/trg overflow guards (a 4-bar track caps k≈4).
4. **Tape path parity:** apply the same relaxation in `CaptureSpanTape`; the window now spans `k*N`
   `bar_start` markers — verify the tick→step bucketing in `CaptureMaterializeNote` is correct across
   the multi-measure window.
5. **Phase/alignment risk:** robotize_measure_ctr increments per global measure; a multi-measure
   track's loop start recurs every N measures *if* it began aligned (synch-to-measure or natural).
   An un-synched track may have loop boundaries that don't sit on the captured frames — detect and
   refuse (or document) rather than capture a torn loop.

**Leaves refused:** sub-measure (length < gspm) and non-integer polymeter. That's Approach A.

**STATUS — SHIPPED + by-ear GO 2026-06-26; HIL 216/216.** A1 (any-length WHILE-PLAYING tick-period
tape) is the headline win — grabs sub-measure/odd/multi-bar/self-mod loops note-for-note. Stopped
re-sim restricted to ONE global measure (`spm != gspm → -8 "play to grab"`): the multi-bar stopped
drive rotates by a sub-measure amount (HIL trace +11 on a 2-bar fwd line) — that's the queued **A2**
kernel. Folded into design §9 (2026-06-26) + REFERENCE (multi-measure CAPTURE) + MANUAL. Grab unit =
"loops"; 256-step ceiling kept. **NOT retired — A2 (stopped multi-bar drive-phase) + synch-to-measure
support (route synch'd track as a 1-bar loop) remain (see the Mechanism/Stage notes below).**

**Bugs caught (adversarial trace-review, both fixed pre-by-ear):** (1) `step==0` boundary premise was
false (frame holds the PRE-advance step); (2) phase off-by-one (loop-starts at `ctr ≡ 1 (mod n)`).
Fix = frame-count arithmetic `e=(ctr-1)%n` (reduces to `FrameBack(k)` for n=1). See design §9.

**Two FATAL bugs caught by adversarial trace-review (both in the B work, both now fixed + re-verified):**
1. **`step==0` loop-boundary premise was false.** The frame is snapshotted in the tick PROLOGUE
   *before* `NextStep` wraps, so `frame->step` holds the PRE-advance step (`==tcc->length` forward;
   an RNG value for random traversal), never reliably 0. Every whole-measure grab silently refused.
   The struct comment ("step ... 0 for a whole-measure track") was the lie that fooled it AND the
   first review's "byte-identical" verdict. **Fix:** frame-count arithmetic, no `.step` read.
2. **Phase off-by-one (n≥2 only).** Loop-start frames sit at `robotize_measure_ctr ≡ 1 (mod n)`, not
   `≡ 0` (the first frame lands at ctr=1 with the track still at step 0 — FIRST_CLK suppresses that
   tick's advance). `e = ctr % n` landed on the loop MIDPOINT → half-loop rotation. Invisible to HIL
   (multi-measure pins assert success/determinism, NOT note-for-note phase) — only trace/ear catch it.
   **Fix:** `e = (ctr-1) % n`. Reduces to `FrameBack(k)` for n==1.
   **Lesson:** this capture timing is the "#1 hardware-validation item" for a reason — trust source
   traces over comments, and multi-measure phase needs a note-for-note HIL pin (TODO) or by-ear.

## Approach A — re-frame the ring around the track's own loop (general) — COMMITTED BUILD

User chose **full A** (tape + stopped re-sim) over B-only + the Steps-per-Measure workaround, 2026-06-26.

### Mechanism (verified against source 2026-06-26 — durable symbols, line numbers drift)

Two clocks with **different cadences** — this is the whole key:
- **Generator auto-mutate** (`SEQ_GENERATOR_Tick`, fires `cur==0 && prev!=0` where `cur =
  seq_core_trk[t].step`) rides the **TRACK's own loop wrap** → phase-independent.
- **Self-bus** (`Ctrl` par-type direction/progression), `loop[]`, and the step/progression counters
  are captured in the frame → phase-independent.
- **Robotize / GRAVITY / phrase-morph** (the `ref_step==0` block, ~`SEQ_CORE_RobotizeLoopBarTick` /
  `SEQ_CORE_TensionResolveBoundary`) ride the **GLOBAL measure** → the *only* thing that needs
  global-phase reproduction for a non-aligned loop.

Within-tick order (`SEQ_CORE_Tick`): `SEQ_GENERATOR_Tick` (mutate, top) → `SEQ_CORE_RenderTracks` →
`(bpm_tick%96==0)`: ref_step++ then `(ref_step==0)`: robotize/GRAVITY/`++robotize_measure_ctr`/
`SEQ_CORE_CaptureRingTick` → body `SEQ_CORE_NextStep` advances `t->step` (wrap = `++t->bar`).

Consequences:
- **A subsumes B.** Per-track-loop framing makes "loops" literal; the `spm % gspm` gate and the
  `step==0` boundary search both DISSOLVE (every frame is a loop boundary). Whole-measure tracks
  become the special case where a loop spans N global measures. So A **replaces** the shipped B
  mechanism and **re-validates** the whole-measure path.
- **The tape path (while PLAYING) needs no kernel** — it records emitted output, so robotize/GRAVITY
  are faithful at any phase. The hard part is the **stopped re-sim** global-phase reproduction.
- The capture ring is **RAM-only** (`seq_core_cap_ring` is `.bss`, not persisted) → **no SD format bump**.
- **Random traversal** (`Random_Step` / `Random_D_S` / `Random_Dir`) sets `t->step` straight from the
  RNG and **never wrap-detects** (no `++t->bar`); it has no clean loop. Refuse it under A (revisit if a
  by-ear case wants it — would need a synthetic fixed-step-count window).

### Stage A1 — per-track-loop ring framing + tape path (foundation, low-risk)

- New per-recording-track `cap_loop_ctr`; **leave `robotize_measure_ctr` untouched** (FREEZE's
  `robotize_seed_snapshots` + robotize phase still use it — no coupling broken).
- Capture the frame at the recording track's loop wrap: hook at the **top of `SEQ_CORE_Tick`**,
  mirror the `cur==0 && prev!=0` detection on the recording track, **before** the mutate (frame holds
  pre-mutate state, matching the existing convention). Add `ref_step` (global phase) to the frame for A2.
- Re-base `seq_core_cap_tape_bar_start` to loop-wrap downbeats, indexed by `cap_loop_ctr`. Ring depth = 17 LOOPS.
- Simplify the consumer: drop the `step==0` search in `SEQ_CORE_CaptureRingLoopWindow` (every frame is
  a loop now) and the `spm % gspm` gate in both span paths + MaxK. `dst_steps = k * spm` (any length).
- Refuse random-traversal directions.
- **By-ear gate (A1):** while PLAYING, grab an 8-step and a 24-step self-modulating melodic track
  (`Ctrl: Directn.` ping-pong/jump-back) → tape path → the re-phrased arrangement reproduces.

### Stage A2 — stopped re-sim global-phase kernel (the hard part, higher risk)

- Re-sim drive: instead of `B = gspm*96` + `ref_step = steps_per_measure`, derive the drive's initial
  `ref_step` / base tick from the **frame's recorded global phase** so robotize/GRAVITY fire at the
  correct phase relative to the loop start.
- Robotize SEED is already self-contained in the frame (per-track-RNG keystone) → this is about
  **phase**, not seed lookup; verify `robotize_loop_phase` / GRAVITY per-measure state don't read a
  stale `robotize_measure_ctr` index.
- **By-ear gate (A2):** STOPPED grab of an 8-step / 24-step loop **with GRAVITY on the track** →
  reproduces the robotize-shaped arrangement. (This is the same class as the existing "#1
  hardware-validation item," made harder by arbitrary phase.)

### Stage A3 — polish

- Final random-traversal policy, thermometer/LCD wording, HIL pins (sub-measure + odd-length success,
  while-playing), docs (design §9/§10 + REFERENCE + MANUAL), retire this plan.

## Open decisions (carry into the build)

- Random-traversal: refuse (A1 default) vs synthetic fixed-step window (later).
- Whether the `dst_steps > 256` ceiling ever bites a sub-measure case (it won't for short loops; a
  long odd loop like 240 steps caps k≈1) — accept as the k×length cap.
