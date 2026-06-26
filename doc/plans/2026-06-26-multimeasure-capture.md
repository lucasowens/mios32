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

## Approach A — re-frame the ring around the track's own loop (general)

The "right" general solution. Push a frame at each **track-loop wrap** of the ring's recorded track
(it records only ONE track at a time, so this is feasible), not at the global-measure boundary.
`FrameBack(k)` = k track-loops. Handles any length incl. sub-measure/polymeter. Bigger change:
decouple `CaptureRingTick` from `robotize_measure_ctr`, re-base `seq_core_cap_tape_bar_start` on
track-loop downbeats, redefine ring depth as 17 *loops*. Because the capture ring is already
self-contained (own seed), this does **not** disturb FREEZE or the robotize ring. Do this only if a
by-ear case wants sub-measure/odd-length self-modulation that B can't reach.

## Validation / by-ear plan

- HIL: extend `test_render_perf.py`-style or the capture tests — a 2-bar (32-step, gspm=16) track,
  static-freeze k=1 and k=2, assert dst gets 32/64 steps and the materialized notes match a known
  self-modulated arrangement (use a deterministic direction, e.g. ping-pong, so the re-phrasing is
  reproducible without RNG). Add a refusal pin: a 24-step (non-multiple) track still returns -8.
- By ear (the gate): a 2-bar melodic track with a `Ctrl: Directn.` layer doing jump-back/ping-pong,
  static-freeze, play the bounce on a fresh track with NO Ctrl layer → the re-phrased 2-bar
  arrangement must reproduce. Then try random direction across k bars and confirm re-sim reproduces.

## Open decisions for the session

- B vs A first (default B; jump to A only if a sub-measure case is the actual desire).
- Loop-boundary alignment policy for the grab (require-aligned vs snap-to-nearest vs refuse-torn).
- Whether to also lift the `dst_steps > 256` ceiling or accept it as the k×length cap.
