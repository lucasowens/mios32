# MBSEQV4 — Control Surface Map (fork)

The **complete gesture map** of the **midiphy SEQ V4+** front panel as wired by this
fork's `hwcfg/lso/MBSEQ_HW.V4` — every physical control, every button combo, every
hold/tap gesture, and the per-page meaning of the two button rows.

This is the **UX-design companion** to `MBSEQV4_HARDWARE_GLOSSARY.md`:
- the **glossary** owns the *vocabulary* (what each control is called, the seam rule, wiring).
- this **map** owns the *grammar* (what each control and combination *does*, and where the
  unclaimed real estate is for new features).

Use the **bold glossary terms** here. Lines marked **[fork]** are repurposed away from
stock MBSEQ. Everything below was verified against source (symbol names, not line numbers —
line numbers drift in this fork). Last verified: 2026-07-12 (merge passes — the rack is
now 10 rows: ChordMask dissolved into Ptch, Limit into Voic, Arp into TGen; the
pitch/generative cluster leads — Ptch/Voic/Tens/PGen/TGen; PROC base-layout header).

---

## 1. The control inventory

The physical panel as wired by `lso/MBSEQ_HW.V4`, by cluster. "Stock role" = mainline
MBSEQ V4 behavior; "Fork role" = what this build does with it.

### 1.1 The GP cluster (the heart — under the screen)

| Control | Count | Firmware symbol | Stock role | Fork notes |
|---|---|---|---|---|
| **GP encoders** (row A) | 16 | `SEQ_UI_ENCODER_GP1..16` | Value edit per page; push = FAST | Per-page repurposable; push has no LED. First repurposed pushes: Capture GP1 (FILL⇄LOOP). |
| **GP row** (row B) | 16 | `SEQ_UI_BUTTON_GP1..16` | EDIT = the 16 steps; menu page = value-picker | The universal **value-picker / step row**; tri-colour LEDs (`ui_gp_leds`). |
| **B-row** (row C) | 16 | `BUTTON_DIRECT_TRACK1..16` | Direct track-select 1–16 | **Context row** — meaning follows **sel-view** (§2.4). This is the "two things at once" row. |
| **datawheel** | 1 | `SEQ_UI_ENCODER_Datawheel` | Master value wheel (+push) | Also the **morph ride**, the **Capture GRAB dial**, and **[fork]** in PROC mode the **track walker** (turn = track ±1, push = jump a group — §5a). |

### 1.2 Selection-mode cluster (sets the **sel-view** — what the B-row means)

These pick a *dimension* for the B-row; they do **not** navigate pages.

| Control | Panel | Firmware symbol | sel-view set | Fork notes |
|---|---|---|---|---|
| **TRACK** | Track | `BUTTON_TRACK_SEL` | TRACKS (default) | direct track-select on B-row |
| **PARAM** | Param. | `BUTTON_PAR_LAYER_SEL` | PAR | parameter-layer picker |
| **TRIGGER** | Trigger | `BUTTON_TRG_LAYER_SEL` | TRG | trigger-layer picker |
| **INSTR** | Instr. | `BUTTON_INS_SEL` | INS | drum-instrument picker; gates the **drum-pad / keyboard** live surface (§4) |
| **STEP** | Step | `BUTTON_STEP_VIEW` | STEPS | 16-step page picker |
| **MUTE** | Mute | `BUTTON_MUTE` | MUTE | mute view |
| **BOOKMARK** | Bookm. | `BUTTON_BOOKMARK` | BOOKMARKS | **[fork]** SELECT+tap = CHECKPOINT, SELECT+hold ≥1s = REVERT (§3) |
| **PHRASE** | Phrase | `BUTTON_PHRASE` | PHRASE | **[fork]** snapshot waypoints on B-row; also still opens the song-arrangement page |

### 1.3 Mode / state (row above transport)

| Control | Panel | Firmware symbol | Stock role | Fork role |
|---|---|---|---|---|
| **SOLO** | Solo | `BUTTON_SOLO` | solo selected track | (unchanged) |
| **FREEZE** | Metr. | `BUTTON_METRONOME` | metronome click | **[fork]** generator-mutation master switch — `seq_core_state.FREEZE`; lit = FROZEN. Stock metronome gone. |
| **LOOP** | Loop | `BUTTON_LOOP` | fx-loop toggle | (unchanged) |
| **RECORD** | Rec. | `BUTTON_RECORD` | record arm | also arms the **drum-pad / keyboard** surface to record vs. preview (§4) |
| **LIVE** | Live | `BUTTON_LIVE` | jam/live page | **[fork]** toggles the **PROC rack page** (§5a) — tap in to sculpt, tap out returns where you came from. Its LED = "in PROC mode". (The displaced Live-Forwarding toggle is gone; `FWD_MIDI` is pinned on.) |

### 1.4 Transport (bottom-left)

| Control | Panel | Firmware symbol |
|---|---|---|
| **REW / FWD** | ◀◀ / ▶▶ | `BUTTON_REW` / `BUTTON_FWD` (held = shuttle) |
| **STOP / PLAY / PAUSE** | ■ / ▶ / ❙❙ | `BUTTON_STOP` / `BUTTON_PLAY` / `BUTTON_PAUSE` |

### 1.5 Function buttons (bottom row, left of logo)

| Control | Panel | Firmware symbol | Stock role | Fork role |
|---|---|---|---|---|
| **COPY / PASTE / CLEAR** | Copy/Paste/Clear | `BUTTON_COPY/PASTE/CLEAR` | copy/paste/clear in context | **CLEAR** is also the UNDO/REDO half of **SELECT+CLEAR** (§3) |
| **MOVE / SCROLL** | Move/Scroll | `BUTTON_MOVE/SCROLL` | move / scroll steps | (unchanged) |
| **FAST** | Fast | `BUTTON_FAST` | encoder fine-mode | (unchanged) |
| **ALL** | All | `BUTTON_ALL` | edit-all-steps | (unchanged) |
| **SELECT** | **"Shift"** | `BUTTON_SELECT` | cycle item / confirm | the **master modifier** — gates every SELECT+X combo. LEFT/RIGHT are dead so this is *the* item-cycle key. |

### 1.6 Navigation / page buttons (bottom row, right of logo)

| Control | Panel | Firmware symbol | Stock role | Fork role |
|---|---|---|---|---|
| **`<` / `>`** | `<` / `>` | `BUTTON_Up` / `BUTTON_Down` | value ∓1 (datawheel nudge) | mostly unchanged — NOT item-cycling. **[fork]** on the **PROC page** either key flips the focused row's **plane** (OPERATE⇄LOOP/STEPS, §5a); in **INS sel-view** they scroll the keyboard ±1 semitone (§4). |
| **LEFT / RIGHT** | — | `BUTTON_Left` / `BUTTON_Right` | cursor L/R | **DEAD** — unwired (`0 0`). Free firmware functions, no physical key. |
| **EXIT** | Exit | `BUTTON_EXIT` | leave page / up a level | (unchanged) |
| **MENU** | Menu | `BUTTON_MENU` | MENU+GP → page shortcuts | (unchanged) |
| **EDIT** | Edit | `BUTTON_EDIT` | → Edit page | (unchanged) |
| **PATTERN** | Pattern | `BUTTON_PATTERN` | → Pattern page | **[fork]** hold = **CAPTURE-to-slot** gesture (§3) |
| **CAPTURE** | "Song" | `BUTTON_SONG` | → Song page | **[fork]** → opens the **unified Capture page**; song-arrangement moved to PHRASE |
| **UTILITY** | Utility | `BUTTON_UTILITY` | → Utility page | **[fork]** hold = **retroactive grab** gesture (§3) |

### 1.7 Displays & indicators

| Element | What it is |
|---|---|
| **LCD-L / LCD-R** | Two 2×40 char panels = logical **2×80**, with **the seam** at col 39/40 (never straddle it). |
| **TPD** | 8×8 Track Position Display (green/red), play position. |
| **GP LEDs** | tri-colour ring per GP key (`ui_gp_leds`), via BLM8x8. |
| **BEAT / MEASURE LEDs** | `LED_BEAT` / `LED_MEASURE`. |

> **No dedicated panel key** on this LSO build for stock GROUP, SCRUB, FOLLOW, MIXER, SAVE,
> SAVE-ALL, tap-tempo, or the per-track config pages (TRKMODE/DIR/LEN/…). They're firmware
> functions reached via **MENU + GP** shortcuts or other combos.

### 1.8 LED feedback conventions **[fork]**

Every mode indicator obeys three rules (enforced in `SEQ_UI_LED_Handler`, `seq_ui.c`).
New modes should follow them so the panel stays legible:

| Signal | Means | Examples |
|---|---|---|
| **Steady** | *You are here / holding this now* — the page you're on, or the key you physically hold for a gesture. | EDIT/MUTE/CAPTURE page LEDs; **PATTERN** & **UTILITY** lit while held for their capture gesture. |
| **Flash** (`ui_cursor_flash` timebase, in phase with the GP cursor) | *An armed/live MODE is engaged* — a background state you should know about, whether or not you're looking at it. | **PHRASE** flashes on any page while a **POSTURE-MORPH is armed** (rideable on the datawheel); **INSTR** flashes when the **INSSEL play-surface** is hot (drum-pad/keyboard) vs. steady = plain instrument-select. |
| **GP-row aim** | The **hold-then-paint family**'s live target. | **UTILITY** held → depth thermometer (grabbable loops); **PATTERN** held → letter cursor on the aimed dest group; **B-row PULL** held → letter cursor on the aimed source pattern. |

Contextual safety-net signal: while **SELECT** is held, **BOOKMARK** lights if a checkpoint
exists this session (REVERT has a target) — visible exactly when the SELECT+BOOKMARK gesture
is armed. Persistent modes keep their own steady light: **FREEZE** on METRONOME (lit = frozen).

> LED reads in the handler must stay trivial (RAM/state only — **never SD or heavy queries**
> per pass). The CHECKPOINT-available signal uses a cheap RAM flag
> (`SEQ_PATTERN_CheckpointValid`), seeded once at session load, not a per-pass disk probe.

---

## 2. The combo grammar

Five mechanisms generate every gesture on this panel. Learn these and the whole surface
is predictable.

### 2.1 Latching modifiers (`seq_ui_button_state`)

Holding certain buttons sets a flag in the `seq_ui_button_state_t` bitfield; while held,
other buttons change meaning. The performance-relevant flags:

`SELECT_PRESSED` · `EDIT_PRESSED` · `MUTE_PRESSED` · `PATTERN_PRESSED` · `SONG_PRESSED` ·
`PHRASE_PRESSED` · `FAST_ENCODERS` / `FAST2_ENCODERS` · `ALL` (`CHANGE_ALL_STEPS`) ·
`MENU_PRESSED` · the view-latches (`STEP_VIEW`, `BOOKMARK`, `PAR/TRG_LAYER_SEL`,
`INS_SEL`, `TRACK_SEL`). The page-change latches share one radio group
(`PAGE_CHANGE_BUTTON_FLAGS`) so only one is active at a time.

### 2.2 The deliberate two-button idiom **[fork]**

Destructive or load-bearing actions require **two deliberate buttons**, never a bare tap —
so a single fat-finger can't fire them:
- **SELECT + CLEAR** → UNDO/REDO (never a bare CLEAR-to-wipe surprise).
- **SELECT + BOOKMARK** → CHECKPOINT / REVERT.
- **SELECT + tap waypoint** → arm morph (vs. bare tap = recall).

### 2.3 Hold-vs-tap (time-thresholded) **[fork]**

The same button forks on hold time (`ANCHOR_REVERT_HOLD_MS` = 1000 ms):
- **SELECT+BOOKMARK** tap (<1s) = CHECKPOINT; hold (≥1s) = REVERT.
- **PHRASE** waypoint tap = RECALL; hold (≥1s) = CAPTURE into that waypoint.

### 2.4 The sel-view system — "two things at once"

The **B-row** (row C) is a second 16-key row whose meaning is set by the **selection-mode
cluster** (§1.2). Default = TRACKS (track-select). This lets you pick a destination/track
on the B-row while the **GP row** stays free as the value/step picker. `seq_ui_sel_view`.

### 2.5 Hold-a-key-then-GP capture/pull gestures **[fork]**

The big performance verbs are "hold a page button, then paint on the rows":
- **PATTERN held** → B-row picks dst track, **GP1–8** pick group, **GP9–16** commit to slot.
- **UTILITY held** → B-row picks dst track, **GP-n** grabs the last *n* loops.
- **B-row held** (TRACKS view) → arm a PULL: **GP1–8** src letter, **GP9–16** commit pull onto the held track.

The **half convention** (GP1–8 = left/letter, GP9–16 = right/number, lands on the seam)
is reused by all three.

---

## 3. Combo & gesture reference

Every non-trivial combo. "✓" = shipped/committed.

| Gesture | Does what | Where (symbol) | Ship |
|---|---|---|---|
| **MENU + GP1–16** | jump to menu page (EDIT/MUTE/PATTERN/…) | `SEQ_UI_PAGES_MenuShortcutPageGet` | ✓ |
| **MENU + EXIT** | Follow toggle (alt) | `SEQ_UI_Button_Exit` | ✓ |
| **MENU + SELECT** | Bookmark (alt) | `SEQ_UI_Button_Select` | ✓ |
| **FAST / FAST2 + GP-enc** | encoder fine / ultra-fine | `FAST_ENCODERS` | ✓ |
| **ALL + GP-enc** (EDIT) | edit all steps at once (ramp if option) | `CHANGE_ALL_STEPS` | ✓ |
| **LayerC + LayerA/B** | inc/dec the par/trg layer | `SEQ_UI_Button_ParLayer/TrgLayer` | ✓ |
| **COPY + PASTE** (EDIT) | duplicate steps | `SEQ_UI_Button_Paste` | ✓ |
| **REW / FWD held** | shuttle back / forward | `SEQ_UI_Button_Rew/Fwd` | ✓ |
| **MUTE held + B-row** | layer-mute the current track (vs. track-mute) | `MUTE_PRESSED` | ✓ |
| **SELECT + CLEAR** | **[fork]** UNDO/REDO toggle (unified journal) | `SEQ_UI_JournalToggleDispatch` → `SEQ_CORE_JournalUndo/Redo` | ✓ |
| **SELECT + BOOKMARK** tap | **[fork]** CHECKPOINT (bless all 4 groups) | `SEQ_UI_Button_Bookmark` → `SEQ_PATTERN_Checkpoint` | ✓ |
| **SELECT + BOOKMARK** hold ≥1s | **[fork]** REVERT to checkpoint | `SEQ_PATTERN_Revert` | ✓ |
| **METRONOME (FREEZE)** | **[fork]** freeze/thaw generator mutation | `SEQ_UI_Button_Freeze` → `seq_core_state.FREEZE` | ✓ |
| **PATTERN held + B-row + GP** | **[fork]** CAPTURE visible track → slot (B-row=dst trk, GP1–8=group, GP9–16=commit) | `SEQ_UI_Button_Pattern` / `_DirectTrack` | ✓ |
| **UTILITY held + B-row + GP-n** | **[fork]** retroactive GRAB last *n* loops → dst track | `SEQ_UI_Button_Utility` (GP dispatch in `SEQ_UI_Button_GP`) | ✓ |
| **B-row held + GP** (TRACKS) | **[fork]** PULL a stored track onto the held live track (GP1–8 letter, GP9–16 commit) | `SEQ_UI_Button_DirectTrack` | ✓ |
| **CAPTURE (Song) tap** | **[fork]** open/close unified Capture page | `SEQ_UI_Button_Song` → PAGE_CAPTURE | ✓ |
| **LIVE tap** | **[fork]** toggle the PROC rack page (§5a); LED = in-PROC | `SEQ_UI_Button_Live` → PAGE_PROC | ✓ |
| **PROC: B-row tap / double-tap** | **[fork]** focus a rack row / row on-off (bypass, ENGAGE⇄DISENGAGE; Ptch dbl-tap also drops the ChordMask playmode) | `SEQ_UI_SEL_VIEW_PROC` B-row dispatch | ✓ |
| **PROC: `<` `>` (or Up/Down)** | **[fork]** flip the focused row's plane (Robotize OPERATE⇄LOOP, PitchGen/TrigGen OPERATE⇄STEPS) | `SEQ_UI_PROC_page_Button` | ✓ |
| **PROC: GP-encoder push** | **[fork]** snap dial to its pass-through detent; ACTION dials execute (Reseed/Freeze/Roll/Anchor/Snap/Bounce) | `PROC_KIND_ACTION` / default-snap | ✓ |
| **PHRASE → B-row waypoint** tap | **[fork]** RECALL phrase (whole-organism snapshot) | B-row dispatch (PHRASE sel-view) | ✓ |
| **PHRASE → B-row waypoint** hold ≥1s | **[fork]** CAPTURE live organism into waypoint | same | ✓ |
| **SELECT + B-row waypoint** | **[fork]** arm POSTURE-MORPH toward that phrase | same; ride on **datawheel** / GP-bar | ✓ |

### Capture page (CAPTURE/Song button) control map ✓

- **datawheel** = GRAB dial (`Save` → `1b…Kb`); **`<`/`>`** nudge ∓1.
- **B-row** = destination track. **GP row** = destination pattern (GP1–8 letter, GP9–16 number = commit).
- **GP1 encoder** = `Fit: FILL ⇄ LOOP` (loop at the canvas vs. at the grab).
- **GP2 encoder** = `Ph: GRID ⇄ HEARD` — where a PLAYING grab's window ends (loop-aligned vs.
  the playhead, "keep the last N bars exactly as heard"). STOPPED re-sim is always GRID.

---

## 4. The live-play surface (B-row, INSTR sel-view) **[fork]**

With **OPT → "Drum pads + 1-row keyboard"** on (`INSSEL_DRUM_TRIGGER`), the **B-row**
(selection buttons) becomes a playable surface whenever the **INSTR** sel-view is active —
the first "get" of the fusion instrument. Because it's on the B-row (sel-view), you play it
from **any page** while the GP row keeps its page meaning ("two things at once"). RECORD-armed
= records into the track; else previews. (`seq_ui_inssel.c`, `SEQ_UI_INSSEL_SelectRow_Button`.)

**INSTR toggles the mode.** A clean **re-tap of INSTR** (while already in INS sel-view) flips
the B-row between **instrument-select** and the **play-surface** (`INSSEL_DRUM_TRIGGER`).
INSTR-held + a B-row tap (drum silent-retarget) does **not** toggle. The **INSTR LED** reads
the mode: flash = play-surface hot, steady = instrument-select.

| Track type | B-row plays | held-modifier + key |
|---|---|---|
| **Drum** | TR-909-style pads (one drum per key), vel 100 on press | **INSTR-held + tap = silent retarget** of the selected instrument (`INS_SEL`; bare tap plays). Frees SELECT. |
| **Melodic** | a one-row keyboard; layout set by **OPT → "Melodic keyboard layout"** or **SELECT + key2** | **SELECT + key1 / key16 = octave down / up** (coarse scroll ±12) · **SELECT + key2 = cycle layout** · **SELECT + key15 = collapse-to-scale toggle** |

Melodic layouts (`INSSEL_KBD_LAYOUT`, base = middle C `0x3c`):
**Chromatic (isomorphic)** key k = base + k·**Jump** · **Scale degrees (in key)** ·
**Diatonic chords (in key)** in-key triads. Scale & root read live.
**Collapse to scale** (`INSSEL_KBD_FOLD`, OPT item or SELECT+key15): **Jump strides
scale degrees** from the tonic instead of semitones — compact, every key a distinct
scale note: Jump 1 = scale steps, 2 = diatonic thirds, 3 = diatonic fourths, …

**Isomorphic keyboard controls** (melodic play-surface) — all live in INS sel-view from any page:
- **GP1 encoder** = **Jump** — semitones between adjacent keys, 1..12 (1=chromatic,
  2=whole-tone, 5=fourths, 7=fifths, 12=octaves). Chromatic layout only.
  **SELECT + GP1 encoder** = **velocity** (1..127, plays *and* records; default 100).
- **‹ / ›** and **datawheel** = **scroll** the whole row ±1 semitone (fine); **SELECT +
  key1/key16** = ±1 octave (coarse). One row reaches the full range by scrolling.
  (Jump/scroll are intercepted globally in `SEQ_UI_Encoder_Handler` / `SEQ_UI_Button_Up/Down`
  so they work off the INSSEL page; a transient LCD readout confirms base note / Jump /
  velocity / layout / fold.)
- **B-row LEDs** (native 2-colour `select_leds`): **green** = in-scale key, **amber** =
  root/tonic, **dark** = out-of-scale, **red** = key under a finger
  (`SEQ_UI_INSSEL_KeyboardLeds`). The GP row keeps its page function. The INSSEL page's
  LCD shows layout(+fold) / Jump / base note / transpose / velocity / scale on line 0 and
  the 16 keys' note names on line 1.

**EDIT RECORDING punch-in (hold-step + key) [fork].** On the EDIT page, holding a GP step
button arms the stock MIDI-learn; while it's held, a B-row **keyboard key or drum pad
punches its note into the held step** — same path an external MIDI keyboard takes
(`SEQ_UI_NotifyMIDIINCallback` is offered the event before the play/record branch,
`seq_ui_inssel.c`). Chord-layout presses land **atomically** on the held step (all three
notes; the stock per-note learn is last-note-wins). Works with **ALL held** too: the learn
handler copies the punched note across all selected steps. Monitoring follows the stock
learn behavior (the step is replayed as it now sounds).

*The stock SELECT-tap learn latch on the EDIT page is REMOVED* **[fork]** — SELECT is
the play surface's modifier (octave/layout/fold/velocity), and every tap was arming a
latch that then swallowed the keys. **Hold-a-step is the only learn gesture.**

---

## 5a. The Processor Rack (PROC page — LIVE button) **[fork]**

The operating grammar's home (G0–G3 arc, §9/LOG). **LIVE toggles the page**; while on it,
`seq_ui_sel_view = PROC` claims all three rows. Leaving by any route (EXIT, LIVE re-tap,
any page change) drops the latch; a dirty Groove paint persists `MBSEQ_G.V4` on the way out.

**The three rows in PROC mode:**
- **B-row** = the rack: one key per row, in order
  **Ptch · Voic · Tens · PGen · TGen · Grve · Humn · Robo · Echo · LFO**
  (10 rows after the 2026-07-12/13 merges — pitch/generative cluster, the feel trio
  (Grve/Humn/Robo), then the emission tail:
  **Ptch** = the merged pitch-domain cockpit (old Pitch's Semi/Oct/FTS/Scle/Root/Deg +
  the dissolved ChordMask row's Str/Bus + mask face; fronts BOTH stack slots; where LIVE
  lands you); **Voic** = voicing + the dissolved Limit row (Sprd/Inv/Drop/Strm/Tilt ·
  spacer cell · Lo/Hi range clamp; fronts the LIMIT stack slot alongside its emission CCs.
  At rest the range reads as the full keyboard — Lo 0 / Hi 127, both = open; Hi's
  pass-through detent is the TOP of its sweep, stored as the stock "off" 0);
  **Tens** (position 3, 2026-07-12) = Grip/Grav/**Shade** (the GRAVITY page's
  brightness ladder joins the rack — a view on the GLOBAL scale, "---" when
  off-ladder; push/reset = no-op so the scale is never yanked onto the ladder) ·
  spacer · **FTS** (doubled from Ptch for convenience — same per-track flag); the
  16-track grip bar rides unlabeled in the dead cells (cols 24-39), zone name at
  the row-readout home (col 41)).
  Tap = focus (always lands on the row's primary plane). **Double-tap (<350 ms) = the row's
  on/off gesture**: emission rows flip their native bypass bit (config preserved — on Voic
  the bypass only gates the voicing dials; **an active range clamp keeps applying**, kill
  it by pushing the Lo/Hi encoders to their detents); generator rows ENGAGE⇄DISENGAGE
  (loop preserved — on TGen, which absorbed the ARP row into its OPER cells 6-7, the
  double-tap stays the generator's gesture: an armed arp keeps playing, kill it by
  dialling/pushing its Arp cell to Off); param rows (Ptch/Tension) reset to
  pass-through — Ptch also returns a ChordMask-playmode track to Normal (the painted
  mask CCs survive).
  LEDs: **green = occupied** (winks when bypassed / at true pass-through), **red = focused**
  (reads amber on the focused occupied row).
- **GP encoders** = the focused row's dials on the current **plane** (labels on the LCD).
  **Push = snap to the pass-through detent**; ACTION dials execute instead
  (Robotize Rsd/Frz; PitchGen/TrigGen Roll/Anc/Snp/Bnc; Grve Clr).
- **Datawheel** = the **track walker** (2026-07-13 — the B-row is claimed by the rack,
  so the wheel is how you move through the matrix): **turn = track ±1**, **push =
  jump a group** (G1→G2→G3→G4→G1, same track position). Focused row + plane survive
  the switch — sit on one processor and compare it across tracks; GxTy top-right is
  the feedback. (Replaces the wheel's old duplicate ride on the headline dial.)
- **GP row** = the plane's **face** (bespoke surface, where the row declares one):
  Ptch **Self mask** (GP1–12 = pitch classes, Self bus mode only; the LCD keyboard + LEDs
  show the mask ALWAYS — engaged or not — and visible == paintable) · Grve **paint**
  (16-step shape of the selected lane, custom templates — **quick tap (<350 ms) =
  toggle**: off SHADOWS the step's value, back on RESTORES it (only shadowless cells
  paint the Val brush — 0 = intensity-follow, else a literal signed offset); **hold =
  PEEK**: the Val cell reads out the held step (`+Int`/`-Int` for sentinel cells), a
  long release changes nothing; **hold + turn Val = dial that step's exact cell**;
  hold + push Val = erase (shadowed, re-tap restores). The shadow follows one template,
  cleared on style switch / Clr. Grve's Glob cell = the stock global-groove flag — on,
  Styl/Intn/Sync broadcast to every global track; Stps = template length; Clr =
  whole-template reset) · LFO **waveform palette**
  (tap to pick) · Tension **zone jump** (GP9–15 = DRONE…SLIP, GP16 = RESOLVE) · Robotize
  LOOP plane **bar anchors** (tap = reroll) · PitchGen/TrigGen STEPS plane **LOCK
  toggles** (16-step window into the 64-step loop, Win dial picks the quarter; since
  2026-07-13 the plane shows the window's TRUTH twice over — the LCD **activity
  strip** at row 1 right (`o` = sounding/unlocked "yours to lock", `#` =
  sounding+locked, `-` = locked silence, `.` = empty) and the **duo-color GP LEDs**:
  color 1 = triggered steps (visible pre-ENGAGE, so a punched-in phrase shows before
  the generator exists), color 2 = locks, both = the blend, playhead inverts as it
  sweeps. **Locks work pre-ENGAGE** (2026-07-13): the first lock tap ADOPTS a
  disengaged slot whose loop copies your source — nothing is written or randomized —
  and the later ENGAGE re-adopts the then-current source, arms the undo net, and
  mutates around your locks. Punch a phrase in, lock it BY SIGHT, then engage and
  let the generator fill around it).
- **`<` / `>` (or Up/Down)** = plane flip on two-plane rows (Robotize OPER⇄LOOP,
  PitchGen/TrigGen OPER⇄STEP, **LFO CONF⇄DEST** — the LFO carries the FULL stock
  parameter set since 2026-07-13: CONF = Wave/Amp/Phas/Step/Rst/1Sht/ClkD (+ the
  waveform-palette face), DEST = Note/Vel/Len/CC enable flags + the extra-CC stream
  (xCC#/xCC/Offs/PPQN); the old single-select Targ router is retired).

**Page LCD (base layout, 2026-07-12):** left screen = the 8-cell dial grid (label over
value, one 5-col cell per GP encoder; SPACER cells group dials — Voic/Tens — and pad
doubled dials to a fixed physical position — Roll on cell 8 of BOTH gen planes).
Right screen row 0 = the row's own readout at col 41 (Ptch: scale name + Deg landing
note; Voic: `Rng` when clamped; Tension: zone name; two-plane rows: the plane cue
`OPER 1/2` / `STEP 2/2` / `LOOP 2/2`) with the identity compressed and right-aligned —
`Ptch  1/11 G1T1` at cols 65–79 (row abbr · rack position · GxTy); BYP cue at col 55.
Row 1 = the row's custom status — on Ptch the **fixed 12-slot mask keyboard**: `M*:`
(Self, editable) / `M: ` (live bus, read-only), then one 3-col cell per pitch class
C…B — active PCs read as note names, inactive as dots.

**Grammar invariants** (why the rack stays predictable): every headline dial's 0 = true
pass-through (dark row); a 0→on turn engage-seeds the row so it is audible at once; bypass
never destroys config; the EDIT page edits SOURCE, the rack operates the processor.

> **Duplicated legacy surfaces (cleanup pending, see `doc/OPEN_ITEMS.md`):** the GRAVITY
> page (= Tension row + zone face), the ROBOLOOP page (= Robotize LOOP plane), the
> "Pitch Gen (POC)" menu page (= PitchGen row), and the FX_* menu pages (deep-edit of the
> same CCs the rack operates). The rack is the canonical operating surface.

---

## 5. Page-context cheat-sheet (GP row / B-row)

What the two rows mean on the pages that matter live. (B-row defaults to track-select
unless the page or sel-view overrides.)

| Page | GP row | B-row | Notable encoders / buttons |
|---|---|---|---|
| **EDIT** | the 16 steps (toggle / value) | track-select | GP-encs = step values; ALL = all-steps |
| **MUTE** | track mutes (or layer mutes if MUTE held) | track-select | — |
| **PATTERN** | bank A–H / num 1–8 (8\|8) | track-select | — |
| **CAPTURE** (Song) | dst pattern (letter\|num) | dst track | datawheel=GRAB; GP1-enc=FILL⇄LOOP |
| **PHRASE** | morph coarse bar (when armed) | 16 snapshot waypoints | datawheel=morph ride |
| **INS sel-view** (drum/kbd on) | *(page's own)* | drum pads / keyboard (green=in-scale, amber=root) | INSTR re-tap toggles play⇄select; RECORD arms rec-vs-preview; GP1-enc=Jump; ‹/›+datawheel=scroll; SELECT+key1/16=octave |
| **PROC** (LIVE) | the focused row's **face** (mask/paint/palette/zones/anchors/locks) | the **rack** (10 rows; tap=focus, dbl-tap=on/off) | GP-encs = row dials; push=detent/ACTION; ‹/›=plane; datawheel=track walker, push=group jump (§5a) |
| **GRAVITY** *(legacy — Tension row covers it)* | item hints; GP8=RESOLVE, GP16=→FX_SCALE | track-select | GP1-enc=GRAVITY, GP2=SHADE, GP3=GRIP, GP4=track |
| **ROBOLOOP** *(legacy — Robotize LOOP plane covers it)* | GP6=reseed, GP7=freeze, GP8=freeze-q; **SELECT+GP1–16 reroll measure anchor** | track-select | GP1-enc=track, GP2=palette len, GP3=loop start, GP4=cycles, GP5=rotate |
| **TRKEUCLID** (stock, menu "GENERATE") | Euclidean trigger preview | track-select | per trigger-layer params; one-shot destructive fills — the LIVING generators are the rack's PitchGen/TrigGen rows |
| **TRKDIR** | — | track-select | **[fork]** dir modes 7/8/9 = **WpHop / WpFill / WpHopSaw**: traversal bounces through pins painted on a **Waypoint par layer** (value = visit order, 0 = off-path; no layer → gated steps). |

---

## 6. Free real estate & overload (for designing new features)

The forward-looking layer — where new gestures can land, and what's already saturated.

### Unclaimed / underused inputs
- **GP-encoder pushes (×16)** — default FAST, **no LED**. Now spoken for on PROC
  (detent-snap + ACTION exec, §5a) and Capture GP1; still free elsewhere.
- **`<` / `>`** are value-nudge on most pages — but claimed on PROC (plane flip) and in
  INS sel-view (keyboard scroll); check §5a/§4 before reusing.
- **datawheel push** — used sparsely; a free per-page confirm/toggle. **[fork]** taken
  in PROC mode (group jump, §5a); still free elsewhere.
- **LEFT / RIGHT firmware functions** — physically dead, but the *firmware handlers* exist
  if ever re-wired.
- **SOLO, LOOP** — single-purpose; available as modifiers if a combo needs a fresh latch.

### Saturated / contended controls (handle with care)
- **SELECT ("Shift")** — the master modifier. Already gates UNDO/REDO, CHECKPOINT/REVERT,
  morph-arm, INSSEL melodic-keyboard octave, layer-context copy/paste. (Drum-pad silent
  retarget moved off SELECT to **INSTR-hold** — `INS_SEL` — to relieve this.) New SELECT+X
  must not collide with these and must respect the deliberate-two-button idiom.
- **GP row** — the most overloaded surface: steps, value-picker, capture/pull targets,
  drum pads/keyboard, morph bar, page-specific actions. Always context-gated by page +
  sel-view; new GP meanings need a clear page/sel-view scope.
- **B-row** — meaning fully determined by sel-view; safe to extend by adding a sel-view,
  risky to overload within an existing one.
- **The page-button hold gestures** (PATTERN/UTILITY/B-row-hold) — the "hold-then-paint"
  slot is a coherent family; new capture-like verbs should join it, not invent a new idiom.

### Design guardrails carried from the glossary
- **Seam rule**: never straddle col 39/40. 16-wide rows split 8\|8 on the seam.
- **No reliance on LEFT/RIGHT** for item-cycling — use **SELECT**.
- **Two things at once**: put destination/track picking on the **B-row**, keep the **GP row**
  as the value-picker.

---

## 7. Appendix

### 7.1 Pages reachable (MENU + GP or buttons)
EDIT, MUTE, PMUTE, PATTERN, SONG (→PHRASE), MIXER, TRKEVNT, TRKINST, TRKMODE, TRKDIR,
TRKDIV, TRKLEN, TRKTRAN, TRKGRV, TRGASG, TRKMORPH, TRKRND, TRKEUCLID (menu "GENERATE"),
TRKJAM, MANUAL, FX + FX_ECHO/HUMANIZE/ROBOTIZE/LIMIT/LFO/DUPL/LOOP/SCALE,
**ROBOLOOP** [fork, legacy], **GRAVITY** [fork, legacy], UTIL, BPM, OPT, SAVE, MIDI,
MIDIMON, SYSEX, CV, DISK, ETH, TRKLIVE, PATTERN_RMX, BOOKMARKS, INFO,
TRKPITCHGEN [fork, POC — retirement queued], **CAPTURE** [fork], **PROC** [fork, LIVE button].
(`seq_ui_page_t` in `seq_ui_pages.h`.)

### 7.2 Note on SysEx test-control codes
The HIL harness drives many of these gestures via SysEx command codes in
`seq_testctrl.c` (e.g. CMD_TRACK_REDO 0x47, CMD_RNG_SEED 0x4d, CMD_PHRASE_MORPH 0x4f,
CMD_TRANSPORT 0x4a). Those are **test inputs, not physical panel gestures** — out of scope
for this map, listed only so they aren't mistaken for hardware combos.
