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
line numbers drift in this fork). Last verified: 2026-06-30.

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
| **datawheel** | 1 | `SEQ_UI_ENCODER_Datawheel` | Master value wheel (+push) | Also the **morph ride** and the **Capture GRAB dial**. |

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
| **LIVE** | Live | `BUTTON_LIVE` | jam/live page | (unchanged) |

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
| **`<` / `>`** | `<` / `>` | `BUTTON_Up` / `BUTTON_Down` | value ∓1 (datawheel nudge) | (unchanged) — NOT item-cycling |
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
| **PHRASE → B-row waypoint** tap | **[fork]** RECALL phrase (whole-organism snapshot) | B-row dispatch (PHRASE sel-view) | ✓ |
| **PHRASE → B-row waypoint** hold ≥1s | **[fork]** CAPTURE live organism into waypoint | same | ✓ |
| **SELECT + B-row waypoint** | **[fork]** arm POSTURE-MORPH toward that phrase | same; ride on **datawheel** / GP-bar | ✓ |

### Capture page (CAPTURE/Song button) control map ✓

- **datawheel** = GRAB dial (`Save` → `1b…Kb`); **`<`/`>`** nudge ∓1.
- **B-row** = destination track. **GP row** = destination pattern (GP1–8 letter, GP9–16 number = commit).
- **GP1 encoder** = `Fit: FILL ⇄ LOOP`.

---

## 4. The live-play surface (INSSEL page) **[fork]**

With **OPT → "Drum pads + 1-row keyboard"** on (`INSSEL_DRUM_TRIGGER`), the **GP row** on
the **INSTR** page becomes a playable surface — the first "get" of the fusion instrument.
RECORD-armed = records into the track; else previews. (`seq_ui_inssel.c`)

| Track type | GP row plays | SELECT + GP |
|---|---|---|
| **Drum** | TR-909-style pads (one drum per GP), vel 100 on press | **SELECT + tap = silent retarget** of the selected instrument (stay on page) |
| **Melodic** | a one-row keyboard; layout set by **OPT → "Melodic keyboard layout"** | **SELECT+GP1 / SELECT+GP16 = octave down / up** |

Melodic layouts (`INSSEL_KBD_LAYOUT`, base = middle C `0x3c`):
**Chromatic (isomorphic)** GP1=tonic +1 semitone/key · **Scale degrees (in key)** ·
**Diatonic chords (in key)** in-key triads. Scale & root read live.

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
| **INSSEL** (drum/kbd on) | drum pads / keyboard | instrument-select | RECORD arms record-vs-preview |
| **GRAVITY** | item hints; GP8=RESOLVE, GP16=→FX_SCALE | track-select | GP1-enc=GRAVITY, GP2=SHADE, GP3=GRIP, GP4=track |
| **ROBOLOOP** | GP6=reseed, GP7=freeze, GP8=freeze-q; **SELECT+GP1–16 reroll measure anchor** | track-select | GP1-enc=track, GP2=palette len, GP3=loop start, GP4=cycles, GP5=rotate |
| **TRKEUCLID** (stock) | Euclidean trigger preview | track-select | per trigger-layer params |

---

## 6. Free real estate & overload (for designing new features)

The forward-looking layer — where new gestures can land, and what's already saturated.

### Unclaimed / underused inputs
- **GP-encoder pushes (×16)** — default FAST, **no LED**. Only Capture GP1 (FILL⇄LOOP)
  uses one so far. A whole row of latent momentary buttons, repurposable per page.
- **`<` / `>`** are only value-nudge — a free pair of contextual buttons on most pages.
- **datawheel push** — used sparsely; a free per-page confirm/toggle.
- **LEFT / RIGHT firmware functions** — physically dead, but the *firmware handlers* exist
  if ever re-wired.
- **SOLO, LOOP** — single-purpose; available as modifiers if a combo needs a fresh latch.

### Saturated / contended controls (handle with care)
- **SELECT ("Shift")** — the master modifier. Already gates UNDO/REDO, CHECKPOINT/REVERT,
  morph-arm, INSSEL octave/retarget, layer-context copy/paste. New SELECT+X must not
  collide with these and must respect the deliberate-two-button idiom.
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
TRKDIV, TRKLEN, TRKTRAN, TRKGRV, TRGASG, TRKMORPH, TRKRND, TRKEUCLID, TRKJAM, MANUAL,
FX + FX_ECHO/HUMANIZE/ROBOTIZE/LIMIT/LFO/DUPL/LOOP/SCALE, **ROBOLOOP** [fork],
**GRAVITY** [fork], UTIL, BPM, OPT, SAVE, MIDI, MIDIMON, SYSEX, CV, DISK, ETH, TRKLIVE,
PATTERN_RMX, BOOKMARKS, INFO, TRKPITCHGEN [fork], **CAPTURE** [fork].
(`seq_ui_page_t` in `seq_ui_pages.h`.)

### 7.2 Note on SysEx test-control codes
The HIL harness drives many of these gestures via SysEx command codes in
`seq_testctrl.c` (e.g. CMD_TRACK_REDO 0x47, CMD_RNG_SEED 0x4d, CMD_PHRASE_MORPH 0x4f,
CMD_TRANSPORT 0x4a). Those are **test inputs, not physical panel gestures** — out of scope
for this map, listed only so they aren't mistaken for hardware combos.
