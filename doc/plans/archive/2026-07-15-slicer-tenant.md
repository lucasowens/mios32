# Slicer tenant — chop the loop like a sample, resequence it

**Date opened:** 2026-07-15
**Status:** ACT 1 COMPLETE — by-ear GO 2026-07-16 ("works!"), HIL 278/278 baseline
2026-07-17 (10 slicer pins; the 14 capture-family reds were the ext-CC replay gap,
fixed via ExtCcNeutralExtend — see the 2026-07-16/17 decisions-log block). Archived. Act-1 painted-order surface = the
SlcOr par layer on the EDIT page (Waypoint idiom); the bespoke plane-2 GP face
waits for a user tab-grid mock (mock ritual). Deviations from plan: no per-drum
scope CC pair yet (slot drum_mask fixed 0xFFFF, whole kit); REPT/REV ride their own
independent thermometers rather than being strength-gated (each 0 = off, better
grammar); scratch is static main-SRAM .bss (400 B), not stack.
**Scope decided with user:** both surfaces (seed-browse + painted order) in act 1;
both materials (drum + melodic) from day one.

## What it is

A render-stack processor that treats the track's *heard output* as a sample:
divide the loop into equal slices, then resequence them — reorder, repeat
(stutter), reverse. Chop + reorder + repeat + reverse are all one primitive:
a slice *mapping* (output position → source slice), applied as a buffer
permutation pass. The mapping need not be a bijection, so "slice 1 four times"
is the same code as "swap slices 2 and 7".

NOT a live beat-repeat FX. It's a deterministic resequence that re-renders when
a dial moves — sweepable, pass-through at 0, capturable, bounce-faithful for free.

## Why the render stack tail (verified against source 2026-07-15)

- `SEQ_CORE_RenderTrack` (seq_core.c) hands each processor the **whole**
  par+trg buffer, not one step — a cross-step permute fits the existing
  dispatch shape (`*_render_range(track, p, par_buf, [trg_buf,] 0, num_p_steps)`).
- Output mirror holds the chopped result → `OutputActive`, tape/CAPTURE,
  bounce, midexp all faithful with zero bake code (the §3 rule's whole point).
- EDIT keeps showing SOURCE (durable rule); mirror/TPD show the resequence.
- Tail position = **after LIMIT**: it chops what you hear, including
  everything Pitch/ChordMask/Arp/Tension/Limit did. Sample metaphor intact.
- Emission-time effects (Groove shift, Humanize, Echo, strum ranks) happen
  after render and are NOT chopped — same status as today's grammar: slicer
  reorders the material; feel effects ride on top.

## Stack + slot changes

- `SEQ_CORE_NUM_PROCESSOR_SLOTS` 5 → 6; new `SEQ_CORE_SLICE_SLOT 5` (tail).
  Cost: 16 tracks × 8 B = +128 B RAM.
- New `SEQ_PROCESSOR_ID_SLICE`; `SEQ_CORE_SliceSlotSync(track)` follows the
  Arp shape (active on non-neutral state: grid != 0 && !bypass). **Durable
  rule applies:** raw tcc writes on LIVE tracks need `SEQ_CORE_AllSlotSync`.
- slot->strength = SLICE_STRENGTH (universal sweep, see dials).
  slot->drum_mask = per-drum scope, same semantics as ChordMask.

## The mapping (one construction, three inputs)

Mapping is built per render, deterministically, into `u8 map[NUM_SLICES]`
(+ per-slice REV flag):

1. **Painted order wins** where non-zero: `SliceOrd` par layer, value at the
   slice's first step = source slice 1..S (0 = unpainted). Waypoint idiom.
2. **Seed fills the rest**: SEED != 0 → per-track deterministic shuffle of the
   unpainted positions (per-track RNG keystone / grip_hash idiom: same seed +
   same material = same chop, forever).
3. **REPT** replaces mapped slices with a repeat of the previous *output*
   slice, thermometer by rank; **REV** flags slices for in-slice step
   reversal, thermometer by rank.
4. **STRENGTH gates it**: slices ranked deterministically; strength 0..127 =
   how many ranked slices take their mapped value vs identity. 0 = true
   pass-through, 127 = full resequence. Sweepable morph into the chop.

Slice geometry: GRID = slice length in steps {2,4,8,16}; S = num_steps/GRID.
Mapping window = 16 slices max, wraps for longer loops (typical material is
16 steps; revisit only if long-loop chop is ever wanted by ear).

## The permute pass

`slice_render_range(track, p, par_buf, trg_buf, ...)`:

- Build mapping (above), then permute **in a scratch pass**: for each output
  slice, copy the source slice's step-block from a snapshot of the pre-slice
  buffer (can't permute in place; slice pass needs a stable source. Options:
  small on-stack per-layer row buffer looped per layer/instrument, or read
  from `seq_par_layer_value` source — NO: source lacks upstream processor
  results. Snapshot-per-row loop it is; 16-step row = 16 B on stack, fine
  vs the 83 B task-stack rule).
- **Par layers**: permute all note-bearing/CC layers per instrument.
  **Exclude control-topology layers**: `SliceOrd` itself and `Waypoint`
  (they describe positions, not content).
- **Trg layers**: permute the same step-blocks per instrument (gates,
  accents, rolls travel with their steps). Both buffers are already in the
  dispatch signature (LIMIT takes trg_buf today).
- **Drum mode**: same helper looped per instrument row — this is the whole
  "both materials" cost, one loop level.
- **REV** within a slice: reverse step order of the block in both par + trg.
- Note lengths crossing slice edges: leave uncut for the POC; add a CHOKE
  dial only if the ear demands it (candidate follow-on, see below).

Determinism/live-signal: slicer has NO live input → renders once per dial
change via the normal dirty path; it does NOT join the every-tick force-dirty
set. GOTCHA from rung 3 applies if a sig-driven variant ever lands: sig is
evaluated in tick prologue only.

## Dials + CC map (ext block V5, 0xA4..0xA8 — 12 were free, 7 remain)

All 0-neutral → V5 sessions from before the slicer load correctly, no V6.

| CC | Name | Encoding |
|----|------|----------|
| 0xA4 | `SEQ_CC_SLICE_GRID` | bits 0..2: 0=off, 1=2-step, 2=4-step, 3=8-step, 4=16-step; bit 7 = row bypass (VOICE_SPREAD idiom) |
| 0xA5 | `SEQ_CC_SLICE_SEED` | 0..127; 0 = identity order (painted still applies) |
| 0xA6 | `SEQ_CC_SLICE_REPT` | 0..127 stutter amount (thermometer) |
| 0xA7 | `SEQ_CC_SLICE_REV`  | 0..127 reverse amount (thermometer) |
| 0xA8 | `SEQ_CC_SLICE_STRENGTH` | 0..127 universal sweep (0 = pass-through) |

tcc fields mirror the CCs (chordmask/arp idiom); SliceSlotSync copies into the
slot. Defaults all 0 in `SEQ_CC_TRACK_Init` + SeedRowDefaults registry.

## Painted-order surface

- New `SEQ_PAR_Type_SliceOrd = 25` (21..24 = voicing layers). Value = source
  slice 1..S at the output slice's first step; 0 = unpainted. Emits no MIDI
  (Waypoint precedent). Read from the SOURCE buffer at mapping-build time.
- Storage rides the pattern for free (par layer), like Waypoint.
- **Rack face plane 2** (G2 planes, params2/face2): GP encoders 1..S set the
  order value at each slice position; unpainted shows `--`. Follow the Grve
  Val-brush idiom for hold-step paint + peek if it fits the face budget;
  otherwise encoders-only for act 1.
- Layer assignment ceremony: same as Waypoint (assign a par layer to
  SliceOrd). A3 HIL fixture is 1-par-layer → pins that need painted order
  must use `track_note_init` re-init idiom.

## Rack row

- `proc_rows[]` gains row 11: `{ .name = "Slicer", .abbr = "Slic",
  .rowkind = PROC_ROW_STACK, .stack_slot = SEQ_CORE_SLICE_SLOT }`.
  Position in rack order: after Tens (material processors) — exact position
  to taste at build time; rack is POSITION-based since the redesign
  (row==slot identity GONE), so this is free.
- Plane 1 = dials (Grid/Seed/Rept/Rev + Strength on B-row encoder);
  plane 2 = painted order. LCD faces to be mocked at build time
  (SEQ_LCD_PrintSigned rule; no '+' in vsprintf).
- Control-surface map entry required before any new gesture ships.

## Workflow bundles + GO gates

1. **Drum chop bundle** (headline): 909 pattern on a drum track → GRID on →
   sweep SEED until it grooves → REPT for stutter → CAPTURE the keeper →
   return. GO = does browsing chops feel like performing, not programming?
2. **Melodic chop bundle**: GRAVITY-shaped generated line → chop post-pitch-
   chain → painted order to lock a phrase → CAPTURE. GO = does resequencing
   *heard* material (not source) sound as expected?

## HIL pins (sketch)

- Identity: GRID=0 and STRENGTH=0 both → output mirror == pre-slice render.
- Known permutation: 16-step ramp pattern, GRID=4-step, SEED=k → assert the
  documented block order in mirror (force a render before track_par_get).
- REV: block-internal reversal asserted on ramp.
- Drum mode: two instruments, assert per-instrument permute + drum_mask scope.
- V5 persistence: dials survive save/load in a V5 session; pre-slicer V5
  session loads with all-zero slicer CCs (0-neutral proof).
- Bus-notestack residue rule applies if a jam preceded the run.

## Deferred by choice (do not build in act 1)

- **CHOKE dial** (cut lengths at slice edge) — only if the ear asks.
- **MOTION / per-bar re-roll** (re-seed every N bars) — mechanism known
  (dirty-on-bar + seed=f(bar)), adds the "alive" variant later.
- **Live slice-jump performance gesture** (pads = jump playhead to slice,
  Tension zone-jump idiom) — different machinery (playhead, not render);
  candidate act 2/3.
- Long-loop (>16 slices) painted windows.
