# Unified FREEZE → anywhere — slice plan (2026-06-20)

Durable model lives in the design doc **§9 (2026-06-20 decision)** + **§5.6 overlap #5** + the
**§10 Retroactive CAPTURE** entry. This file is the workflow scaffolding (slices, order, open
questions); fold each slice into the design doc / code as it lands, then archive this.

## The model in one breath

CAPTURE (retroactive grab off the ring) and BOUNCE (lossless freeze of the computed output) are
**two means to one end** — freeze whatever the organism is doing (playing / generated / programmed)
into static material, in whatever track/pattern you choose. They're split today only by one engine
boundary:

- **Render stage** → result is in the rendered mirror **as par/trg layers** → losslessly copyable →
  **BOUNCE** (all params + trigs + CC + config).
- **Emission stage** (random traversal order, robotize, roll-as-played, echo, prob/humanize coin-flips,
  live MIDI keys) → **flattens into played notes** → only note-recoverable → **the ring/tape**.

**Unified verb:** `FREEZE [last K bars of the ring] → [track, pattern], all params, generators off`.
Bounce is the **lossless core**; the ring makes it **retroactive**; the tape is the **temporary catch**
for the emission residue, which **shrinks to zero** as emission effects migrate into the render stack
(robotize first). Endgame: **FREEZE = BOUNCE = one verb**; the tape survives only for irreducible
real-time input (live keys / true coin-flips). Convergence, not duplication.

Why bounce is the base (not capture): the note-stream collapses a roll/chord to a single quantized
note; bounce keeps every layer editable. Memory is not the obstacle — static layers (roll/chord/CC)
don't vary per bar (capture once); only the notes vary, which the ring already holds.

## Slices (in order)

### Slice 1 — precise gate + multi-step length (SHIPPED 2026-06-20, tape path) ✅
The note-stream capture made faithful for melodic articulation incl. long notes.
- `SEQ_CORE_CaptureGateToParLen` (gate ticks → stored length, inverts tps·len/96).
- `SEQ_CORE_CaptureMaterializeNote` rebuilds the multi-step length **chain** (gated Gld start +
  carried Gld steps with note+vel repeated, gate off + fractional tail). Velocity carried.
- By-ear GO. HIL `test_capture_precise_gate.py` (gate-gradient + glide + multi-step pins).
- **Owed follow-on:** the **stopped re-sim sink** got only the simpler glide-tail fix (open-at-end →
  Gld), not the full multi-step helper — route `SEQ_CORE_CapSpanSink` through
  `SEQ_CORE_CaptureMaterializeNote` so both paths match. Small; same helper.

### Slice 2 — bounce-off-the-ring with dest = track + pattern (NEXT, highest leverage)
Make the retroactive grab **lossless** (all params) and **targetable to any pattern**.
- Today: `SEQ_CORE_CaptureSpanTape/ReSim` write note/vel/length to an **in-session dst track**;
  `SEQ_CORE_CaptureToSlotTrack` already writes a full bounce to **a pattern slot** (load-modify-save).
- Goal: a retroactive freeze that, for the render-stage content, copies the **rendered layers** (via
  `SEQ_CORE_CaptureTrackOutput`, all params) rather than just the emitted notes — and lets the
  destination be **any track AND any pattern** (extend the UTILITY gesture's dst pick to include a
  pattern, reusing the slot-bounce save path).
- **Open design questions (resolve before building):**
  - **Per-bar layer reconstruction.** The rendered layers vary per bar when a generator is engaged
    (the generator rewrites the source layer each bar). To bounce K bars back losslessly, re-sim must
    snapshot the **source layer** at each ring bar, not just the emitted notes. (Re-sim already
    restores per-bar source state — extend it to capture the layers.) For static / non-generative
    tracks this is trivial (layers don't change). **Scope the generative case explicitly.**
  - **Reconcile bounce-layers vs emission-notes** for a span that has both render-stage and
    emission-stage content. Cleanest: bounce the render layers; overlay the tape's emission-only
    delta as notes; **do not copy a transform layer whose effect is already baked into the overlaid
    notes** (double-apply). Until migration (slice 3), this is a per-layer choice — document it.
  - **Dest = pattern** memory/UX: the slot path is load-modify-save (RAM snapshot of the dst group).
    The UTILITY gesture currently picks a select-row track; adding a pattern pick is new UI.

### Slice 3 — render-stack migration of emission effects (the convergence engine)
Each emission effect moved into the render stack becomes **bounce-visible** → drops out of the tape
residue. Order by leverage:
- **Robotize → render-stack processor** (named in §5.6 #4 / §10; the lone emission special-case in §3).
  Its own session — completes the §3 closure AND shrinks the FREEZE residue.
- Then: echo, probability/humanize/random-gate (the emission coin-flips — note the measure-axis hash
  vs tape decision in the keystone SHIPPED note, §10).
- Random **traversal order** is structural (the order steps fire) — likely stays a re-sim/tape concern
  longer; revisit whether it can be expressed as a rendered reordering.

### Endgame
When slice 3 is far enough, slice 2's "overlay the emission delta" path shrinks to nothing and the
unified verb is **just bounce, retroactive, to anywhere**. The tape remains only for live MIDI-in
and any genuinely irreducible real-time coin-flip. At that point CAPTURE and BOUNCE are literally one
function with one gesture.

## Status note (uncommitted as of 2026-06-20)
Slice 1 firmware (precise gate + multi-step + velocity carry) is built and by-ear-confirmed for
multi-step; the velocity-carry rebuild is **ready to flash** (pending the user's confirm). Nothing
committed this session.
