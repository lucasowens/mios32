# MIDIbox SEQ V4 — User Manual — Fork Additions

This manual documents the features added to MIDIbox SEQ V4 in this fork, on top of upstream V4.098. Each section follows the style of the official ucapps.de manuals (intro, usecases, action-named subsections with 2×80 LCD mockups, Tips & Tricks footer).

Seven features are documented here:

1. **Robotize Loop** — bar-anchor PRNG control over the Robotizer Fx (sculpt randomness one measure at a time)
2. **GENERATE Page** — the former EUCLID page expanded into five generator types (Euclidean, Cellular, Polyrhythm, Subdivide, L-System)
3. **Capture / Bounce** — freeze a track's computed output (lossless) into a pattern via the PATTERN-hold gesture; build variation libraries and multitimbral canvases
4. **The Pull** — load one stored track into any live track, on the bar, while everything keeps playing (the mirror of Capture); SELECT+CLEAR is the one-gesture undo
5. **Fearless Switching** — your work always saves itself; CHECKPOINT blesses a safe point and REVERT snaps the whole living rig back to it in one gesture
6. **FREEZE** — a global master switch (the repurposed METRONOME button) that stops the generators mutating, so a recalled phrase plays as static tape instead of drifting
7. **Phrases** — a snapshot library of the whole living rig; tap to recall a phrase, hold to capture the current one (paired with FREEZE for static-vs-alive recall)

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
* **GP16 — CCs** jump to the FX_ROBOTIZE page

(The old GP9 "Bnc" shortcut was removed when bounce was unified onto the PATTERN-hold gesture — see the Capture / Bounce section. GP9–GP15 are now unused on this page.)

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

## Capture / Bounce

Live tweaks are non-destructive by design — you can run generators and processors, morph, echo, robotize, and duplicate without ever modifying the underlying pattern. The trade-off is that a good take vanishes the moment the playhead wraps. **Capture** (bounce) freezes a track's *computed output* — the exact rendered notes, velocities, lengths, and CC of the loop, lossless — into a pattern on the SD card, so you can keep it, recall it, and build with it.

This is the unified, lossless capture model; it replaced an earlier lossy "emission tape". What you capture is the post-processor render — the loop as the engine computes it each tick — taken *before* the emission stage, so loopback / echo / global-transpose applied at output are not baked in. The captured slot is the editable computed loop, with its generative modulation neutralized so it plays back deterministically.

### Usecases

* Run a generator or processor on a track until it lands somewhere magical, then capture that take into a pattern and recall it un-randomized later.
* Build a **variation library**: keep a live "generator" track running, tweak it, and bounce snapshots into new patterns — *without ever disturbing the live track* — then load a variation when you want it.
* Build a **multitimbral canvas**: bounce successive takes onto the tracks of another group, each track on its own MIDI port/channel, layering a full pattern that drops in locked to the beat.

### The PATTERN-hold gesture

The capture trigger. **Source = the visible track.** While playing:

1. **Hold PATTERN.** The LCD overlays the capture prompt (`CAPTURE Tn -> Tm`).
2. *(optional)* **Tap a select-row button** to pick the **destination track** — which track of the destination pattern receives the capture. Default = the source's own track index.
3. **Tap GP1..GP8** — destination **group letter** (A..H). Default = the destination group's current letter.
4. **Tap GP9..GP16** — destination **pattern number** (1..8). This **commits** the capture, saved to SD.

A confirmation flashes (e.g. `saved A C5.T3`). A bare PATTERN tap with no GP opens the Pattern page — the original behavior, preserved.

What the commit does is decided by **one rule**, steered purely by which destination track you aimed at:

* **Destination in the SOURCE's own group** (including the default — no select-row press): **save only.** The capture is merged into the chosen pattern on SD; your live source keeps playing, untouched. The variation-library move — *tweak → bounce → tweak → bounce* to bank takes without disturbing what you're tweaking. Select a variation deliberately (Pattern page) to play it.
* **Destination in a DIFFERENT group:** the capture is written into the **destination group's own bank**, and that group is **auto-loaded on the next bar** (locked to the master), so the take drops in immediately and plays — for auditioning a variation in a spare group, or building a canvas. Your source group is never the one loaded, so it never jumps.

In every case the capture **merges** into the destination pattern: it replaces only the one track-position you aimed at and keeps the pattern's other three tracks. So you can bounce different takes into T1, T2, T3, T4 of the *same* pattern and build it up over several bounces.

### In-place freeze (GP8 on the PITCHGEN page)

A separate, simpler verb for freezing a generator/processor onto the track it is already running on, with no destination pick. On the PITCHGEN page, **GP8** freezes an engaged generator — or, failing that, an enabled processor's output — directly into the visible track's own source layers (the dial returns to zero and the frozen notes *are* the buffer). With nothing engaged, the LCD shows "nothing to bounce here".

### What Gets Captured

* **Notes, velocity, length, and all CC** — the exact rendered output of the loop, lossless. (The earlier emission-tape model dropped CC and grid-snapped onsets; this does not.)
* **Note and drum tracks capture identically** — the capture/render path is mode-agnostic. For a drum track the per-instrument output is captured.

### What Is Neutralized

So the frozen take plays back deterministically, generative modulation on the captured copy is reset via `SEQ_CC_ResetGenerativeForBounce()` ([seq_cc.c](../core/seq_cc.c)) — the genuinely-generative bits (e.g. `random_gate` / `random_value`) and the generator/processor configuration. **Structural** assignments are *preserved*: which trigger layer is the Gate/Accent/Glide/Roll/Skip, and the par-layer *type* assignments. (Preserving the gate assignment is essential — a captured pattern keeps its gate layer and plays the captured rhythm, rather than firing a note on every step.) When a new generative feature lands, extend that function in the same review.

### Tips & Tricks

* Same group = build a library (save-only, the source is never disturbed); different group = audition / canvas (auto-loads on the bar). You choose which purely by where you aim the destination track.
* A cross-group capture lands in the **destination group's own bank** — navigate that group to find it; it persists across pattern switches.
* The capture is non-destructive to the source in every case — the live track you're tweaking is never replaced by a bounce.
* A canvas track keeps its own MIDI port/channel across re-bounces (capture leaves the port/channel CCs alone), so set the routing once and bounce notes into it repeatedly.
* It's the same lossless capture whether the source is a hand-drawn line, a running generator, or a processed track — *bounce → tweak → bounce* to accumulate a set of patterns to perform with.

---

## Tension Workbench (GRAVITY)

One bipolar knob bends the whole room's harmony — *toward* consonance (pull) or *into*
structured tension (push) — and a button resolves it on the downbeat. Instead of dials
that only make material *more correct*, GRAVITY is a ranked field you push notes *along*
in either direction.

### Reaching the page

From the **FX → Scale** page press **GP16** to toggle to the GRAVITY cockpit (the
harmonic sibling of that page). It's also in the page menu as **GRAVITY**.

### The dials

- **GP1 — GRAVITY** (global, −64…+63). Center (0) is the **detent**: true pass-through,
  material untouched. **Left/CCW pulls:** SCALE (tidy to the key) → CHORD (lock to the
  held chord) → DRONE (collapse toward the root). **Right/CW pushes:** LEAN (sus/add
  color) → RUB (chromatic tension a semitone off the chord tones) → SLIP (the whole set
  side-slipped a semitone).
- **GP3 — GRIP** (per track). How strongly *this* track is held — a send-amount,
  set-and-forget. 0 = the track ignores GRAVITY; raise it to pull the track in.
- **GP2 — SHADE.** Steps the global scale along a brightness ladder
  (Lydian→Ionian→Mixolydian→Dorian→Aeolian→Phrygian→Locrian) — the *terrain* the field
  moves across (force-to-scale tracks follow it too).
- **GP8 — RESOLVE.** Ramps GRAVITY back to the detent, landing on the next downbeat —
  tension resolved into the One, on the beat. (Instant when stopped; turning GP1 cancels
  an in-flight resolve; **stopping the transport mid-ramp lands it at the detent
  immediately** — same "no bar to ramp into" rule.)
- **GP16** — back to FX → Scale.

### Reading the meter

The panel knobs are endless **encoders** — no felt center — so the right LCD shows a
**bipolar meter**: `|` is the detent (home), the fill grows **left as you pull / right
as you push**, its length is how deep you are, and the `:` ticks (turning `+` once
crossed) mark the zone boundaries. The zone name + signed value sit alongside.

### Notes

- **Force-to-scale and GRIP now coexist on one track** (since the pitch-chain
  migration, 2026-06-10): FTS tidies *before* the field, so a push lands on the
  tidied material and **survives** — the old "turn FTS off on gripped tracks" rule
  is retired. Sweep with both on; the pull side and FTS agree by construction.
- **GRAVITY/SHADE are performance state** (session config, like the global scale), not
  per-pattern. **GRIP is per-track** and saved with the pattern.
- A pushed groove **bounces faithfully** — capture a gripped track and the heard
  (pushed) pitches are in the notes, no special handling.

---

## The Pitch Chain (transpose · force-to-scale · note limit)

Since 2026-06-10 the per-track pitch chain — **transpose** (including live
bus-planing in Transpose track mode), **force-to-scale**, and the **note limit**
(FX → Limit) — is computed *into the track's playing material*, not sprayed on at
the MIDI output. You'll feel it in three places:

- **What you see is what you hear.** The EDIT page shows the *sounding* pitch —
  planed, snapped, folded — not the raw stored note.
- **Planing stays in key.** In Transpose mode with FTS on, the snap happens *after*
  the bus offset, so planing a groove from a keyboard never walks it off-scale.
- **Capture is always faithful.** Bounce/capture a planed, snapped, limited,
  gripped groove and the frozen copy holds exactly the heard pitches, with the
  chain's dials reset on the copy — recall it cold and it *is* that moment.
  (Previously a captured planed track silently lost its planing.)

Edge behaviors worth knowing: with **no key held in Transpose mode** the track is
silent and shows rests (with HOLD on it keeps following the last key, as always);
**Arpeggiator tracks and drum tracks keep the old output-side chain** (unchanged
feel); humanize/LFO note-wander still lands in-scale and in-window on FTS/limited
tracks.

---

## The Pull (track-grain load)

Patterns store and load four tracks at a time — until now, getting one stored
track back meant loading its whole pattern into a group, four tracks moving
together. The Pull is the missing move: **load ONE stored track into ANY live
track, landing on the bar, while everything keeps playing.** It is the mirror
of the PATTERN-hold Capture: Capture pushes one track *into* storage; the
Pull brings one track back *out* — any bank × pattern × section, onto any of
the 16 live tracks. Nothing else moves: the destination group's other tracks,
the loaded-pattern display, and everything you're tweaking stay untouched.

Together they make storage a two-way shelf: jam, capture the good moments,
and any future night pull them back under whatever is running.

### The gesture

1. **Hold a select-row track button** — that track is the destination. (A
   quick tap is still a plain track select, unchanged.)
2. *(optional)* **Tap another select-row button** — the source *column*
   (column = bank × section: buttons 1–4 are bank 1's four track slots,
   5–8 bank 2's, and so on). Default: the held track's own column.
3. **GP1–8** — the source pattern letter (A–H). Default: the letter that
   column's group currently has loaded.
4. **GP9–16** — the pattern number, and **COMMIT**: the stored track drops
   onto the held track at the next measure. Keep holding and tap other
   numbers to walk variations bar-by-bar.
5. Release: the held track becomes the selected track — the cursor follows
   the transfusion, ready to tweak.

While aiming, the LCD shows the pull overlay (source column and letters on
the left display, numbers and the last result on the right), mirroring the
Capture overlay.

### SELECT + CLEAR — one gesture back

Every pull first snapshots the destination track (notes, gates, full config,
name). **SELECT+CLEAR restores it, on the bar** — pulled the wrong thing, or
done with the experiment? One gesture and the track is back. One-deep: the
most recent pull wins. With nothing to undo, SELECT+CLEAR just says so — it
**never** falls through to a destructive clear.

### Tips & Tricks

* A wrong pull costs one bar + SELECT+CLEAR — cheaper than remembering what's
  where. Aim, listen, undo. Storage coordinates carry no musical commitment,
  so organize banks like crates (kicks in one column, basses in another) and
  let muscle memory do the live navigation.
* Pulling works on running material: the bar-aligned drop means a kick swap
  lands like a quantized clip launch.
* The pulled copy carries its full stored config — length, groove, drum
  layout, even GRIP — so it sounds the way it was captured, wherever it lands.
* Capture and Pull are designed as a pair: PATTERN-hold to bank a moment
  mid-jam, track-hold to bring history back under tonight's material.
* Heads-up: while a track button is held, the other select-row buttons aim
  the source column, so multi-track chord-select is unavailable during a
  pull. Release the hold and it's back.

---

## Fearless Switching

The old rule was: switch patterns and your unsaved tweaks are gone unless you
remembered to SAVE first — and you never remember mid-jam. This fork inverts
it. **Your working state always saves itself.** Edit a track, sculpt a
generator, then switch a group away and back — it comes back exactly as you
left it, including a living generator that keeps mutating right where it was.
Nothing is lost on a switch, ever.

Because saving is automatic, protection becomes the deliberate act:
**CHECKPOINT** blesses a safe point, and **REVERT** snaps the whole rig back to
it in one gesture.

### What persists, automatically

Whenever you switch a group, its current live state — notes, gates, all
config, and any engaged generator — is written back to its working slot first.
So switching is free: roam around, come back, keep going. The same happens
when you change sessions. The only stretch not yet written is the one playing
right now if you power off without switching — so end on a switch, or
CHECKPOINT, before you pull the plug.

### CHECKPOINT and REVERT — SELECT + BOOKMARK

One key does both, by how long you hold it:

* **`SELECT` + `BOOKMARK`, quick tap → CHECKPOINT.** Blesses all four groups
  (and their generators) as your safe point. The LCD confirms:

```
                CHECKPOINT
                organism blessed
```

* **`SELECT` + `BOOKMARK`, hold ~1 second → REVERT.** Restores the whole rig
  to the last blessed point — every group, every track, generators alive and
  resuming. The LCD confirms:

```
                REVERT
                organism restored
```

REVERT is the destructive one (it throws away whatever you've jammed since the
checkpoint), so it deliberately takes the *hold* — a quick fumble can only ever
CHECKPOINT, never wipe your jam. If you REVERT before ever blessing a point,
nothing happens and the LCD says `no checkpoint yet`.

One checkpoint is kept at a time (per session): a new CHECKPOINT overwrites the
last one, REVERT always returns to the most recent. After a REVERT the rig is
"dirty" against its slots, so the next switch writes the reverted state into
them — exactly as if you'd jammed it there yourself.

### Tips & Tricks

* Bless a checkpoint the moment a jam feels good, then explore without fear —
  mangle it, pile on generators, switch all over. One held `SELECT+BOOKMARK`
  and you're home. The safe point is the deliberate gesture; everything between
  is free.
* CHECKPOINT captures the *whole organism* (all 16 tracks across the 4 groups)
  in one press — it's a snapshot of the moment, not of one track.
* It pairs with the Pull and Capture: pull history in and jam it, and if the
  experiment goes nowhere, REVERT wipes the whole detour at once (vs.
  SELECT+CLEAR, which undoes the last single pull).
* A freeze you Capture into a group's *own* working slot gets overwritten by
  the ongoing jam at the next switch (the jam is the slot's memory now). Park
  freezes in *other* slots — that's what the variation library is for.

---

## FREEZE — Stop the Generators Mutating

The generators don't sit still: an engaged generator loop re-mutates and rewrites
its track at every measure wrap, so the rig keeps wandering on its own. **FREEZE**
is the global master switch that holds that wandering. Engage it and every engaged
generator loop locks to its current shape — the per-measure auto-mutate pauses
across all tracks at once. It's fully reversible: unfreeze and the loops resume
mutating from where they were.

FREEZE lives on the **METRONOME button** (repurposed — this fork doesn't click to
a live metronome). Tap it to toggle FROZEN ↔ live; the LCD flashes `FREEZE /
FROZEN` or `FREEZE / live` to confirm, and the METRONOME LED stays lit while
FROZEN. (If you've configured the button for hold behaviour rather than toggle, it
freezes only while held.) FREEZE gates the automatic generator-loop mutation only —
a deliberate ROLL or force-rewrite still fires while frozen, and a RESET clears
FREEZE back to live.

FREEZE is the other half of **Phrases**. Recall a phrase while frozen and the
organism lands as static *tape* — exactly the snapshot, no drift. Recall while live
and it lands *alive* — the generators pick the loop back up and carry it forward.
Frozen = static tape, unfrozen = alive: the same waypoint, two ways to play it.

---

## Phrases — A Set Is a Path

A **phrase** is a snapshot of the *whole living rig* (all 16 tracks, all four
groups, every generator) committed to a named slot. CHECKPOINT/REVERT, but with
16 waypoints instead of one — capture the moments that matter, then navigate
them live: the rig (and its generators) snaps to each one, on the bar.

Open the **PHRASE select-view** (the 16 select-row buttons are the 16 waypoints):

* **Tap a waypoint → RECALL.** The whole organism comes back to that snapshot,
  bar-aligned, generators resuming alive. Phrases are **immutable** — a recall is
  always the pristine snapshot; if you'd nudged the previous one, the nudge is
  written back to its working slot (never lost), not onto the phrase.
* **Hold a waypoint ≥1 s → CAPTURE** the live rig into it, then drop into the
  **name editor**. Type a name (multi-tap on the top/step row, like the SAVE
  page), `GP16` or `EXIT` to save — or just `EXIT` to leave it numbered.

**Reading the row.** Occupied waypoints light green; the current one (where you
are) is amber. When you've **deliberately edited** the rig since landing on the
current waypoint, it **winks** — "you've drifted off; recall to snap back, or
hold to capture where you've gotten to." The living generators wandering on
their own does *not* count as drift (that's what they do) — only your own
edits, pulls, ROLLs. Pair it with **FREEZE**: frozen, the waypoint holds and only
your hands move it; unfrozen, it's alive and the wink tracks your edits.

Names and occupancy survive a session reload. Recalling shows `PHn <name>` (or
`Phrase N` if unnamed). Track-grain recombination still happens *live* (the Pull)
— then capture the result as a phrase.

**Morphing between phrases (posture).** A recall is an instant jump; a **morph** is
a continuous glide. **Hold SELECT + tap a waypoint** to arm a morph toward it (for
the focused group). The **datawheel** rides the morph from 0 to full; the **GP row**
is a coarse 16-segment bar (the LEDs show how far you've gone). At 0 you're on your
own live posture — nothing changes. Sweeping up glides the *feel* toward the target
phrase — robotize density, chord-mask snap, tension GRIP — while the notes keep
playing underneath. (It's the **posture** that morphs, not the notes or the groove;
transpose/groove/generators are a later add.) Sweep back to 0 to land exactly where
you started. Then **tap that waypoint (a normal recall) to arrive** — the structure
lands on the bar and the morph lets go. While playing it steps on each bar; stopped,
it moves immediately. Re-arm the same slot (SELECT+tap) to cancel; the morph also
releases on its own if you recall, switch, pull, or UNDO that group. *Set up the two
phrases to differ in robotize / chord-mask / GRIP — that's what you'll hear move.*

**Recall feel — Quantize vs Seamless.** `OPT` → *Phrase Recall lands Seamless*.
**Off (Quantize):** a recall restarts on the next bar — a clean downbeat. **On
(Seamless):** the groove flows straight through, no restart. Either way the switch
no longer clicks or stutters. (REVERT always snaps back immediately — it's an undo.)

> Gestures here are provisional and will get refined as the wider instrument
> comes together; capture-then-name in particular may decouple.

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
| 0x9a | `tension_grip` (GRAVITY field, per track) |

The robotize five live in the ext block (above 0x7f) and follow the upstream V4.088
robotize CC range (0x80..0x90). The **v3 ext block** (2026-06-10) widened the persisted
range to 0x80..0x9f, so chord-mask (0x96–0x99) and GRIP (0x9a) now save/recall; old v2
patterns still load (the v2 byte-count is frozen, read path dispatches on tag).

---

*Last update: 2026-06-14*
