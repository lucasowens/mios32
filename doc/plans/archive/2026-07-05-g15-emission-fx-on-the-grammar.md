# G1.5 — Emission FX onto the operating grammar (Echo first, expose-in-place)

**Status:** ✅ EXECUTED — SHIPPED with a by-ear **GO**, committed main `ce9a9f70` (code) +
the §9 (2026-07-05) decision entry. Scaffolding kept for provenance; the durable home is
the design doc §9. **Date:** 2026-07-05 · builds on
[G1](../MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md) (§9 2026-07-05) and the UX study
`2026-07-02-ux-study-fluid-flow.md` §3.5.

Landed beyond the original Echo scope (all by-ear GO): the shared **encoder-aligned grid**
+ per-unit **fmt** formatting; the `%+3d` signed-print bug fix; **ChordMask engage-from-rack**
(Str turns it on); and **Pitch completed** — global Scale/Root dials + a **diatonic transpose
"Deg"** dial (FTS-gated, `SEQ_SCALE_WalkScale`, persisted `GlobalScaleTranspose`) with a live
`>note` readout of the degree's target tone.

## The reframe (steered by user 2026-07-05)

G1's §9 note queued G1.5 as *"make the FX render-stack processors first (a §3 lift)."*
A 7-agent recon of Echo/LFO/Robotize/Humanize/Groove showed that lift is **wrong for
all five**:

- The render stack is a **fixed-geometry, per-step buffer rewrite**. **Echo** is a
  scheduler (posts feedbacked copies to *future* ticks — no step cell). **LFO** is a
  free-running CC stream (no step to attach; would force render-dirty every tick and kill
  the change-detection zero-duty). **Robotize** is per-event stochastic; half its powers
  (sustain/nofx/+echo/+duplicate) are emission-only. **Groove** is timing (negative
  delays can't be baked). Only **Humanize** *could* move — at the cost of its per-pass
  reroll character.
- **The reason to lift is already spent.** Config-copy (`SEQ_CC_ResetGenerativeForBounce`
  + deterministic re-apply, design §5) already makes them capture-faithful — which is
  *why* robotize was never migrated. §5 explicitly holds "born-as-processors" loosely.

**So G1.5 = expose-in-place (PATH B):** the rack unifies how you *operate* an FX (B-row
select → GP encoders → LED/LCD readout, the exact ChordMask grammar) while the DSP stays
at emission. Zero audio-path risk. The design rule is **bent, not broken** — §5 sanctions
it; the §9 entry will say so.

## The one piece of real infra — rack rows

The rack was a raw walk of 4 `seq_processor_stack` slots. Generalize to an ordered list of
**rows**, each `PROC_ROW_STACK` (occupancy from the stack, as today) or `PROC_ROW_EMISSION`
(occupancy from the effect's `tcc` CCs via a one-line predicate). `ui_focused_proc_slot`
becomes a **row index**; the first 4 rows stay in stack-slot order so row i == slot i and
G1 reads identically. **After this, migrating an effect = adding a row** (a `proc_param_t[]`
table + one predicate in `SEQ_UI_PROC_RowState`).

## First tenant — Echo (4 dials this cut)

| Enc | Param | CC | Range | Pass-through |
|---|---|---|---|---|
| 1 | **Rpt** (repeats, headline/strength) | `ECHO_REPEATS` 0x70, bits 0–5 | 0..15 | **0** (dark row) |
| 2 | **FbN** (fb note) | `ECHO_FB_NOTE` 0x74 | 0..49 | **24** (=0 st) |
| 3 | **FbT** (fb ticks) | `ECHO_FB_TICKS` 0x76 | 0..40 | **20** (=100%) |
| 4 | **FbV** (fb velocity) | `ECHO_FB_VELOCITY` 0x73 | 0..40 | **20** (=100%) |

- **Repeats** is a **masked RMW** (new `PROC_KIND_ECHO_REP`): preserve the `0x40` disable
  bit on write; read masks `&0x3f`. A raw `SEQ_CC_Set` would clobber the flag / flood
  (#59, bit7).
- **Double-tap the Echo row** = toggle the `0x40` disable bit (a real bypass that
  *preserves* the dialed count — no shadow state, unique to Echo's built-in flag).
- **Deferred:** Delay (needs a name-formatter + user↔internal remap — G2 territory),
  Vel.Level (0x72), FB-Gatelength (0x75) — a later encoder bank.

## Build steps

1. `PROC_KIND_ECHO_REP` in `proc_pkind_t`; handle in `ParamRead`/`ParamWrite` (masked RMW).
2. Hoist the 4 param tables to file scope; add `proc_row_t` + `proc_rows[]` (5 rows) +
   `PROC_NUM_ROWS`; rewrite `SlotName`/`SlotParams` to index rows; add
   `proc_rowstate_t SEQ_UI_PROC_RowState(track,row)`.
3. B-row dispatch: bound `PROC_NUM_ROWS`; double-tap → emission branch toggles `0x40`.
4. LED block: iterate rows via `RowState` (stack behavior unchanged; Echo lights when
   repeats>0, winks when bypassed).
5. GP-row mask paint: guard the stack read to the ChordMask row (Echo row index 4 must
   never index `seq_processor_stack[..][4]` — out of bounds).
6. LCD: header count → `PROC_NUM_ROWS`; headline bar scales by param `hi` (works for 0..15).
7. Build + flash + by-ear.

## Depth-first: Echo as the complete reference (2026-07-05, steered)

By-ear feedback: procs work, but "hard to tell how well" — too few params exposed, and
the encoder wasn't visually under its value. Decision: **depth-first** — finish ONE proc
with a genuinely good UI, then roll the template out. Two changes:

- **Encoder-aligned grid (shared, all procs).** The PROC page was left-packing values
  from col 0; now it's a grid — param i in a 5-char cell at col i*5, **label over value,
  directly under GP encoder (i+1)**. Left screen (cols 0–39) = the ≤8-dial bank; right
  screen (cols 40–79) = identity + rack position + BYP + ChordMask's 12-PC mask.
- **Echo fully populated + properly formatted.** All 7 dials (Rpt/Dly/Vel/FbV/FbN/FbT/
  FbG), each in its own unit via a new `fmt` field on `proc_param_t` (the seed of the
  G2 formatter registry): **Delay** operates in musical order and reads as a note name
  ("16"/"8T"/"64d") via `Map*ToInternal`; **Vel/FbV/FbT/FbG** read as **%** (raw×5);
  **FbN** reads as **±semitones** (raw−24). New kinds `PROC_KIND_ECHO_DLY`, fmt
  `PCT5`/`SEMI24`.
- **Engage-seed.** A fresh track's echo feedback params are all 0 → the echo train is
  *silent* (velocity 0%). Turning Rpt 0→on now seeds the neutral "clean repeat" detents
  (Vel/FbV/FbT/FbG=100%, FbN=0st, Delay≈16th) **only when unconfigured** (`echo_velocity
  ==0`), so "dial Rpt up → hear it" works immediately and never overrides shaped values.

## By-ear fixes (2026-07-05, at the bench)

- **Signed readout bug (`%+3d`).** `SEQ_LCD_PrintFormattedString` has NO `+` flag
  (MBSEQV4_REFERENCE.md) — `%+3d` printed the literal "3d" and never consumed the
  value, so Pitch Semi/Oct (and Tension Grav, Echo FbN) showed a stuck "3d". Fixed:
  emit the sign by hand (`%c%d`) via `SEQ_UI_PROC_PrintSigned`. (Inherited from G1.)
- **ChordMask had no engage path from the rack.** It's the one processor gated on the
  track PLAYMODE (`TRKMODE_ChordMask`), so turning Str did nothing until the mode was
  set on the old Track Mode page. Fixed: **turning Str up engages ChordMask playmode**
  (new `PROC_KIND_CM_STR`), so all four stack processors now come alive the same way —
  focus, turn a dial. Double-tap still toggles the mode explicitly. (Design note: this
  clobbers a Transpose/Arp playmode → ChordMask, same as the existing double-tap.)

## By-ear GO / NO-GO

On a running melodic pattern: **LIVE → tap the Echo row → sweep Rpt 0→8** (echoes bloom
from a true-silent pass-through) → **nudge FbN off 24 / FbT off 20** (tail bends & drifts)
→ **double-tap to bypass and back** (count preserved).

- **GO** if operating Echo through the ChordMask grammar feels like *one instrument* (no
  page hunt, LED honest, sweep musical). → license migrating the rest (Groove next — the
  config-copy archetype).
- **NO-GO** if it reads as a worse dedicated FX page. → the fix is a formatter table (G2),
  not more rows.

**Gotcha:** Echo also needs the track's `no_fx`/`NOFX` off (default). If a swept Rpt is
silent, check that first.

## Adversarial review (15-agent find→verify, 2026-07-05) — fixed + latent

**Fixed before flash:**
- **FbN clean sweep** — capped `hi` 49→48. Value 49 is a *random-pitch mode*, not a
  pitch extreme; an unlabelled top-detent discontinuity violates the "clean 0→max
  sweep" rule. A labelled Rnd selector is G2 formatter work.
- **Bypass legibility** — a bypassed Echo with count>0 (`0x40` set, reachable via the
  FX page / a preset / the B-row double-tap) printed "Rpt N" + a filled bar while
  silent (ParamRead strips the disable bit), and the Rpt dial can't clear `0x40` — a
  silent-echo trap with no steady LCD cue (the LED only winks). Added a steady **BYP**
  marker on the PROC header, driven by `RowState` (occupied && !enabled), so it also
  covers any future emission row.

**Latent (noted, NOT fixed — not reachable today, deferred to G2 descriptor work):**
- The emission double-tap and `RowState` hardcode `SEQ_CC_ECHO_REPEATS`; a *second*
  emission row would mis-target Echo's disable bit. Generic bypass needs a per-row
  descriptor field (G2). Migrating a 2nd emission FX must touch these echo-specific
  spots — the "just add a row" recipe understates that for emission rows with a
  native disable bit.
- The ChordMask GP-mask paint + LCD "Mask:" readout compare the **row index** to the
  **stack-slot constant** `SEQ_CORE_CHORDMASK_SLOT` (correct only because row 1 == slot
  1). A future reorder/prepend of `proc_rows[]` would silently attach the mask to the
  wrong row. Guard the intent with `.rowkind`/`.stack_slot` when the table grows.
