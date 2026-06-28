# MBSEQV4 — Hardware Interface Glossary (fork)

Shared vocabulary for the **midiphy SEQ V4+** front panel as wired by **this fork's
`hwcfg/lso/MBSEQ_HW.V4`**. When we discuss UI work, we use the **bold glossary terms**
here so there's no ambiguity. Grounded in three sources:
- **panel** — the silkscreen labels on the physical midiphy V4+ (the photo).
- **firmware** — the names the code addresses (`seq_ui.h` button enum / `SEQ_UI_BUTTON_*`,
  `SEQ_UI_ENCODER_*`; HIL `BTN_*` in `seq_testctrl.c` / `tests/harness/sysex.py`).
- **wiring** — `hwcfg/lso/MBSEQ_HW.V4` (which physical pin drives which firmware function).

> ⚠️ = something I inferred and want you to confirm (see **Open questions** at the end).
> Lines marked **[fork]** are repurposed away from stock MBSEQ behavior.

---

## 1. Displays

| Glossary term | What it is |
|---|---|
| **LCD-L** | Left character display (2 lines × 40). |
| **LCD-R** | Right character display (2 lines × 40). |
| **the screen** | LCD-L + LCD-R together. Firmware addresses them as one **logical 2 × 80** buffer (column 0–39 = LCD-L, 40–79 = LCD-R; `SEQ_LCD_CursorSet(col, line)` uses this 0–79 space) — **but they are two physically separate panels with a bezel GAP between them.** See **the seam rule** below. |
| **the seam** | The physical break between col **39** (end of LCD-L) and col **40** (start of LCD-R). **The seam rule: never let a single word, field, number, or bracketed `>…<` item straddle the seam** — it gets torn across the bezel and reads as garbage. Compose each half (0–39 and 40–79) as if it were its own 40-char display. A full-width 16-cell picker is fine *because* it splits cleanly 8 | 8 at the seam (cell 8 ends at col 39, cell 9 starts at col 40 — this is exactly why the GP-1–8 / GP-9–16 **half convention** works). |
| **line 0 / line 1** | Top / bottom row of the screen (each spans both halves, across the seam). |
| **left half / right half** | LCD-L (cols 0–39) / LCD-R (cols 40–79) — the two physical 40-char panels. Lay out each independently; many pages put a "menu" on the left half and a "value picker" on the right half precisely so neither crosses the seam. |
| **TPD** | The dot-matrix Track Position Display (top-left). Shows play position; not used for menus. |

---

## 2. The GP cluster — the heart of the interface

**GP = General Purpose.** The central cluster under the screen is, top → bottom (this fork is a **LH / left-handed** midiphy V4+):

| Row | Glossary term | What it is |
|---|---|---|
| **A** | **GP encoders** (GP-enc 1…16) | The 16 detented knobs (8 under LCD-L, 9–16 under LCD-R). Each has an integrated **push-switch, no LED, default-mapped to FAST** — ⚠️ available to repurpose per page (see open notes). |
| **B** | **GP row** (GP 1…16) | The classic 16 GP keys of a stock MBSEQ. **Tri-colour LEDs.** In EDIT = the 16 steps; on a menu page = the **value-picker for the selected item**. |
| **C** | **B-row** (B 1…16) | The V4+ "second" 16-key row — **context-aware**, semi-independent from the GP row so you can **do two things at once**. Its meaning follows the **sel-view** set by the selection-mode cluster (§3). Firmware addresses it as `DIRECT_TRACK1..16`; default sel-view = TRACKS → direct track-select 1…16. |
| (below) | function / nav buttons | COPY/PASTE/CLEAR/… and `<`/`>`/EXIT/MENU/… — see §6/§7. |

| Glossary term | Panel | Firmware | Wiring (lso) |
|---|---|---|---|
| **GP encoders** | the 16 knobs (row A) | `SEQ_UI_ENCODER_GP1..GP16` | `ENC_GP1..16` |
| **GP row** | row B squares | `SEQ_UI_BUTTON_GP1..GP16` | `BUTTON_GP1..16` |
| **B-row** | row C squares | `BUTTON_DirectTrack` (`DIRECT_TRACK1..16`) | `BUTTON_DIRECT_TRACK1..16` |
| **datawheel** | "Select Mode" wheel | `SEQ_UI_ENCODER_Datawheel` | `ENC_DATAWHEEL` (+push `BUTTON_ENC_DATAWHEEL`) |

**Half convention** (used all over the firmware and respected per **the seam rule**): when a 16-wide row encodes two things, **GP 1–8 = left option (LCD-L), GP 9–16 = right option (LCD-R)**. E.g. Pattern page: GP1–8 pick the bank letter A–H, GP9–16 pick the number 1–8. The 8|8 split lands exactly on the seam, so neither option crosses the bezel.

**sel-view** = what the **B-row** currently represents. Set by the *selection-mode cluster* (§3). Values: `TRACKS` (direct track select — the default), `PHRASE` **[fork]** (snapshot waypoints), `STEP`, `PAR`, `TRG`, `INSTR`, `MUTE`, etc. Code: `seq_ui_sel_view`. This is what lets the B-row do "two things at once" — e.g. pick a destination track on the B-row while the GP row edits values.

---

## 3. Selection-mode cluster (left of the datawheel)

These set **the B-row's sel-view** (what row C represents) — they pick a *dimension*, they don't navigate pages.

| Glossary term | Panel | Firmware | Notes |
|---|---|---|---|
| **TRACK** | "Track" | `BUTTON_TRACK_SEL` | select-row → track select. |
| **PARAM** | "Param." (Layer) | `BUTTON_PAR_LAYER_SEL` | parameter-layer select. |
| **TRIGGER** | "Trigger" (Layer) | `BUTTON_TRG_LAYER_SEL` | trigger-layer select. |
| **INSTR** | "Instr." | `BUTTON_INS_SEL` | drum-instrument select. |
| **STEP** | "Step" | `BUTTON_STEP_VIEW` | step-view select. |
| **BOOKMARK** | "Bookm." | `BUTTON_BOOKMARK` | **[fork]** tap = **CHECKPOINT**, hold ≥1s = **REVERT** (bless/restore all 4 groups). |
| **MUTE** | "Mute" | `BUTTON_MUTE` | mute view. |
| **PHRASE** | "Phrase" | `BUTTON_PHRASE` | **[fork]** snapshot-library waypoints; also still navigates to the song-arrangement page. |

---

## 4. Mode / state buttons (row above transport)

| Glossary term | Panel | Firmware | Notes |
|---|---|---|---|
| **SOLO** | "Solo" | `BUTTON_SOLO` | |
| **FREEZE** | "Metr." (Metronome) | `BUTTON_METRONOME` | **[fork]** repurposed to the **generator-mutation master switch** (`seq_core_state.FREEZE`): engaged generator loops hold. Stock metronome function is gone. |
| **LOOP** | "Loop" | `BUTTON_LOOP` | Fx-loop toggle. |
| **RECORD** | "Rec." | `BUTTON_RECORD` | live/step record arm. |
| **LIVE** | "Live" | `BUTTON_LIVE` | jam/live page. |

---

## 5. Transport (bottom-left)

| Glossary term | Panel | Firmware |
|---|---|---|
| **REW / FWD** | ◀◀ / ▶▶ | `BUTTON_REW` / `BUTTON_FWD` |
| **STOP / PLAY / PAUSE** | ■ / ▶ / ❙❙ | `BUTTON_STOP` / `BUTTON_PLAY` / `BUTTON_PAUSE` |

---

## 6. Function buttons (bottom row, left of the midiphy logo)

| Glossary term | Panel | Firmware | Wiring |
|---|---|---|---|
| **COPY / PASTE / CLEAR** | Copy / Paste / Clear | `BUTTON_COPY/PASTE/CLEAR` | mapped |
| **MOVE / SCROLL** | Move / Scroll | `BUTTON_MOVE/SCROLL` | mapped |
| **FAST** | Fast | `BUTTON_FAST` | `M8A 5` — encoder fast-mode |
| **ALL** | All | `BUTTON_ALL` | `M8A 6` — edit-all-steps |
| **SELECT** | **"Shift"** | `BUTTON_SELECT` | `M8A 7` — the "Shift" cap is a **relabeled SELECT**. This is our reliable "cycle the selected menu item / confirm" key (LEFT/RIGHT are dead — §7). |

---

## 7. Navigation / page buttons (bottom row, right of the logo)

| Glossary term | Panel | Firmware | Wiring | Notes |
|---|---|---|---|---|
| **`<` / `>`** (= UP/DOWN) | `<` / `>` | `SEQ_UI_BUTTON_Up/Down` | `M4B 1` / `M4B 0` | **The `<`/`>` keys are wired to firmware UP/DOWN, which act as datawheel ∓1** — i.e. `<`/`>` just **nudge the selected value down/up**, they do NOT cycle menu items. There are no separately-labelled Up/Down keys. |
| **LEFT / RIGHT** | — (none) | `SEQ_UI_BUTTON_Left/Right` | **`0 0` — DEAD** | The firmware Left/Right cursor is **unwired** on this panel. **Menu item-cycling must NOT depend on Left/Right** — use SELECT (the "Shift" key) to cycle, and `<`/`>`/datawheel/encoders to set values. |
| **EXIT** | Exit | `BUTTON_EXIT` | mapped | leave page / up one level. |
| **MENU** | Menu | `BUTTON_MENU` | mapped | page shortcuts (MENU+GP). |
| **EDIT** | Edit | `BUTTON_EDIT` | mapped | back to the main Edit page. |
| **PATTERN** | Pattern | `BUTTON_PATTERN` | mapped | Pattern page. **[fork]** also: hold = legacy capture-to-slot (being removed). |
| **CAPTURE** | "Song" | `BUTTON_SONG` | mapped | **[fork, 2026-06-27]** repurposed → opens the **unified Capture page**. The song-arrangement page moved to **PHRASE**. |
| **UTILITY** | Utility | `BUTTON_UTILITY` | mapped | Utility page. **[fork]** also: hold = legacy retroactive-CAPTURE ring (being removed). |

---

## 8. Quick firmware ↔ glossary cheat-sheet (the ones we'll say most)

- **GP row** = row B = top 16 squares = `BUTTON_GP1..16`. The value-picker.
- **B-row** = row C = bottom 16 squares = `DIRECT_TRACK1..16`. The context row (sel-view); default = track picker.
- **GP-enc** = the 16 knobs (row A); pushes default to FAST, repurposable.
- **datawheel** = the big wheel.
- **SELECT** = the key labelled **"Shift"** (`BUTTON_SELECT`, `M8A 7`) — our reliable "cycle the selected menu item / confirm" key, since LEFT/RIGHT are dead.
- **`<` / `>`** = datawheel ∓1 (value nudge), NOT item-cycling.
- **CAPTURE button** = the **Song** key (fork-repurposed).

---

## 9. Resolved (2026-06-27) + standing notes

Confirmed with the user:
1. **`<` / `>`** = firmware **UP/DOWN** = **datawheel ∓1** (value nudge). They do **not** cycle items.
2. **No physical Up/Down keys** — the `<`/`>` keys *are* the UP/DOWN functions.
3. **"Shift" = SELECT** (relabeled cap, `BUTTON_SELECT M8A 7`).
4. **GP = General Purpose.** Row naming blessed: **GP row** (B) / **B-row** (C); Song key = **CAPTURE button**.

Consequences for menu UI (apply to the Capture page re-tune):
- **Item navigation:** cycle with **SELECT** ("Shift"); set the selected item's value with **`<`/`>`** (= datawheel ±1), the **datawheel**, or the **GP row**. Never rely on LEFT/RIGHT.
- **Use both rows at once:** put **track/destination picking on the B-row** (its native job) so the **GP row** stays free as the value-picker — that's the "two things at once" the B-row exists for.
- **GP-encoder pushes** (default FAST, no LED) are an unclaimed input we *could* repurpose per page — wiring/feasibility TBD before relying on them.
- Respect **the seam rule** (§1) in every layout.

### Capture page control map (shipped 2026-06-28)

The unified Capture page (SONG button) lands the above conventions, and is the first page to repurpose a **GP encoder**:
- **datawheel** = the GRAB dial (`Save` detent → `1b…Kb`); **`<`/`>`** nudge it (∓1).
- **B-row (select row)** = destination track (`SEQ_UI_CAPTURE_SetDstTrack`; select-row LEDs light the dst).
- **GP row** = destination pattern — GP1–8 = letter A–H, GP9–16 = number 1–8, **number press = COMMIT**.
- **GP1 encoder** = **`Fit:FILL ⇄ LOOP`** (CW → LOOP, CCW → FILL; readout on LCD-left) — confirms GP-encoders are usable per-page.
