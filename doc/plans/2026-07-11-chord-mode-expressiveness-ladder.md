# Chord-mode expressiveness ladder (post act-1/act-2 arc)

**Context (2026-07-11):** act 1 (Voicing tenant: Sprd/Inv/Strm, LOG cont. 3) and act 2
(chord-space GRAVITY substitution, LOG cont. 4) both shipped by-ear GO same day, HIL
254/254. The user wants the surfaced follow-on ideas captured and built incrementally.
Each rung is its own by-ear gate (§2.7 workflow-bundle discipline); rungs are ordered
by build-cost × musical payoff, but pick by ear, not by list order.

## Rung 1 — Drop + Tilt dials (IN PROGRESS this session)

Two more pure-function dials on the Voicing row, same architecture as act 1:

- **Drop** (0..3 = off / Drop2 / Drop3 / Drop2&4): classic jazz drops — the k-th
  voice(s) FROM THE TOP of the (post-inversion) close voicing down an octave.
  Chain order: Inv → Drop → Sprd (drops apply to the inverted close position,
  spread opens the result). Skipped gracefully when the chord is too small.
- **Tilt** (bipolar ±63, 64-biased CC like Strm): velocity ramp across the chord by
  pitch order — CW accents the top voice, CCW the bottom, linear between, clamped to
  1..127 (never 0: a 0 would rest the voice). Uses the same ascending pitch ranks the
  strum computes.

CCs 0xA2/0xA3 (RAM-only — extends the rung-5 V5 payload to 0xA0..0xA3). Bounce
classification: deterministic SHAPING, preserved (same as the act-1 trio).

## Rung 2 — Per-step voicing par layer (the sleeper; OPEN DESIGN)

A `SEQ_PAR_Type_Voicing` par layer: per-step voicing values, Hapax-style.

**The unlock nobody should forget:** once voicing is a PAR LAYER it lives in the
RENDER BUFFER — so render-stack processors can WRITE it and the result is in the
mirror = bounce-faithful by construction. That is the clean route to "GRAVITY
collapses spread toward unison in DRONE" (rung 3) and to "LFO sweeps spread":
they become ordinary render/LFO targets instead of emission couplings.

Open forks to settle before building:
- **Encoding**: one byte/step — spread nibble + inv nibble? Or separate thin layers?
  (Strm/Tilt per-step too, or dials-only?)
- **Composition with the track dials**: per-step OFFSET on the dial (dial stays the
  performance macro) vs OVERRIDE (step wins). Offset is the grammar-consistent guess
  (dial = macro, layer = automation), decide by ear.
- **EDIT surface**: how a voicing byte reads/edits on the EDIT page (precedent:
  Waypoint layer shipped with plain numeric display).
- The layer emits no MIDI (Waypoint precedent).

## Rung 3 — GRAVITY × Voicing coupling ("collapse, not dropout" for register)

Deep pull narrows *what* the chord is (act 2) — this rung narrows *how wide it sits*:
effective spread scales down through DRONE, toward unison at full pull.

- **Preferred route: via rung 2** (a render pass writes the collapsed spread into the
  voicing layer → mirror-faithful).
- **Interim emission route** (if built before rung 2): scale spread by
  f(gravity, grip) at expansion. Caveat to weigh: gravity is live GLOBAL state, so
  the emission result isn't in the mirror — same faithfulness class as legacy FTS on
  chord tracks, i.e. the thing §3 tries to retire. Grip resets on bounce (frozen
  copies don't re-collapse), so it's *tape-faithful*, not *mirror-faithful*.

## Rung 4 — Ctrl-layer legibility for the voicing CCs

Per-step voicing painting ALREADY works (self-bus Ctrl layer → raw CC 0x9F..0xA3).
Add unit decode (the self-bus legibility pattern, 2026-07-08) so the Ctrl step values
read as `Sprd 3` / `Inv -2` instead of raw bytes. Cheap; superseded by rung 2 if that
ships first.

## Rung 5 — ext-CC block V5 bump (persistence) — SHIPPED 2026-07-11 (cont. 6)

DONE (HIL 256/256 = new baseline): tag 0x05, range 0x80..0xAF, frozen V3/V4=32,
write ladder V5→V4→V3, PhraseReadCCs neutral-extends (strum/tilt=64), snap list
+= inv/drop. Voicing persists only in sessions created by V5 firmware (older
slots degrade the record). Pins: `test_voicing_persist.py`. Outcome owned by
DECISIONS_LOG 2026-07-11 cont. 6.

## Rung 6 — Strum in MIDI export

`seq_midexp` renders unstrummed onsets today (spread/inv/drop ARE exported — they're
in the expansion). Decide: teach export the per-voice tick offset, or document as
accepted (tape capture already hears strum). OPEN_ITEMS §4.

---
*Move to archive when the rungs have all shipped or been re-decided; the decisions
log owns the outcomes.*
