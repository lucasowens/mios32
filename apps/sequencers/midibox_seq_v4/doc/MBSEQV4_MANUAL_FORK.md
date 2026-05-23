# MIDIbox SEQ V4 — User Manual — Fork Additions

This manual documents the features added to MIDIbox SEQ V4 in this fork, on top of upstream V4.098. Each section follows the style of the official ucapps.de manuals (intro, usecases, action-named subsections with 2×80 LCD mockups, Tips & Tricks footer).

Three features are documented here:

1. **Robotize Loop** — bar-anchor PRNG control over the Robotizer Fx (sculpt randomness one measure at a time)
2. **GENERATE Page** — the former EUCLID page expanded into five generator types (Euclidean, Cellular, Polyrhythm, Subdivide, L-System)
3. **Bounce-in-Place** — capture the live MIDI output of a track into a pattern slot via the PATTERN button

For developer-facing topics (the testctrl SysEx interface, the Python hardware-in-the-loop pytest harness, the top-level orchestration Makefile, and the source-level design catalog) see `MBSEQV4_REFERENCE.md` and `tests/README.md`.

---

## Robotize Loop

The Robotizer Fx (added in V4.088) replaces a step's note/velocity/length with random variations at playback time. Without a loop, every pass through the pattern produces fresh randomness — useful for liveness, but the moment a happy accident lands, the next bar erases it.

The Robotize Loop adds a per-track PRNG bar-anchor array. Each measure of the loop has its own seed, so you can lock individual measures into a repeatable shape, reroll one anchor without disturbing the others, or freeze the last N measures retroactively when the music just landed where you wanted it.

### Usecases

* Audition robotize live, then freeze the last 2 or 4 bars into a locked loop when you hear the right one — without stopping playback.
* Build an evolving 16-bar loop where 12 measures are locked and 4 are still randomizing, then reroll one anchor at a time until you like the whole thing.
* Polymetric tracks share the same loop clock (anchored to global ref_step == 0), so a 7/8 track and a 4/4 track lock to the same measure boundaries even though they drift against each other.
* Persist the locked anchors with the pattern so reopening the session reproduces the same loop, not a new random sequence.

### Robo Page — Two Sides of the Same Coin

The FX submenu's "Robo" entry now opens a single conceptual page with two togglable sides:

* **FX_ROBOTIZE** — the original probability/mask page (Probability, Skip, Octave, Note, Velocity, Length ranges; per-step mask).
* **ROBOLOOP** — the loop sculpt page (palette, window, anchors, freeze/reseed/reroll).

GP16 on either side jumps to the other; the label flips between **`Loop`** (on FX_ROBOTIZE) and **`CCs `** (on ROBOLOOP). SELECT+GP16 keeps its original meaning on both pages (mask XOR on FX_ROBOTIZE; reroll bar 15 on ROBOLOOP).

### ROBOLOOP Page Layout

```
 Trk. Len. Strt Loop Rot. Resd Frz  FrzQ            Anchors:  * play  # loop  + palette  CCs
 G1T1   8    0    4    0  rsd  frz  frzq            S Phase 0/4   . . . . * # # # + + + + . . . .
```

* **GP1 — Trk.** select track (G1T1 … G4T4)
* **GP2 — Len.** palette length, 0–16 anchors (total measures of pre-rolled randomness in the pool)
* **GP3 — Strt** loop start, 0–15 (which anchor index is the head of the loop window)
* **GP4 — Loop** loop cycles, 0–16, where `0` disables the loop (free-running robotize)
* **GP5 — Rot.** phase rotation inside the window, 0–15 (offsets the start position without moving the head)
* **GP6 — Resd** reseed the palette with fresh independent randoms
* **GP7 — Frz** freeze: copy the last K live snapshots into the palette starting at Strt (jump-now — takes effect immediately)
* **GP8 — FrzQ** freeze, quantized — same as Frz, but the swap happens at the next measure boundary (gap-free)
* **GP9 — Bnc** bounce-in-place to the next free pattern slot (see Bounce-in-Place section)
* **GP16 — CCs** jump to the FX_ROBOTIZE page

SELECT+GP1..16 = reroll anchor 0..15 (replace one anchor with a fresh random while leaving every other anchor locked).

The right LCD shows the 16-character anchor grid. Each character is one anchor's state:

* `*` — the anchor currently playing
* `#` — anchor is inside the loop window
* `+` — anchor is in the palette but outside the window
* `.` — anchor is inactive

The `S` indicator in front of `Phase N/M` lights when master-sync is on (anchor phase aligned to the song-level guide-track wrap instead of the track's own length).

### RESEED: Throw the Whole Palette Away

Press **GP6 (Resd)** to fill every anchor in the palette with new independent random states. The currently playing measure switches at the next playback wrap; held notes hand off cleanly. Use this when the entire loop has gone stale and you want a fresh take.

### FREEZE: Lock In the Last Few Bars

The robotizer keeps a 16-deep snapshot ring of its live PRNG state, captured at each global measure boundary. Freeze copies the last K snapshots from the ring into the palette starting at the current loop start.

* **GP7 (Frz)** — jump-now: the new anchors take effect immediately. There can be an audible discontinuity at the next step.
* **GP8 (FrzQ)** — quantized: the swap is queued for the next measure boundary, so there is no audible jump.

The retroactive design is the point — you decide to keep what just played *after* you heard it, not before.

### REROLL: One Anchor at a Time

Hold **SELECT** and press **GP1..16** to replace anchor 0..15 with a fresh random. Every other anchor stays locked. Use this to sculpt a fixed loop one bar at a time: lock the whole thing with Freeze, then keep rerolling the one bar that isn't working until it does.

### MASTER-SYNC: Align to the Song

By default the loop clock ticks on global ref_step == 0 — the meter, not the song. In song mode you can also lock the loop phase to the guide track's wrap, so a 4-bar loop on track 1 stays in step with the song's intended measure 1 forever.

Enable via the terminal:

```
robotize master-sync 1 on
```

### Scale-Aware Robotize

When the track has FORCE_SCALE enabled (FX → Scale), the robotizer's note shift walks scale degrees, not semitones. The shift range is halved internally so the value distribution stays uniform around the original pitch without clustering at the clamp boundaries.

The two new scale helpers are `SEQ_SCALE_WalkScale()` and `SEQ_SCALE_PrevNoteInScale()` (see [seq_scale.c](../core/seq_scale.c)); the rest of the FX pipeline is unchanged.

### Track-Edit Page Indicator

When a track has an active robotize loop, the existing context-tag slot on the EDIT page (where `*LOOPED*` or `*Sect.X*` appears) flashes:

```
 G1T1 *RbLp*  Len. 256  Notes: Pop1   Vel:100  Gate:75  Skip: 0
```

### Terminal Commands

Every loop operation is also available from the MIDI Studio terminal:

```
robotize reseed       <track>
robotize reroll       <track> <0..15>
robotize freeze       <track> [K]      # jump-now, K defaults to loop length
robotize freeze-q     <track> [K]      # quantized
robotize length       <track> <1..16>
robotize start        <track> <0..15>
robotize loop         <track> <0..16>  # 0 disables loop
robotize rotate       <track> <0..15>
robotize master-sync  <track> on|off
robotize status       <track>          # print palette, window, all 16 anchors, phase
```

`status` is the diagnostic — it prints the full loop state including the raw 32-bit value of every anchor, so two boards running the same session can verify they reproduce identical loops.

### Persistence

Robotize CCs (0x80..0x95) and the 64-byte per-track bar-anchor array are persisted in a per-track v2 extension block inside each pattern slot of `MBSEQ_B1..B4.V4`. The block is appended after the trigger layers; the upstream firmware doesn't see CC ≥ 0x80 and would have discarded everything otherwise.

The block is forward and backward compatible:

* Older firmware reading a session created by this fork sees the extension bytes as harmless padding (the leading tag byte makes the slot size still parse).
* This fork reading a session created by older firmware finds no tag byte and skips the extension cleanly — robotize and anchors come up at defaults instead of corrupting.
* A brief v1 format (tag `0x01`, anchors only) is also accepted on read for backward compatibility with the development branch.

### Tips & Tricks

* The 16-deep snapshot ring is wider than the maximum 16-cycle loop, so Freeze can always look back as far as the loop length allows.
* Reseed throws away the in-window anchors but does not touch the snapshot ring — you can Reseed and immediately Freeze if you wanted the random-then-frozen pattern.
* Loop cycles = 0 turns the bar anchors off entirely and falls back to the original V4.088 behavior (fresh randomness every step).
* If a session has been saved by this fork and you read it on stock V4.098, robotize will appear inactive — the CC bytes are in the v2 extension block, not the base CC table. Re-enabling robotize on the older firmware is harmless but won't recover the bar anchors. Re-saving on the older firmware writes the bank without the v2 block; reload on this fork comes up with defaults again.
* `robotize status` over the terminal is the fastest way to confirm a save survived a power cycle.

---

## GENERATE Page

The Track Euclidean Generator page (introduced V4.059) has been expanded into a generic **GENERATE** page. Euclidean placement is now one of five generator types selectable per trigger layer per track. The page name in the EXIT menu is **Gen.** (the legend on the EDIT page's datawheel mode line changed from `Euclid` to `Track`).

### Usecases

* Quickly seed a new pattern with a 5-in-8 Bjorklund / Euclidean rhythm, then switch the same track's generator to Cellular Automaton without leaving the page.
* Stack a 3-against-8 polyrhythm on top of a 5-against-7 polyrhythm using one trigger layer (Polyrhythm with PG2 enabled).
* Build organic-feeling textures with the L-System generator's eight rewrite-rule presets, then dial the seed knob until the iteration lands somewhere musical.
* Tweak a recursive Subdivide depth from 1 to 8 to grow patterns from "two halves" to "256 cells" — useful for hi-hat figures.

### Page Layout

```
 Trk. TrkLength    Layer ParAval R.acc%   N    M  Phase  Type   Preview Triggers
 G1T1  16  16       ParB    Orig    20    3    8    0    Poly   * . . * . . * . . * . . * . . *
```

* **GP1 — Trk.** select track
* **GP2 / GP3 — TrkLength** track length (two encoder positions, both adjust the same field)
* **GP4 — Layer** parameter or trigger layer selector (drum instrument or layer A..P on non-drums)
* **GP5 — ParAval** parameter value (constant offset for non-drums; `Orig` keeps the existing layer A value)
* **GP6 — R.acc Min** random-accent low velocity / drum vel-N
* **GP7 — R.acc Max** random-accent high velocity / drum vel-A
* **GP8 — R.acc%** random-accent probability, 0–100% in steps of 10
* **GP9 / GP10 / GP11** — generator-specific parameters (see per-type tables below)
* **GP12 — Type** generator type selector: `Eucl`, `CA`, `Poly`, `Sub`, `Lsys`
* **GP16 — n/N** sub-page toggle for generators that have a second parameter page (CA, Poly). LED lit when on PG2.

The right LCD displays the live trigger preview: `*` = trigger + accent, `.` = trigger only, `-` = step disabled, space = empty. The preview updates as you scroll any parameter.

### Choosing a Generator Type

Turn **GP12** to cycle through the five generator types. Each type stores its own parameters per trigger layer, so switching from Euclidean to Cellular and back returns to the original Euclidean parameters.

```
 GP12  =>  Eucl    Cellular   Polyrhythm  Subdivide  L-System
            (0)       (1)         (2)        (3)        (4)
```

### EUCLID: Bjorklund Placement

```
                                Length  Pulses  Offset
 GP9 / GP10 / GP11    ........    0..N    0..N    0..N
```

The original V4.059 Euclidean generator, unchanged. Length is the cycle, Pulses is how many active beats to spread evenly across it, Offset rotates the result. No PG2.

### CA: Cellular Automaton

A 1-D elementary Wolfram cellular automaton. Each generation is a row of cells (one cell per step). The next row is computed by applying the 8-bit `Rule` to each cell's neighbourhood. After `Gens` generations the final row is written to the layer.

```
                                Rule    Seed   Gens     (PG2)  Bound.
 PG1: GP9 / GP10 / GP11           0..255  1..255 0..32           Wrap / Zero / Mirror
 PG2: GP9                                                        (set on PG2 only)
```

* **Rule** — Wolfram rule number (0..255). Rule 30 is the default.
* **Seed** — initial-row PRNG seed (1..255; 0 is reserved).
* **Gens** — how many generations to evolve (max 32).
* **Boundary** (PG2 → GP9) — what happens at the edge of the row:
  * `Wrap` — cells wrap around (default, gives circular patterns)
  * `Zero` — assume zero outside the row (edges decay)
  * `Mirror` — reflect the row at its edges (symmetric patterns)

Computation cost is bounded at `gens × num_steps`, capped 32 × 256 = 8192 cell updates.

### POLY: Polyrhythm

A Bresenham-style "N pulses spread evenly across M steps" distribution, tiled across the full track length. PG2 unlocks a second polyrhythm layer that is OR'd with the first, so you can stack e.g. 3-against-8 on top of 5-against-7 in one trigger layer.

```
                                N       M       Phase     (PG2)  N2    M2    Phase2
 PG1: GP9 / GP10 / GP11           0..M    1..256  0..M-1
 PG2: GP9 / GP10 / GP11                                            0..M2 1..256 0..M2-1
```

* **N** / **M** — pulses-over-cycle (e.g. N=3, M=8 = three evenly placed beats every eight steps)
* **Phase** — rotates the cycle
* **N2 = 0** disables the second layer (defaults state)

### SUB: Recursive Subdivide

Builds a 2^depth pattern by recursive binary splitting; each half survives with `Prob`%. Tiles across the track.

```
                                Depth   Prob    Seed
 GP9 / GP10 / GP11                1..8    0..100  1..255
```

* **Depth** — recursion depth (1 = two halves; 8 = 256 cells)
* **Prob** — survival probability per split
* **Seed** — local PRNG seed; isolated from the live robotize PRNG so the generator doesn't disturb playback randomness

Computation cost is bounded at 2^depth ≤ 256.

### LSYS: L-System Rewrite

Eight hardcoded `(axiom, '0'-rule, '1'-rule)` rewrite presets. Each iteration expands the string by replacing every character with its rule. After `Iter` iterations the resulting string (read as 0/1 trigger states) is laid down on the track.

```
                                Preset   Iter    Seed
 GP9 / GP10 / GP11                0..7     0..6    1..255
```

* **Preset** — pick one of eight rule sets (each named on the LCD as it's selected)
* **Iter** — how many expansion passes (0 = axiom only; 6 = deeply elaborated)
* **Seed** — rotates the read offset into the expanded string, so you can audition different "windows" of the same iteration

The expansion uses two ping-pong 512-byte buffers; iterations halt early if the next pass would overflow.

### Tips & Tricks

* GP16 only toggles when the current generator has a PG2; otherwise the button does nothing and the LED stays dark. CA and Poly have PG2; Euclid, Sub, L-System don't.
* The trigger preview row stays live across PG1↔PG2, so you can see how the boundary mode (CA) or the second polyrhythm layer (Poly) changes the output.
* Switching generator type re-runs immediately with the new type's stored parameters; it does not write to the layer until you actually nudge an encoder. So glance at the preview first.
* Accent layer regeneration honors the random-accent settings on every type, not only Euclidean. Set R.acc% to 0 if you want pure no-accent output.
* For drum tracks, the layer selector picks an instrument (CH, OH, BD, …) and the parameter shown is that drum's velocity, not a parameter layer letter.

---

## Bounce-in-Place

Live tweaks on a sequencer are non-destructive by design — you can morph, robotize, echo, and duplicate without ever modifying the underlying pattern. The trade-off is that good takes can disappear the moment the playhead wraps.

Bounce-in-Place captures the actual live MIDI output of the current track — *after* all Fx — into a pattern slot. The captured slot is a frozen take with the Fx stripped; further morph/robotize/echo on the original track no longer modify the bounce.

### Usecases

* Audition robotize on a track until it lands somewhere magical, then bounce that take into a free pattern slot and recall it later un-randomized.
* Build complex layered output from echo + duplicate + LFO, then bounce the result into a clean track that doesn't need the Fx chain.
* Capture an improvised solo (live recording into a track with morph) as a static pattern.

### How the Capture Works

A small in-RAM ring buffer per track tracks every emitted MIDI event (note on/off, CC, pitch bend, program change, aftertouch) at the moment they go out the wire. The buffer is always armed — there is no warm-up button.

When you trigger a bounce, the firmware:

1. Snapshots the last N measures of the ring.
2. Strips Fx (robotize / echo / humanize / LFO Extra) so the bounce is a deterministic playback.
3. Writes the snapshot to the destination pattern slot.
4. Reloads the source pattern so playback continues seamlessly.

The capture tap is one branch on a `u8` flag in the hot path when no track is armed, and one packed write when armed; commit work runs in user-task context, not the tick path.

### One-Step Bounce: PATTERN + GP

The fast path. While playing:

1. **Hold PATTERN** — the LCD overlays a hold prompt.
2. **Tap GP1..GP8** — selects the destination letter (A..H within the current group).
3. **Release PATTERN** — the bounce fires into the chosen letter's currently selected number.

A confirmation flashes on the LCD:

```
                                                                          > A3  OK
```

(meaning: bounced into group A, pattern 3).

If you release PATTERN without tapping any GP, the bare press is interpreted as a navigation request and the Pattern page opens — the original V4.0beta1 behavior, preserved.

### Two-Step Bounce: PATTERN + GP1..8 + GP9..16

For an explicit letter + number choice:

1. **Hold PATTERN.**
2. **Tap GP1..GP8** — stash the letter (A..H).
3. **Tap GP9..GP16** — pick the number (1..8). Bounce fires immediately on this press.
4. **Release PATTERN.**

The two-step form is useful for live performance: you can pre-arm the letter, then pick a number when the music tells you to.

### Bounce From the ROBOLOOP Page

The ROBOLOOP page (see Robotize Loop section above) also has a GP9 shortcut labeled **Bnc**:

```
 ...  Resd Frz  FrzQ Bnc                                                        ...
```

Pressing GP9 picks the next free slot in the current pattern group's bank automatically. If no slot is free, the LCD overlays a picker:

```
                       PICK SLOT  GP1-8 (any other GP cancels)
                                  press GP1..GP8 to write into pattern 1..8
```

Press GP1..GP8 to select the destination; press any other GP (or EXIT) to cancel.

The default bounce length from this shortcut is whatever was last configured by the two-step UX — usually 1 measure.

### What Gets Bounced

* **Notes** — note on / note off events including the velocity emitted at the moment of playback (so robotized velocities are captured as fixed values).
* **CC** — all CC messages, including LFO-modulated CCs.
* **Pitch bend, program change, aftertouch.**
* For **drum tracks**, the bounce captures the per-instrument output (BD, SD, CH, …) and writes each instrument back to its drum slot in the destination.

### What Does *Not* Get Bounced

* The Fx parameters themselves — robotize probabilities, echo settings, humanize amounts — are reset on the destination. The bounce is the *output* of those Fx, not their config.
* Anything that wasn't actually emitted during the captured window. A note that should have played but was suppressed by robotize-SKIP will be missing.
* MIDI events received over loopback into bus tracks; the capture tap is on emission, not on the bus.

### Tips & Tricks

* The capture ring is fixed-size and rolls over silently. If you bounce after a long quiet period, the captured window may be partially stale — bounce promptly after the take you want.
* The bounce strips Fx, so you can bounce a robotized take into a slot, then bounce the bounce again with a different Fx configuration without compounding.
* If you've armed a long bounce length (8+ measures), expect the commit to take noticeably longer than a one-measure bounce — the work is hoisted out of the tick path so playback doesn't stall, but the destination won't be playable until commit completes (~100ms scale).
* The bounce shortcut on ROBOLOOP is the fastest path during a live set: one GP9 tap and the current robotized take is locked into the next free slot, no menu navigation.

---

## Cross-Cutting Notes

### Where files live

| File | Purpose |
| --- | --- |
| `MBSEQ_B1..B4.V4` | Pattern banks. The fork stores its v2 per-track extension block (robotize CCs + bar anchors) appended inside each track slot. |
| `MBSEQ_C.V4` | Session config. Generator state (per-layer type, parameters, PG selection) lives here. |
| `MBSEQ_GC.V4` | Global config. Unmodified by the fork. |
| `MBSEQ_BM.V4` | Bookmarks. ROBOLOOP and GENERATE are bookmarkable; existing bookmarks survive the GENERATE page rename. |

### Page IDs and bookmarks

`SEQ_UI_PAGE_ROBOLOOP` is a new page id. Its `old_bm_index` is fresh, so existing bookmark files don't accidentally bind to it. The renamed GENERATE page keeps the original Euclid `old_bm_index` so bookmarks made on the upstream firmware still resolve.

### CC space additions

CCs added by the fork:

| CC | Purpose |
| --- | --- |
| 0x91 | `robotize_loop_cycles` |
| 0x92 | `robotize_sync_to_master` |
| 0x93 | `robotize_palette_length` |
| 0x94 | `robotize_loop_start` |
| 0x95 | `robotize_loop_rotate` |

All five live in the v2 ext block (above 0x7f) and follow the upstream V4.088 robotize CC range (0x80..0x90).

---

*Last update: 2026-05-23*
