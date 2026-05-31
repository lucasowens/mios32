# MBSEQ V4 Fork — Pages Manual (Spine Era)

*Last code-checked against: 8e216fcf (phase H) + 499ba7d7 (step 7 mask polish).*

This is the **working UI surface** for the spine-era features (steps 1, 4, 5, 6, 7
of the §8 build order). It's a living document — every page mockup below is taken
from the LCD format string in the C source, not from memory. When the UI changes,
this doc changes alongside the commit.

Scope vs. the shipping manual:

| File | Audience | What's in it |
| --- | --- | --- |
| `apps/sequencers/midibox_seq_v4/doc/MBSEQV4_MANUAL_FORK.md` | end-user reference | shipped fork features (Robotize Loop, GENERATE page, classic Bounce-in-Place) — settled and ucapps-style |
| **this doc** | UI design collaboration | the new generative platform spine — PITCHGEN page, ChordMask, cursor-aware BOUNCE, mask polish. **Promote sections from here into MANUAL_FORK.md once they're settled enough not to need iteration.** |

Reading order: Section 0 (mockup conventions — read first if you've never
seen this format) → Section 1 (track setup) → Section 2 (TRKMODE / ChordMask)
→ Section 3 (PITCHGEN page) → Section 4 (BOUNCE dispatch) → Section 5
(step-7 polish, CC-only). Sections 6–7 are reference.

---

## 0. Mockup conventions

Every LCD mockup below is taken directly from the C source format strings.
The renderings are character-cell accurate against an 80-column canvas — two
physical 2×40 char LCDs side-by-side. The aim is for any field width or
position to be **countable**, so a UI iteration can be checked against fit
before any code changes.

Symbol legend (in the mockups themselves):

```
   ·       a space character — rendered visible so padding/widths are countable
   │       physical LCD-device seam at col 40 (between LCD 1 and LCD 2) —
           NOT a character position; a visual marker of the bezel
   └─Z─┘   zone Z spans this column range; cells align 1:1 with content above
   ✱       field flashes at cursor-flash cadence (~250 ms) when it is the
           active edit item (ui_selected_item == this field)
   ~       dynamic-width glyphs (e.g. SEQ_LCD_PrintNote prints exactly 3 chars
           but "C 2" and "F#3" pack differently — character count is fixed,
           glyph density is not)
```

Symbol prefix on each zone in the legend table:

```
   ○       static text — never changes
   △       state-dependent — value/label changes with engagement / cursor /
           track state (variants listed in the per-page variant tables)
   ◆       encoder-bound — rotate to adjust value
   ●       button-bound — press to act / cycle / toggle
   ◆●      both — encoder rotates value, button press is an independent action
```

LED-row mockup (when a page drives custom step-LEDs):

```
   LEDs:  [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           ·   █   ▒   ░   ·   ·   ·   ~    ·   ·   ·   ·   ·   ·   ·   ·

   ·       off
   ░       1/4 brightness
   ▒       1/2 brightness
   █       full brightness
   ~       blink (cursor / flash at ~250 ms)
```

When a page does **not** drive step-LEDs, the LED row is shown with all dots
and a note — this flags that the LED surface is an open UI design slot (§7).

GP-controls overlay (drawn ABOVE the LCD rows in the page mockup):

```
   GP#:     1    2    3    4    5    6    7    8 │  9   10   11   12   13   14   15   16
   encoder:          ◆    ◆    ◆    ◆    ◆       │
   button:  ●    ●    ●    ●         ●    ●    ● │
```

The hardware has 16 GP encoders+buttons evenly spaced across the 80-char LCD
canvas — each GP's center column is `5*i + 2` for i=0..15 (so GP1 at col 2,
GP2 at col 7, ..., GP8 at col 37, GP9 at col 42, ..., GP16 at col 77).

  - `GP#` row labels position the GP number at that center column. Two-digit
     labels (10..16) land at center-1 and center, so the **rightmost** digit
     sits over the center col.
  - `encoder` row marks ◆ at a GP center col when that GP's encoder is bound
     to a meaningful value on this page. If the encoder is NOT bound, a `·`
     marks the GP center (signaling "this GP exists, but its encoder doesn't
     do anything here"). Whether the *button* is bound is shown on the next
     row, independently.
  - `button` row uses the same convention with ● — bound buttons get ●, GP
     centers without distinct button gestures get `·`.

  So at every GP center column (col `5*i + 2` for i=0..15) you read **both
  rows together**:
    `◆ + ●` = this GP is fully engaged (rotate + press both meaningful).
    `◆ + ·` = encoder-only (button press is just "select for editing").
    `· + ●` = button-only (no encoder value bound).
    `· + ·` = GP exists but isn't used on this page at all.

The point of drawing the overlay above the LCD content is that **the GP
center column tells you which LCD column ought to carry that GP's label or
value**. When the label drifts away from the GP that controls it, the page
becomes harder to read by muscle memory — exactly the kind of misalignment
this doc exists to make visible.

---

## 1. Prerequisites — drum-pitch unlock + PAR-ASG setup

The generator page targets the **active drum** on the **visible track**. The
mental model: one slot = one (track, drum) pair, holding a 64-step pitch loop.
A track can host up to 16 simultaneous gens (one per drum); the global pool
holds 64 slots total.

For the generator to engage, the target track must satisfy two conditions:

```
   ┌──────────────────────────────────────────────────────────────────────┐
   │  ENGAGE prerequisites                                                │
   ├──────────────────────────────────────────────────────────────────────┤
   │                                                                      │
   │  1. tcc->event_mode == DRUM    (set on TRKEVENT page)                │
   │                                                                      │
   │  2. tcc->link_par_layer_note >= 0    (set on PAR-ASG page —          │
   │       assign one parameter layer to type "Note". Value 0 in the      │
   │       Note layer = "use preset pitch from lay_const", *not silence*. │
   │       Per-step silence remains the trigger layer's job.)             │
   │                                                                      │
   └──────────────────────────────────────────────────────────────────────┘
```

If either condition fails, the PITCHGEN page row 1 RHS shows guidance instead
of dials, and GP1 ENGAGE refuses with one of:

| ENGAGE return | LCD message (line 2) | Cause |
| --- | --- | --- |
| `-1` | `pool full (64/64)` | global generator pool exhausted |
| `-2` | `needs drum-mode track` | track not in DRUM event mode |
| `-3` | `assign Note par-layer` | no `Note` parameter layer assigned |

Why this matters for UI iteration: the prerequisite check sits before any
gesture — adding a generator type that doesn't need drum-mode (e.g., a normal-
track variant) means changing both the prerequisite gate and the row-1 hint.
The §A3 "Templates over layer type" note in the design doc is the planned
shape.

---

## 2. TRKMODE page — ChordMask playmode + strength dial

ChordMask is the §8 step 4 playmode that snaps the track's emitted notes to a
**pitch-class set** read from the bus transposer notestack. With strength 0
it's pass-through; at 127 it hard-locks every emitted note onto the nearest
in-set semitone. As of step 5 phase C, ChordMask is **a processor in slot 0**
under the hood — but the TRKMODE page keeps the legacy picker UI as a
shortcut. CC writes to `MODE` / `CHORDMASK_STRENGTH` mirror into the
processor stack via `SEQ_CORE_ChordMaskSlotSync`.

### Layout (code-truth from `seq_ui_trkmode.c:240–323`)

**State A — default (Normal mode selected, no editing).**

```
         0         1         2         3         │4         5         6         7
         0123456789012345678901234567890123456789│0123456789012345678901234567890123456789
GP#:       1    2    3    4    5    6    7    8  │  9   10   11   12   13   14   15   16
encoder:   ◆    ◆    ◆    ◆    ◆    ◆    ◆    ◆  │  ◆    ◆    ◆    ◆    ◆    ◆    ◆    ◆
button:    ·    ·    ·    ·    ·    ·    ·    ·  │  ·    ·    ·    ·    ·    ·    ·    ·
         ────────────────────────────────────────┼────────────────────────────────────────
row 0    Trk.>off<·····>Transpose>ChordMask<·Bus·│Note··Hold·Sort·ReSt.·STrg··FTS··Sustain
zones    └A─┘└─B─┘     └───C────┘└────D────┘ └E┘ │└F─┘  └G─┘ └H─┘ └─I─┘ └J─┘  └K┘  └──L──┘
row 1    G1T1·····>Normal<··>Arpeggiator<·····1··│First··on···on···on···on····on···on·····
zones    └M─┘     └──N───┘  └─────O─────┘     P  │└─Q─┘  └R┘  └S┘  └T┘  └U┘   └V┘  └W┘
         ────────────────────────────────────────┴────────────────────────────────────────
                      LCD 1 (chars 0..39)                  LCD 2 (chars 40..79)
                                                 ▲
                                                 physical bezel

  GP role on this page (every GP is encoder-bound; no distinct button gestures):
    GP1=GxTy   GP2..6=MODE picker (5 GPs cycle the same field, one per mode)
    GP7=ChordMask strength   GP8=BUS
    GP9=FirstNote  GP10=Hold  GP11=Sort  GP12=Restart  GP13=STrg
    GP14=FTS  GP15..16=Sustain
```

**Alignment observations from this overlay:**

* GP1 ◆ at col 2 sits over `Trk.` (cols 0..3) — track-select label is right
  under the encoder that drives it ✓
* GP8 ◆ at col 37 sits over `Bus` (cols 36..38) — same, well aligned ✓
* GP10..GP13 ◆ over `Hold`/`Sort`/`ReSt.`/`STrg` — all 1-col-or-so misaligned
  but visually under their labels ✓
* GP14 ◆ at col 67 sits between `STrg` (col 65) and `FTS` (col 68..70) —
  ambiguous which one it controls without the legend
* GP15+GP16 ◆ at cols 72 and 77 — `Sustain` (cols 73..79) spans both, which
  matches the source: `case ITEM_SUSTAIN: *gp_leds = 0xc000;` (bits 14,15 →
  GP15 AND GP16 light up when Sustain is selected). Two GPs, one field —
  visually correct.
* **Mode picker is the rough spot:** GP2..GP6 all cycle the SAME field
  (`ui_selected_item = ITEM_MODE`), but each is positioned at a different
  tile's center. So pressing GP4 selects mode 2 (Transpose), GP6 selects
  mode 4 (ChordMask), etc. — the GP **position** encodes which mode you
  jump to. The 5-tile layout matches GP2..GP6 reasonably well in row 0
  (off at col 4 ≈ GP2 col 7, Transpose at col 14 ≈ GP4 col 17, ChordMask
  at col 24 ≈ GP6 col 27) but the row 1 alternation (Normal/Arpeggiator)
  pulls those tiles to GP3 and GP5 columns instead. This is a known
  cross-row pattern from upstream — not strictly broken, but unusual.

Zone legend (state A — note tile-overlap is real: tiles at x=4, 14, 24 (row 0)
and x=9, 19 (row 1) overwrite each other's trailing padding):

```
   A   ○ cols  0..3    "Trk."                       static label
   B   ✱ cols  4..8    ">off<"                      mode-0 tile, flashes if selected
   C   ✱ cols 14..23   ">Transpose"                 mode-2 tile (< at col 24 overwritten by D)
   D   ✱ cols 24..34   ">ChordMask<"                mode-4 tile — overlaid in state B
   E   ○ cols 36..38   "Bus"                        label (LCD 1 boundary at col 39)
   F   ○ cols 40..43   "Note"                       label
   G   ○ cols 46..49   "Hold"                       label
   H   ○ cols 51..54   "Sort"                       label
   I   ○ cols 56..60   "ReSt."                      label
   J   ○ cols 62..65   "STrg"                       label
   K   ○ cols 68..70   "FTS"                        label
   L   ○ cols 73..79   "Sustain"                    label
   M   △ cols  0..3    "G1T1"                       GxTy (track group + selection)
   N   ✱ cols  9..16   ">Normal<"                   mode-1 tile, flashes if selected
   O   ✱ cols 19..31   ">Arpeggiator<"              mode-3 tile (full 13-char, no overwrite)
   P   ◆ col  37       "1"                          BUS value (right-aligned single digit)
   Q   ◆ cols 40..44   "First" / "Last·"            FIRST_NOTE flag value
   R   ◆ cols 47..48   "on" / "of"                  HOLD (3-char field; last col is trailing ·)
   S   ◆ cols 52..53   "on" / "of"                  SORT
   T   ◆ cols 57..58   "on" / "of"                  RESTART
   U   ◆ cols 62..63   "on" / "of"                  STrg
   V   ◆ cols 68..69   "on" / "of"                  FTS
   W   ◆ cols 73..74   "on" / "of"                  Sustain (cols 75..79 = trailing pad)
```

**State B — ChordMask selected (or `ui_selected_item == ITEM_CHORDMASK_STR`).**

The mode-tile at cols 24..34 gets **overlaid**: `>ChordMask<` (11 chars) →
`>ChMsk<····` (7 chars + 4 trailing spaces). The row-1 cell at 24..34 gets
the live strength readout `·Msk:NNN···` (1 + 7 + 3 = 11 chars).

```
         0         1         2         3         │4         5         6         7
         0123456789012345678901234567890123456789│0123456789012345678901234567890123456789
GP#:       1    2    3    4    5    6    7    8  │  9   10   11   12   13   14   15   16
encoder:   ◆    ◆    ◆    ◆    ◆    ◆    ◆    ◆  │  ◆    ◆    ◆    ◆    ◆    ◆    ◆    ◆
button:    ·    ·    ·    ·    ·    ·    ·    ·  │  ·    ·    ·    ·    ·    ·    ·    ·
         ────────────────────────────────────────┼────────────────────────────────────────
row 0    Trk.>off<·····>Transpose>ChMsk<·····Bus·│Note··Hold·Sort·ReSt.·STrg··FTS··Sustain
zones    └A─┘└─B─┘     └───C────┘└────D2───┘ └E┘ │(LCD 2 unchanged from state A)
row 1    G1T1·····>Normal<··>Arpe·Msk:064·····1··│First··on···on···on···on····on···on·····
zones    └M─┘     └──N───┘  └N2─┘└────D3───┘  P  │(LCD 2 unchanged from state A)
         ────────────────────────────────────────┴────────────────────────────────────────
                                ▲          ▲ ▲
                                │          │ └─ ChordMask strength readout `Msk:064` at
                                │          │    cols 25..31 — GP7 ◆ at col 32 sits at the
                                │          │    very right edge of the readout (good!)
                                │          │
                                │          └─ overlay ends at col 34 → cleanly hands off
                                │             to BUS field at col 35 (under GP8 ◆)
                                │
                                └─ residue: ">Arpe" (cols 19..23) — the FIRST 5 chars of
                                   the Arpeggiator tile that the overlay didn't paint over.
                                   Real hardware behavior — UI iteration target (§7).
```

Zone legend (state B — only the overlaid zones differ from state A):

```
   D2  ✱  cols 24..34   ">ChMsk<····"     overlay (col 35 hand-off back to default labels)
   N2  ✱  cols 19..23   ">Arpe"           the residue — first 5 chars of Arpeggiator tile
   D3  ◆✱ cols 24..34   "·Msk:NNN···"     live strength readout, flashes when GP7 is edit item
                        ▲   ▲▲▲▲
                        │   └──── 3-digit value (000..127), no glyph variation
                        └──── leading space (col 24), trailing 3 spaces (cols 32..34)
```

### Variants (state B row 1 cols 24..34)

| `ui_selected_item == ITEM_CHORDMASK_STR` | `ui_cursor_flash` | Cols 24..34 | Notes |
| --- | --- | --- | --- |
| no  | (irrelevant) | `·Msk:NNN···` | value visible |
| yes | off          | `·Msk:NNN···` | value visible |
| yes | on           | `···········` | 11 spaces (PrintSpaces(11)) — flash gap |

### Step-LED row

```
   LEDs:  [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           ·   ·   ·   ·   ·   ·   ·   ·    ·   ·   ·   ·   ·   ·   ·   ·

   TRKMODE does not drive custom step-LED state — only the default page
   behavior (trigger-layer visualization for the visible track) is present.
   No ChordMask-specific surface on the LED row yet. (§7 iteration target.)
```

### Gestures

```
   ◆ GP7 encoder       adjust ChordMask strength (0..127)
   ● Mode select       (existing flow — pick mode 4 = ChordMask)
   ◆ Bus assign        track's MIDI-out bus (independent of chord_mask read bus since step 7)
```

### Why two buses are now separate (step 7)

Before step 7, `SEQ_CC_BUSASG` did double duty: it set both the track's MIDI
output bus *and* the bus the chord_mask read its PC-set from. Step 7
untangled them:

```
 ┌─────────────────────────┐         ┌─────────────────────────┐
 │ Track plays on bus N    │   ───►  │ TRKMODE: BUSASG = N     │
 └─────────────────────────┘         └─────────────────────────┘

 ┌─────────────────────────┐         ┌─────────────────────────┐
 │ chord_mask listens to   │   ───►  │ CC 0x97 CHORDMASK_BUS=M │
 │ chord context on bus M  │         │ (no UI yet — CC/SysEx)  │
 └─────────────────────────┘         └─────────────────────────┘
```

So a track can play on bus 0 while its ChordMask snaps to chord context being
pumped from bus 1. See §5 for the CC-only access path.

---

## 3. PITCHGEN page — the spine-era Turing generator

This is the page where the §3 spine becomes most tangible. One generator
slot, eight dials, three gestures. Everything else is the destination
control (the cursor) and the live state (engaged/disengaged).

### 3.1 Layout (code-truth from `seq_ui_trkpitchgen.c:415–459`)

The mockup below shows the **engaged** state — gen running on (Trk 1, D 3),
cursor parked on step 3 (datawheel), step 3 not locked, MULT 1×, rate 8,
depth 127, contour Uni, range C2..C6:

```
         0         1         2         3         │4         5         6         7
         0123456789012345678901234567890123456789│0123456789012345678901234567890123456789
GP#:       1    2    3    4    5    6    7    8  │  9   10   11   12   13   14   15   16
encoder:   ·    ·    ◆    ◆    ◆    ◆    ◆    ·  │  ·    ·    ·    ·    ·    ·    ·    ·
button:    ●    ●    ●    ●    ·    ●    ●    ●  │  ·    ·    ·    ·    ·    ·    ·    ·
         ────────────────────────────────────────┼────────────────────────────────────────
row 0    PITCH·GEN··Trk·1.D·3···state:ENGAGED····│G1=EN/RL·G2=UN·G3=ANC·G4=SNP·G8=BNC·····
zones    └───A───┘  └───B───┘   └─────C─────┘····│└──D1──┘ └D2─┘ └─D3─┘ └─D4─┘ └─D5─┘·····
row 1    Lo:C·2·Hi:C·6·R:008·D:127·Ct:Uni········│Stp:03·[·]·M:1x···G6=LCK·G7=MULT········
zones    └─E──┘└──F──┘└─G──┘└─H──┘└──I──┘········│└─M──┘ └N┘ └─O──┘ └─P1─┘ └─P2──┘········
         ────────────────────────────────────────┴────────────────────────────────────────
                      LCD 1 (chars 0..39)                  LCD 2 (chars 40..79)
                                                 ▲
                                                 physical bezel
```

**Misalignment visible from this overlay:** GP3 controls `range_min` and its
button is ANCHOR, but the `Lo:C·2` field sits at cols 0..5 — under GP1/GP2,
**not** under GP3 (col 12). Same for the whole row-1 LHS dial strip: the
labels are 10 cols to the left of their actual encoders. The row-0-RHS hint
band aligns better (`G1=EN/RL` under GP9-ish region, but that's only because
the hint text *spells out the GP number*, not because it lives under the
right physical control).

This is the §7 iteration target — see §7.6.

Zone legend:

```
   A   ○  cols  0..8    "PITCH·GEN"         static title
   B   △  cols 11..19   "Trk·NN.D·NN"       visible track + cursor drum (NN = %2d → " 1", "10")
   C   △  cols 23..35   "state:???????"     3 variants — see table
   D1  ○  cols 40..47   "G1=EN/RL"          static gesture band, always lit
   D2  ○  cols 49..53   "G2=UN"
   D3  ○  cols 55..60   "G3=ANC"
   D4  ○  cols 62..67   "G4=SNP"
   D5  ○  cols 69..74   "G8=BNC"
   E   ◆● cols  0..5    "Lo:C·2"            ◆ range_min  /  ● ANCHOR (note glyph ~ varies)
   F   ◆● cols  6..12   "·Hi:C·6"           ◆ range_max  /  ● SNAP
   G   ◆  cols 13..18   "·R:NNN"            mutation rate 0..127
   H   ◆● cols 19..24   "·D:NNN"            ◆ mutation depth  /  ● LOCK toggle at cursor
   I   ◆● cols 25..31   "·Ct:NNN"           ◆ contour cycle  /  ● MULT cycle at cursor
   M   △  cols 40..45   "Stp:NN"            cursor step %02d (00..63)
   N   △  cols 47..49   "[L]" or "[·]"      per-step lock indicator at cursor
   O   △  cols 51..56   "M:XXXX"            MULT label (4-char padded — see table)
   P1  △  cols 58..63   "G6=LCK"            contextual gestures (engaged state)
   P2  △  cols 65..71   "G7=MULT"
```

### 3.1.1 Variants

**Zone C (state @ cols 23..35):**

| State | Cols 23..35 | Width | Padding |
| --- | --- | --- | --- |
| engaged | `state:ENGAGED` | 13 | (fills C exactly; cols 36..39 are pad) |
| disengaged | `state:disengaged·` | 14 → spills to col 36 | OK — LCD 1 still fits |
| no slot | `state:--··········` | 16 → spills to col 38 | OK — col 40 spill gets overwritten by D1 |

(The source writes `state:disengaged··` and `state:--··········` as fixed
18-char strings — they always reach col 40. Col 40 is the first cell of
LCD 2, which then gets overwritten by the D1..D5 band. Benign.)

**Zone O (MULT label @ cols 51..56) — 4-char padded value after `M:`:**

| MULT code | Label | Cols 53..56 |
| --- | --- | --- |
| 0 (MUTE) | `0x··` | `0x··` |
| 1 (HALF) | `0.5x` | `0.5x` |
| 2 (DEFAULT) | `1x··` | `1x··` |
| 3 (DOUBLE) | `2x··` | `2x··` |

**Row 1 RHS (cols 40..79) — six variants (one of P1+P2+pad block):**

| Variant | Trigger | Cols 40..79 |
| --- | --- | --- |
| 1. not drum-mode | `tcc->event_mode != DRUM` | `(needs·drum-mode·track)·················` |
| 2. no Note layer | `tcc->link_par_layer_note < 0` | `(assign·Note·in·PAR-ASG·first)··········` |
| 3. **engaged** ★ | gen engaged on (track, instr) | `Stp:NN·[L]·M:XXXX·G6=LCK·G7=MULT········` |
| 4. relocate hint | gen engaged elsewhere on this track | `GEN·on·DNN··GP8·relocates·here·········` |
| 5. capture hint | exactly 1 other track has a processor | `Proc·on·TNN··GP8·captures·here··········` |
| 6. default | none of the above | `press·GP1·to·ENGAGE···GP8=bounce·proc···` |

★ = the variant shown in the main mockup above.

The row-1 RHS is the page's "what does GP8 do right now" affordance — it's
how the cursor-aware BOUNCE (§4) is surfaced. If the hint doesn't say
something specific about GP8, BOUNCE will print "nothing to bounce".

### 3.2 GP map

| GP | Encoder (rotate) | Button (press) |
| --- | --- | --- |
| **GP1** | — | **ENGAGE** (when disengaged) / **ROLL** (when engaged) / **DISENGAGE** (SEL+GP1) |
| **GP2** | — | **UNDO** (restore pre-engage source par; disengages every gen on that track) |
| **GP3** | range_min (1..127, clamped ≤ range_max) | **ANCHOR** (re-snapshot loop as new identity) |
| **GP4** | range_max (1..127, clamped ≥ range_min) | **SNAP** (restore loop from anchor) |
| **GP5** | mutation_rate 0..127 | (no button action; press = no-op) |
| **GP6** | mutation_depth 0..127 | **LOCK toggle** at cursor step |
| **GP7** | contour cycle (UNIFORM / LOW_BIAS / HIGH_BIAS / TRIANGLE) | **MULT cycle** at cursor (0× / 0.5× / 1× / 2×) |
| **GP8** | — | **BOUNCE** (see §4) |
| **Datawheel** | scroll step cursor 0..63 (always, even when disengaged) | — |

### 3.3 LED rows

**GP-row LEDs (16 buttons across the top of the LCD):**

```
   GP:    [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           █   ·   ·   ·   ·   ·   ·   ·    ·   ·   ·   ·   ·   ·   ·   ·

   Only GP1 is driven — █ when a gen is engaged on (visible_track,
   ui_selected_instrument). GP2..GP16 use default page handling (no
   PITCHGEN-specific state).
```

**Step-row LEDs:**

```
   LEDs:  [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           ·   ·   ·   ·   ·   ·   ·   ·    ·   ·   ·   ·   ·   ·   ·   ·

   PITCHGEN does not drive custom step-LED state today — only the default
   page behavior (trigger-layer visualization for the visible track). The
   per-step LOCK / MULT / cursor surface is unwired hardware feedback.

   ▲ §7.1 sketches the design-ahead version (brightness per LOCK + MULT,
     cursor as fast blink, 4 LCD-pages of 16 LEDs = 64 loop steps).
```

The cursor (`ui_selected_step`, 0..63) is currently visible only on LCD row
1 RHS as `Stp:NN`. With a 16×4 step matrix the LOOP_LEN=64 loop maps to
4 pages of 16 LEDs — the existing view-switch plumbing could navigate
between them. Design-ahead until a listen-test surfaces the need.

### 3.4 ENGAGE / ROLL / DISENGAGE finite-state model

```
                       press GP1
              ┌───────────────────────────┐
              │                           │
              ▼                           │
   ┌───────────────────┐                  │
   │   DISENGAGED      │ ────────────────►│   ENGAGE
   │  (no slot, or     │  press GP1       │   - alloc pool slot
   │   slot exists but │                  │   - snapshot src par
   │   not mutating)   │                  │     → global undo
   └───────┬───────────┘                  │   - seed loop[] with
           │                              │     random pitches in
           │ press GP1 again              │     [range_min, range_max]
           │ on a fresh ENGAGE            │   - auto-anchor (H):
           │ (re-uses existing slot       │     anchor[] := loop[]
           │  if still allocated)         │   - mutate-rewrite source
           │                              │   - LED GP1 lit
           ▼                              ▼
   ┌───────────────────┐  press GP1   ┌────────────────────┐
   │   ENGAGED         │ ───────────► │  ENGAGED           │
   │                   │   = ROLL     │                    │
   │                   │  (one-shot   │                    │
   │                   │   reroll of  │                    │
   │                   │   unlocked   │                    │
   │                   │   steps;     │                    │
   │                   │   ignores    │                    │
   │                   │   rate/depth │                    │
   │                   │   /MULT)     │                    │
   └───────┬───────────┘              └────────────────────┘
           │
           │ SEL + GP1
           ▼
   ┌───────────────────┐
   │   DISENGAGED      │   slot stays allocated;
   │   (slot kept)     │   anchor / locks / MULT preserved
   └───────────────────┘
           │
           │ GP2 UNDO    -or-    GP8 BOUNCE-in-place
           ▼
   ┌───────────────────┐
   │ SLOT RECYCLED     │   anchor/locks/MULT cleared on the next ENGAGE
   │  (memset to 0)    │   (BOUNCE source stays as last-written;
   │                   │    UNDO restores pre-engage source)
   └───────────────────┘
```

**Key invariant:** the auto-anchor at ENGAGE means SNAP always has a target.
The only path to a "no anchor yet" SNAP refuse (-2) is a code path that
allocates without seeding — no UI gesture currently exposes this.

### 3.5 Mutation math — rate × depth × MULT × contour

Every measure boundary, `SEQ_GENERATOR_Tick` walks every engaged slot and
runs `mutate_loop`. For each step in `loop[0..63]`:

```
  if locks[step]:                          # G.4 per-step lock
      skip
  threshold = mult_threshold(rate, mult[step])
                                           # MULT 0× → 0 (mutation muted)
                                           # MULT 0.5× → rate / 2
                                           # MULT 1× → rate
                                           # MULT 2× → min(rate * 2, 255)
  if PRNG_8bit < threshold:
      if depth == 0:                       # G.2 depth=0 freeze
          do nothing
      elif depth == 127:                   # full-reroll path
          loop[step] = pick_in_range(range_min, range_max, contour)
      else:                                # perturb path
          loop[step] = clamp(loop[step] ± rand(0..depth),
                             range_min, range_max)
                                           # contour ignored on perturb
```

After the per-step walk, `loop[]` is transcribed into the source par-buffer
(`write_loop_to_source`), and `seq_render_dirty[track]` is set so phase-D's
renderer picks it up the same tick.

**ROLL** bypasses `rate`, `depth`, and `mult[]` entirely — it walks all
unlocked steps and rerolls them through `contour`. ROLL is the on-demand
override; rate/depth/MULT are the gating that shapes the *passive* drift.

**Two-dimensional rate/depth space:**

```
                 depth →
              0 ──────────────────── 127
        rate ┌─────────────────────────┐
         ↓ 0 │ frozen     frozen        │  rate=0 ⇒ §2.3 pass-through
             │                          │   no mutation ever (sweep contract)
             │                          │
       ~32   │ frozen     drift         │  occasional perturb at random steps
             │                          │
             │                          │
       ~64   │ frozen     shimmer       │  many small moves per measure
             │                          │
             │                          │
       127   │ frozen     CHAOS         │  full-reroll most steps per measure
             │            (range-wide)  │
             └─────────────────────────┘
                  depth ↑                  contour shapes the
                                           reroll distribution
```

### 3.6 Contour shapes (full-reroll path only)

Cheap distribution shaping via min/max/sum of two uniforms — no trig, no
tables. From `seq_generator.c`:

```
  UNIFORM    → uniform PRNG across [range_min, range_max]
  LOW_BIAS   → min(u1, u2)   — pulls toward range_min
  HIGH_BIAS  → max(u1, u2)   — pulls toward range_max
  TRIANGLE   → (u1 + u2) / 2 — pulls toward midpoint
```

Audibly noticeable most when `range_min`/`range_max` are wide apart and
ROLL is hit repeatedly to redistribute.

### 3.7 Per-step LOCK + MULT — cursor-driven sculpting

The datawheel scrolls a single `ui_selected_step` cursor across 64 loop
steps. **LCD row-1 RHS** is the only visual surface today — three example
captures (cursor at different steps with different lock + MULT combinations):

```
        4         5         6         7
        0123456789012345678901234567890123456789
        ────────────────────────────────────────
   ex1  Stp:03·[·]·M:1x···G6=LCK·G7=MULT········    cursor on step 3, unlocked, MULT 1×
   ex2  Stp:07·[L]·M:2x···G6=LCK·G7=MULT········    cursor on step 7, LOCKED, MULT 2×
   ex3  Stp:12·[·]·M:0x···G6=LCK·G7=MULT········    cursor on step 12, unlocked, MULT 0× (mute)
        ────────────────────────────────────────
                            ▲    ▲    ▲    ▲
                            │    │    │    │
                            └ M-label flexes:
                                 0x··    MUTE
                                 0.5x    HALF
                                 1x··    DEFAULT
                                 2x··    DOUBLE
```

Step-row LEDs are not driven by this page today. The mockup below sketches
what they *would* show if the §A3 design-ahead were wired — kept here so the
UI iteration in §7.1 has a target to refine against, not a built feature:

```
   (design-ahead, NOT WIRED — see §7.1)

   step:  [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           ·   ·   ~   ░   ·   ·   ·   █    ░   ·   ·   ░   ·   ·   ·   ·
                    ▲    ▲              ▲                ▲
                    │    │              │                └─ unlocked, MULT 0.5×
                    │    │              └─ locked + MULT 1× (steady, full bright)
                    │    └─ unlocked, MULT 0.5× (dim)
                    └─ cursor (datawheel) — blinks at flash cadence

   ·   off (MULT 0× / off-page)        █   full (MULT 1× normal)
   ░   1/4 brightness (MULT 0.5×)      ▒   1/2 brightness (used as overlay?)
   ~   blink (cursor)                  L   locked (alternative encoding — undecided)

   ▲ Encoding is unsettled: brightness can mean "MULT level" OR "lock state"
     but not both with 4 brightness levels and 5 states. §7.1 is the place
     where the trade-off gets decided. Current candidates:
       (a) brightness = MULT,  lock = blink-on-cursor-pass
       (b) brightness = lock,  MULT shown only on LCD row 1
       (c) brightness = MULT,  lock = a separate row of LEDs (if available)
```

### 3.8 LOOP_LEN vs track step count

The generator always works in **LOOP_LEN = 64** units. If the track is
shorter (e.g., 32 steps), `write_loop_to_source` only writes the first
`num_steps` bytes of `loop[]`. If the track is longer (e.g., 128 steps), the
loop **tiles** — steps 0..63 of the source are the loop, steps 64..127 are
also the loop (re-transcribed), etc. The 64-step cursor extent therefore
covers the *generator's view*, not the *track's view*.

**Implication for UI iteration:** the cursor display `Stp:NN` is the
generator-loop step, not the track step. On a 16-step drum track only
the first 16 cursor positions actually drive notes you can hear — beyond
that you're editing the loop for a future track-length expansion. This
should probably be surfaced in the LCD (TODO §7).

---

## 4. BOUNCE — cursor-aware destination dispatch

GP8 BOUNCE is where the §3 destination model becomes a single button. The
button **always means the same thing in principle**: "freeze whatever the
cursor is pointing at into source par + clear the dynamic state." What
changes is which thing is "whatever the cursor is pointing at" — the
dispatch resolves it in priority order.

### 4.1 Resolution tree (code-truth from `seq_ui_trkpitchgen.c:208–286`)

```
                       GP8 BOUNCE
                            │
                            ▼
              ┌─────────────────────────┐
              │ generator engaged on    │
              │ (visible_track,         │ YES ──► IN-PLACE GEN BOUNCE
              │  ui_selected_instrument)│         (§3 occupied → replace)
              │ ?                       │         SEQ_GENERATOR_Bounce
              └────────────┬────────────┘         → free slot
                           │ NO                   → source stays as last-
                           ▼                        written
              ┌─────────────────────────┐
              │ generator engaged on    │
              │ some other drum on this │ YES ──► RELOCATE GEN BOUNCE
              │ visible track?          │         (§3 empty → additive)
              └────────────┬────────────┘         SEQ_GENERATOR_BounceRelocate
                           │ NO                   → restore src from undo
                           ▼                      → transcribe loop to dst
              ┌─────────────────────────┐         → free src slot, clear undo
              │ any enabled processor   │
              │ on visible track?       │ YES ──► IN-PLACE PROCESSOR BOUNCE
              └────────────┬────────────┘         SEQ_CORE_ProcessorBounce
                           │ NO                   → commit output → source
                           ▼                      → clear stack
              ┌─────────────────────────┐         → untangle tcc mirror
              │ exactly one OTHER track │
              │ has an enabled          │ YES ──► CROSS-TRACK PROCESSOR
              │ processor?              │         CAPTURE
              └────────────┬────────────┘         (§3 empty → additive)
                           │ NO                   SEQ_CORE_ProcessorBounceCapture
                           ▼                      → copy src output → dst source
              ┌─────────────────────────┐         → src stack untouched
              │ multiple OTHER tracks   │
              │ have enabled processors?│ YES ──► REFUSE: "multi proc"
              └────────────┬────────────┘
                           │ NO
                           ▼
                  REFUSE: "nothing to bounce"
```

### 4.2 The four success cases — visualized

```
 IN-PLACE GEN BOUNCE                       cursor IS the engaged gen
 ────────────────────                      ───────────────────────────

   Before                                  After
   ┌──────────────────┐                    ┌──────────────────┐
   │ Trk T, Drum D    │                    │ Trk T, Drum D    │
   │  pool slot ───►  │ ENGAGED            │  pool slot ───►  │ (freed)
   │  loop[0..63]     │                    │                  │
   │  source par[]    │ (last written)     │  source par[]    │ (frozen,
   └──────────────────┘                    └──────────────────┘  same bytes)


 RELOCATE GEN BOUNCE                       cursor on empty drum,
 ───────────────────                       gen engaged on a different drum
                                           ──────────────────────────────────

   Before                                   After
   ┌──────────────────┐                     ┌──────────────────┐
   │ Trk T, Drum 3    │ ENGAGED             │ Trk T, Drum 3    │ (restored
   │  pool slot       │                     │                  │  from undo)
   │  loop[..]        │                     │                  │
   │  source par[]    │ ← gen rewriting     │  source par[]    │ ← pre-engage
   └──────────────────┘                     └──────────────────┘   bytes
   ┌──────────────────┐                     ┌──────────────────┐
   │ Trk T, Drum 7    │ EMPTY (cursor)      │ Trk T, Drum 7    │ (final loop
   │                  │                     │  source par[]    │  transcribed
   │                  │                     │                  │  in)
   └──────────────────┘                     └──────────────────┘


 IN-PLACE PROCESSOR BOUNCE                 cursor on track with proc enabled,
 ────────────────────────                  no gen engaged anywhere on track
                                           ──────────────────────────────────

   Before                                   After
   ┌────────────────────┐                   ┌────────────────────┐
   │ Trk T              │                   │ Trk T              │
   │  output (live)     │ ChordMask snaps   │  output            │ = source
   │  ↑ chord_mask      │                   │  ↑ slots cleared   │
   │  ↑ output_mirror   │                   │                    │
   │  source par[]      │ (un-snapped)      │  source par[]      │ ← snapped
   └────────────────────┘                   └────────────────────┘   bytes
                                            tcc playmode → Normal
                                            tcc chordmask_strength → 0


 CROSS-TRACK PROCESSOR CAPTURE             visible track empty,
 ─────────────────────────────             exactly one other track has proc
                                           ─────────────────────────────────

   Before                                   After
   ┌────────────────────┐                   ┌────────────────────┐
   │ Src track (e.g. 3) │ ChordMask runs    │ Src track          │ untouched
   │  output            │ on bus chord      │  output            │  (still
   │  source par[]      │                   │  source par[]      │   running)
   └────────────────────┘                   └────────────────────┘
   ┌────────────────────┐                   ┌────────────────────┐
   │ Dst track (T, you) │ empty             │ Dst track          │
   │  source par[]      │ all-zero          │  source par[]      │ ← src's
   └────────────────────┘                   └────────────────────┘   output
                                                                    captured
```

### 4.3 Outcome matrix

| Cursor target | Other state | GP8 result | LCD message |
| --- | --- | --- | --- |
| Engaged gen | — | in-place gen bounce | `BOUNCED in place` |
| Empty drum, this track | Gen on another drum, same track | relocate | `BOUNCED -> drum slot` |
| Empty drum | Gen elsewhere, dst track not drum-mode | refuse | `dst not drum-mode` |
| Empty drum | Gen elsewhere, UNDO covers different track | refuse | `undo stale - reENGAGE` |
| Empty track, has own proc | — | in-place processor bounce | `BOUNCED (proc)` |
| Empty track, no proc | One other track has proc | cross-track capture | `CAPTURED from Tnn` |
| Empty track, no proc | ≥2 other tracks have proc | refuse | `multi proc - pick one` |
| Empty track, no proc | No proc anywhere | refuse | `nothing to bounce` |
| Capture | dst not empty | refuse | `dst not empty` |
| Capture | nothing to capture | refuse | `nothing to bounce` |

### 4.4 The §3 destination model in one rule

> **Cursor occupancy decides additive-vs-replace:**
> *Cursor on the thing → replace it (bounce in place).*
> *Cursor on empty → bring something here (relocate / capture).*

This is the one rule that drives the whole tree above. When you're iterating
on a new processor / generator type, the question to ask is "what does it
mean for the cursor to be 'on' this thing?" and "what does it mean for the
cursor to be 'empty'?". Once those are decided, the tree extends.

---

## 5. Step-7 mask polish — per-drum scope + per-processor bus (CC-only)

Step 7 split two concerns that had been incidentally coupled:

1. **chord_mask drum scope** — previously the processor snapped *every* drum
   on a drum-mode track. Now there's a 16-bit drum mask; drums whose bit is
   clear are skipped.
2. **chord_mask read bus** — previously the chord_mask read its PC-set from
   `tcc->busasg.bus` (which is also the track's playback bus). Now it reads
   from `tcc->chordmask_bus`, independent.

**No UI surface yet — deferred until a control-surface map crystallizes.**
For now, access is via three CCs / SysEx:

| CC | Symbol | Range | Default | Reset by |
| --- | --- | --- | --- | --- |
| `0x97` | `CHORDMASK_BUS` | 0..3 | 0 | `SEQ_CC_ResetGenerativeForBounce`, processor bounce |
| `0x98` | `CHORDMASK_DRUM_L` | 0..0xFF (drums 0..7 bitmask) | `0xFF` | same |
| `0x99` | `CHORDMASK_DRUM_H` | 0..0xFF (drums 8..15 bitmask) | `0xFF` | same |

Default `0xFFFF` mask = all drums included, matches the legacy whole-track
behavior so existing patterns play identically.

```
 drum-mask layout (mental model):
   bit 0  = drum 0 (D 1)
   bit 1  = drum 1 (D 2)
   ...
   bit 7  = drum 7 (D 8)    ← CHORDMASK_DRUM_L holds bits 0..7
   bit 8  = drum 8 (D 9)
   ...
   bit 15 = drum 15 (D16)   ← CHORDMASK_DRUM_H holds bits 8..15

 example — snap only D 1 + D 5 + D 9:
   CHORDMASK_DRUM_L = 0b00010001 = 0x11
   CHORDMASK_DRUM_H = 0b00000001 = 0x01
```

### Cross-bus chord workflow (no UI yet)

```
 ┌─────────────────────┐
 │ Chord source        │
 │   - chord-event-    │ ──► outputs to Bus 1
 │     mode track on   │
 │     T n             │
 └─────────────────────┘
                                  ┌────────────────────────────┐
                                  │ Drum-mode track            │
                                  │   TRKMODE: BUSASG = 0      │ ──► plays on bus 0
                                  │   CC 0x97 CHORDMASK_BUS=1  │     (e.g. MIDI out)
                                  │   ChordMask processor      │
                                  │   reads PC-set from bus 1  │
                                  └────────────────────────────┘
```

When the UI surfaces materialize, candidates are: a new "ChMsk." sub-page
gated off TRKMODE GP16 (mirroring the FX/Robo two-sided convention from
MANUAL_FORK.md §Robotize Loop), or extending the TRKMODE row to carry an
extra field. Open question — see §7.

---

## 6. Reference

### 6.1 Files (code-truth anchor)

| Concern | File | Symbol(s) |
| --- | --- | --- |
| PITCHGEN page | `core/seq_ui_trkpitchgen.c` | `LCD_Handler`, `Button_Handler`, `Encoder_Handler` |
| Generator engine | `core/seq_generator.c` | `SEQ_GENERATOR_Engage`, `_Roll`, `_Disengage`, `_Bounce`, `_BounceRelocate`, `_Anchor`, `_Snap`, `_LockToggle`, `_MultCycle`, `_Tick`, `mutate_loop`, `write_loop_to_source` |
| Processor stack | `core/seq_core.c` | `SEQ_CORE_RenderTracks`, `chord_mask_render_range`, `SEQ_CORE_ProcessorBounce`, `SEQ_CORE_ProcessorBounceCapture`, `SEQ_CORE_ChordMaskSlotSync` |
| TRKMODE page (ChordMask UI) | `core/seq_ui_trkmode.c` | strength tile + overlay |
| CC definitions | `core/seq_cc.h` / `seq_cc.c` | `SEQ_CC_MODE`, `_CHORDMASK_STRENGTH`, `_CHORDMASK_BUS`, `_CHORDMASK_DRUM_L`, `_CHORDMASK_DRUM_H` |

### 6.2 CC additions vs upstream

| CC | Purpose | UI surface |
| --- | --- | --- |
| `0x96` | `chordmask_strength` (step 4) | TRKMODE GP7 |
| `0x97` | `chordmask_bus` (step 7) | none yet — CC/SysEx only |
| `0x98` | `chordmask_drum_l` (step 7) | none yet — CC/SysEx only |
| `0x99` | `chordmask_drum_h` (step 7) | none yet — CC/SysEx only |

Also see MANUAL_FORK.md for the robotize-loop CCs (`0x91..0x95`) that
predate the spine.

### 6.3 Testctrl probes (harness-only, useful for verifying UI claims)

| Cmd | Symbol | Use |
| --- | --- | --- |
| `0x5a` | `CMD_UI_INSTR_SET` | park cursor on a drum |
| `0x5b` | `CMD_TRACK_DRUM_INIT` | programmatic drum-mode setup |
| `0x5c` | `CMD_GENERATOR_QUERY` | dump live `loop[64]` + `locks` + `anchor[]` + `mult[]` of a slot |
| `0x5d` | `CMD_UI_TRACK_SET` | park visible track |
| `0x5e` | `CMD_TRACK_DRUM_PAR_SET` | seed a drum's Note layer without engaging a gen |
| `0x5f` | `CMD_TRACK_DRUM_PAR_GET` | symmetric readback |
| `0x60` | `CMD_GENERATOR_TICK_FORCE` | force one measure of mutation deterministically |
| `0x61` | `CMD_GENERATOR_DIAL_SET` | set range/rate/depth/contour exactly |
| `0x62` | `CMD_GENERATOR_MULT_SET` | stamp a MULT code at any step |

### 6.4 Worked workflow — techno bass (anchored chaos)

The §9 "3-frozen + 1-alive" recipe, shrunk to a single track:

```
  1.  TRKEVENT: track in drum mode.
  2.  PAR-ASG: assign one layer to Note. (lay_const fallback covers
      zero-valued steps, so the kick stays kicked.)
  3.  Navigate to PITCHGEN. Cursor on D 1 (your bass drum).

  4.  GP3 turn → range_min = C 1
      GP4 turn → range_max = C 3                  (narrow detune-feel range)
      GP5 turn → rate = 32                        (occasional moves)
      GP6 turn → depth = 16                       (small perturbations)
      GP7 turn → Ct: TRIANGLE                     (pull toward mid)

  5.  GP1 → ENGAGE
      → loop[] seeded, anchor[] auto-captured, source rewritten
      → kick starts walking around C 2 every measure.

  6.  Listen. When measure 7 lands a phrase you like:
      GP3 button → ANCHOR
      → anchor[] now matches what you just heard.
      → next SNAP returns *here*, not to the original seed.

  7.  Sculpt — datawheel to step 3, GP6 button → LOCK
      Datawheel to step 11, GP6 button → LOCK
      Datawheel to step 14, GP7 button × 3 → MULT 2× (this step drifts harder)
      Datawheel to step 9, GP7 button × 1 → MULT 0× (this step is glued)

  8.  Let it run. Steps 3/11 are frozen, step 9 never moves, step 14 chases
      the chaos hard, everything else drifts gently. Anchor is your safety
      net.

  9.  When it lands again:
      GP3 → ANCHOR (re-capture the new shape)

 10.  When the take is "the one":
      GP8 → BOUNCE in place
      → loop discarded, slot freed, source frozen.
      → re-ENGAGE seeds fresh; locks/MULT/anchor reset because the slot
         was recycled.
```

---

## 7. Open UI questions (this is where we iterate)

Anything below is **not yet built** — these are surfaces we should refine
together. When one of these crystallizes, it gets a section above and this
entry gets retired.

### 7.1 Step-LED visualization on PITCHGEN

**Today:** `ui_selected_step` is only on the LCD as `Stp:NN`. The cursor /
locks / MULT are invisible on hardware step-LEDs.

**Open:** how to use the 16-step LED row (and the 4-page view-switch
plumbing) to show the 64-step loop. Three candidates with mockups:

**Candidate (a) — page-by-page, brightness = MULT, lock = blink:**

```
   loop steps 0..15 (page 1/4 of 64 — view-switch advances page)

   step:  [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           ·   ·   ░   ░   ░   ·   █   █    ░   ░   ░   ·   ░   ░   ·   ·
                            ▲▲▲              ▲▲▲▲▲▲▲▲             ▲
                            cursor=4         locks 9..11           cursor=13
                            (alt-blink)      (slow-blink)          (alt-blink)

   page indicator (which 16-step page is showing):
   [1·][2 ][3 ][4 ]                page 1 of 4   (view-switch button changes)

   Each LED encodes MULT as brightness; locked steps slow-blink; cursor
   alt-blinks. §A3 spec aligned. Cost: brightness driver + 64-step state
   array on the page module.
```

**Candidate (b) — zoomed, 4 loop steps per LED:**

```
   16 LEDs = the 64-step loop quartered (4 steps OR'd per LED)

   step:  [1 ][2 ][3 ][4 ][5 ][6 ][7 ][8 ] [9 ][10][11][12][13][14][15][16]
           ·   ░   ▒   █   ░   ·   ·   ·    █   ░   ·   ·   ·   ·   ·   ·
                       ▲                    ▲
                       chunk 3 = loop[12..15] has 1 locked step
                       chunk 9 = loop[32..35] has full MULT 1× content

   page indicator: not needed (always all 64 steps in view)

   Always shows the whole loop. No per-step LOCK detail. Cursor as a
   moving full-bright LED over the chunk. Friendlier when the track is
   short (16 or 32 steps) — no page-switching needed.
```

**Candidate (c) — hybrid two-row:**

```
   row 1 (LEDs 1..8)  = current 8-step zone, full per-step detail
   row 2 (LEDs 9..16) = global mini-map, 8 chunks of 8 loop steps each

   (depends on the hardware actually having a second row of step-LEDs in
   the same physical band — midiphy V4+ does, stock V4 doesn't. Verify
   before committing.)
```

**Recommendation:** (a) is the §A3 plan and the §2.3 sweep contract
(every dial 0→max) extends naturally to brightness. (b) is the lower-
cost listen-test win — could ship as the placeholder until (a)'s
brightness driver is wired. (c) hinges on hardware availability — open.

### 7.2 Surfacing the LOOP_LEN-vs-track-length mismatch

**Today:** the cursor scrolls 0..63 regardless of track length. On a 16-step
track, positions 16..63 of the cursor edit loop bytes that never play.

**Open:** clamp the cursor to `num_steps` (loses pre-arming for future
length expansion), or surface the mismatch in the LCD (e.g.,
`Stp:03/16` with the denominator = track length). The latter is cheaper
and doesn't reduce capability.

### 7.3 Step-7 mask polish UI surface

**Today:** CC-only. See §5.

**Open:** is this a TRKMODE sub-page (GP16 toggles between "ChordMask
basic" and "ChordMask scope+bus")? A processor-stack page that doesn't
exist yet? A modifier overlay on the step LEDs to show which drums are
in-scope?

The processor-stack page is the larger question — it's coming for the
phase-onward processors (voice-leading, robotize-pitch, etc.). The
step-7 dials might want to ride it.

### 7.4 Surfacing "what gen is on this track" beyond GP1 LED

**Today:** the LED reflects the cursor's slot. If a gen is engaged on a
different drum, the only signal is the row-1 RHS hint when the cursor
sits on an *empty* drum (`GEN on Dnn`).

**Open:** should the page show a per-drum row indicator (16 dots,
each = "gen here" / "empty" / "Note content but no gen")? With 64 pool
slots and up to 16 drums × 16 tracks, this is the page-scope visualization.
Could also live on the EDIT page as a context tag (`*PGen*` indicator
analogous to the `*RbLp*` tag from MANUAL_FORK.md §Robotize Loop).

### 7.5 Pool exhaustion UX

**Today:** ENGAGE returns `-1`, LCD shows `pool full (64/64)` for 2s, no
recovery path.

**Open:** §9 deferred LRU eviction until "play behavior demands." If we
hit it in a live set, the response is probably: "freeze the oldest gen
into source automatically and reuse its slot." That's a §9-grade decision
— don't build it yet, but worth a sentence in this doc when we make the call.

### 7.6 PITCHGEN row-1 LHS dial strip is misaligned to its encoders

**Today:** the dial labels on row 1 LHS (`Lo:C·2·Hi:C·6·R:008·D:127·Ct:Uni`)
sit at cols 0..31 — but the encoders that drive those dials are GP3..GP7,
whose center columns are 12, 17, 22, 27, 32. The labels are shifted ~10 cols
to the **left** of their actual controls. Tracing GP3's button (●) at col
12 lands on `r` (the second char of `Trk` on row 0) and `·` (a separator
space inside `Hi:C·6` on row 1) — neither tells you GP3 controls range_min.

**Why this matters:** muscle memory builds from "label is under knob." Today
a new pilot looks at `Lo:C 2` at the left edge, naturally reaches for GP1,
and pushes ENGAGE instead. The label has to migrate to the GP that owns it.

**Open** — three repair candidates with mockups (LCD 1 cols 0..39 only):

```
   (a) shift the whole strip right by 10 cols, padding the left:
       cols:   0    5    10   15   20   25   30   35
                              Lo:C·2 Hi:C·6 R:008 D:127 Ct:Uni·
       GP#:    1    2    3    4    5    6    7    8
                              ▲       ▲     ▲     ▲     ▲
                              now under the correct encoders.

   (b) shrink the labels and inline them at GP centers (5-col blocks):
       cols:   0    5    10   15   20   25   30   35
                          Lo·C2 Hi·C6 R:08  D:127 Ct:Un
       GP#:    1    2    3     4     5     6     7    8
                          ▲     ▲     ▲     ▲     ▲
       (sacrifices the 'Lo:' / 'Hi:' colon spacers; "R:08" loses a digit
        of range; "Ct:Un" truncates contour name)

   (c) move ENGAGE/UNDO to GP9/GP10 (the LCD-2 side) and put dials at
       GP1..GP5 directly. Big-bang change — affects muscle memory of
       anyone who already learned the page. Probably not the right call.
```

**Recommendation:** (a). Shift right by 10 cols. Costs 10 cols of LCD-1
header space on row 1 LHS, which is currently empty anyway (the row-1 LHS
has no static labels — the dials are the only content). The shift makes
the page self-documenting: every dial label sits directly under the
encoder that drives it.

The same critique applies to TRKMODE row 1 LHS — see §2 mockup state A:
the mode tiles (`>Normal<` at col 9, `>Arpeggiator<` at col 19) don't
correspond to the GPs that *select* those modes (GP3 cycles, GP7 = strength).
TRKMODE is a different beast (mode-picker vs. dial-strip), and the open
question for it is more about whether the legacy 5-tile layout survives
when ChordMask becomes one entry in a processor-stack page (§7.3).

---

*Maintenance note:* this doc is regenerated against source whenever the
LCD format strings in `seq_ui_trkpitchgen.c` or `seq_ui_trkmode.c` change.
The mockups are not hand-art — they're literally the format-string output
with the dynamic fields filled in. If a mockup ever drifts from what
shows on hardware, the mockup is wrong, not the firmware.
