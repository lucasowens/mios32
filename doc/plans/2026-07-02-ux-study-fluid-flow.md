# UX Study — Shaping a Fluid Flow for the Fusion Instrument

**Date:** 2026-07-02 · **Status:** study / proposal (provisional, uncommitted to code)
**Scope:** the *whole* midiphy SEQ V4+ panel as wired by `hwcfg/lso/MBSEQ_HW.V4`, seen
through the fusion loop — not a feature.

Companion documents:
- `apps/.../doc/MBSEQV4_CONTROL_SURFACE_MAP.md` — owns the **grammar** (what each control
  *does* today). This study builds on it and owns the **flow** (how they should compose).
- `doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md` — owns the **model** (the 4 homes, §2 discipline).

> Method: inventory the surface → map it onto the loop → catalog the friction (incl. the
> half-realized midiphy UI) → state principles → propose a shaped flow → sequence the rework
> into playable bundles with GO/NO-GO gates. Facts are grounded in source; the shaped flow in
> §4 is a **strawman to react to**, not a decision.

---

## 1. The instrument, seen as one loop

The north-star (memory: *fusion instrument*) is a single **get → process → tweak → harvest →
return** loop, and the platform model (memory: *capture-centric architecture*) gives that loop
four homes: **MATERIAL** (the notes/steps), **MOTION** (the modulation web / generative heart),
**CAPTURE** (harvest — *the* thing), **LIBRARY** (phrases/patterns).

Every tool we've built serves a stage. The problem is not the tools — it's that each one is a
**destination** (a page, a sel-view, or a modifier chord), so running the loop means constantly
re-contexting. Mapped out:

| Loop stage | Home | Tools today | Where it lives | The seam |
|---|---|---|---|---|
| **GET** | MATERIAL | B-row keyboard/pads; live RECORD; generators | INS sel-view (+ INSTR dance); RECORD arm | reaching the instrument is a ceremony (§2.1) |
| **PROCESS** | MOTION | GRAVITY, ROBOLOOP, pitch-gen, echo/robotize… | separate **pages** | you leave the playing context to reach them |
| **TWEAK** | MOTION | processor GP-encoders; morph datawheel ride | on the processor's page | shaping is a different room from playing |
| **HARVEST** | CAPTURE | UTILITY-grab, PATTERN-to-slot, Capture page, PHRASE-hold | hold-gestures that **repurpose the B-row** | harvesting drops the instrument (§2.2) |
| **RETURN** | LIBRARY | UNDO/REDO, CHECKPOINT/REVERT, FREEZE, phrase recall | SELECT-combos + sel-views | the safety net is a chord grammar to remember |

The instrument is a set of well-built **rooms**. A live instrument wants to be **one room you
stay in** while the material flows through the loop.

---

## 2. Friction catalog (grounded)

### 2.1 The four structural seams

1. **The B-row is time-shared by sel-view.** The 16 selection keys mean track-select /
   keyboard-or-pads / phrase-waypoints / capture-dst / pull-source / par / trg / step / mute /
   bookmark — set by whichever of the 8 sel-view buttons you last touched (`seq_ui_sel_view`).
   The default is TRACKS. So *play → recall a phrase → grab what you played* is three context
   switches **on the same keys**. → the **split-view** (§4.2) is the direct fix: two states at once.

2. **Play and harvest don't compose.** You play on the B-row in INS sel-view, but
   `capture_util_held` / `PATTERN_PRESSED` intercept the B-row **before** the sel-view switch
   (`SEQ_UI_Button_DirectTrack`), turning it into a destination-track picker. The instant you
   reach to keep what you're playing, the keys stop being the keyboard.

3. **Tweak lives on other pages.** MOTION (the generative heart) is a stack of pages
   (GRAVITY / ROBOLOOP / FX / pitch-gen). "Play it → hear it → shape it" is three rooms, and the
   dials aren't reachable from where you're playing.

4. **Modes are scattered with no shared readout.** FREEZE=METRONOME, morph-armed=PHRASE-flash,
   record=RECORD, play-vs-select=INSTR-toggle, FROZEN, slave-mute… each lives on a different key
   with its own LED idiom (some just unified this session). Nowhere shows the instrument's
   *whole* state at a glance.

### 2.2 Half-realized midiphy UI (the "warts")

- **INSTR bounces the page.** On lso, `BUTTON_BEH_INS_SEL 0` + `simplified_antilog_frontpanel 0`
  make INSTR *momentary for the page*: a tap latches `sel_view=INS` but the page flips to INSSEL
  on press and **back to the previous page on release** (`SEQ_UI_Button_InsSel`). Net: the
  INSSEL page — and the new keyboard note-name LCD — are only visible **while you hold INSTR**.
  The sel-view model wanted the page to *stay put* and the B-row to become the instrument. Half
  realized.
- **The datawheel is the only continuous dial, and it's overloaded.** Morph ride (PHRASE),
  switch-quantize (PHRASE), keyboard scroll (INS), value edit (every page), Capture GRAB. One
  wheel, N meanings by context.
- **A whole row of latent buttons: the 16 GP-encoder pushes.** They only *emulate a GP-button
  press* (`ENC_BTN_FWD`); exactly one per-page override exists (Capture GP1 = FILL⇄LOOP). 15+
  momentary buttons with no dedicated job.
- **Underused: `‹ / ›`** (value-nudge only), **datawheel-push** (sparse), **SOLO / LOOP**
  (single-purpose). **LEFT / RIGHT** firmware handlers exist but are **physically unwired** (dead).
- **The TPD is not a musical display.** The 8×8 has 10 modes but all are position / meter / logo
  / BPM (`seq_tpd_mode_t`). It shows *where the playhead is*, never *what the instrument is doing*
  (scale/root, mode, capture depth, morph position, record-armed).
- **Dedicated LEDs with no signal.** FOLLOW, SCRUB, MIXER, the per-track config pages, UNDO,
  JAM, TAP_TEMPO, EXT_RESTART are wired `0 0` on lso — their functions have no feedback; the
  panel's feedback is concentrated in the GP/B-row LEDs + a handful of button LEDs.
- **SELECT is saturated.** The master modifier gates UNDO/REDO, CHECKPOINT/REVERT, morph-arm,
  keyboard octave, layer-context copy/paste. New live verbs can't safely chord onto it.
- **Tempo is buried.** The single most fundamental live parameter has **no one-touch path**. On
  lso `BUTTON_TEMPO_PRESET` and `BUTTON_TAP_TEMPO` are both dead (`0 0`); the only route to the
  BPM page is **MENU (momentary) + GP13** (`ui_shortcut_menu_pages[12] = SEQ_UI_PAGE_BPM`) — a
  two-handed chord onto a GP position you have to *remember*. [your report, confirmed.] For a
  live techno instrument, tempo should be a glance-and-grab, not a memorized menu chord.
- **The EXIT page-list is in accretion order.** EXIT → `SEQ_UI_PAGE_MENU`, whose left-LCD list
  walks `ui_menu_pages[]` **in raw enum order** from EDIT onward
  (`SEQ_UI_FIRST_MENU_SELECTION_PAGE + offset + item`, `seq_ui_menu.c`). That order is *historical
  accretion*, not task grouping — track-config, FX, global, and system pages interleave, and the
  fork's own pages (Pitch, Gravity, Capture) are tacked on at the very end. Finding anything is a
  scroll-and-hunt. [your report, confirmed.] The enum order itself **can't** move (`old_bm_index`
  is frozen for bookmark-file compat), so the fix is a *display-order* indirection (§4.10).
- **ALL's config is a menu dive.** ALL toggles `CHANGE_ALL_STEPS` (live edit-scope), but its
  *behavior* (ramp vs. same-value = `ALL_RELATIVE`) lives as one toggle on the OPTIONS page
  (`seq_ui_opt.c`, `ITEM_ALL_RELATIVE`), reached only by EXIT → scroll to Opt → scroll to the
  item. No fast path from the button whose behavior it governs. [your report.]
- **ChordMask is a processor wedged into a mode-flags page.** The Track Mode page is a *playmode
  radio* (off/Normal/Transpose/Arp/**ChordMask**) plus a flag row (Bus/Note/Hold/Sort/ReSt/STrg/
  FTS/Sustain). ChordMask isn't a flag — it's a render-stack **processor** with four params
  (`chordmask_strength`, `chordmask_bus`, `chordmask_drum_l/h`, `seq_cc.c`). Only **strength** is
  exposed, via an **overlay hack** that repaints the ChordMask mode-tile to show `Msk:NNN` (the
  in-code comment: *"repurpose the ChordMask tile area … losing the > < highlight but gaining a
  visible live value"*). The **source bus** (which bus feeds the chord — arguably *the* key
  param) and the **instrument mask** have **no UI at all**; they're reachable only via CC. And
  ChordMask snaps notes to a **12-bit pitch-class set** (`chord_mask_snap`, `seq_core.c`) — a
  genuinely *visual, playable* object that's currently invisible. [your report — "needs love."]
  This is the same shape as the self-bus-legibility item: a **new fork processor bolted onto an
  old page** with no legible/performable home. → §4.11.

### 2.3 The gesture grammar — and where it fights itself

Six primitives generate every gesture (control-surface-map §2): **hold-latch modifier · tap ·
hold ≥1s · two-button (SELECT+X) · sel-view · hold-then-paint**. The friction isn't the count —
it's that some buttons carry a *hold-meaning and a tap-meaning on the same edge*, so using them as
a modifier **leaks into state**:

- **SELECT is a modifier *and* a stateful toggle.** `SEQ_UI_Button_Select` always forwards to the
  page callback, and on the EDIT page that press toggles **MIDI-learn / EDIT-RECORDING**
  (`midi_learn_mode`, `seq_ui_edit.c`). So *every* SELECT-as-modifier move — SELECT+CLEAR (undo),
  SELECT+waypoint (morph-arm), SELECT+key (octave) — also flips edit-recording, which you then
  have to clear. [your report, confirmed in source.] Modifier and toggle share one button + one
  edge, with no tap-vs-hold disambiguation.
- SELECT is also **saturated** (§2.2), so we can't relieve this by moving the toggle elsewhere
  *on* SELECT — it needs a **different gesture** or a **different home**.
- **The grammar assumes two hands.** Every modifier idiom — MENU+GP (page jump; MENU is
  *momentary* on lso, `BUTTON_BEH_MENU 0`), SELECT+X, hold-then-paint — needs one hand holding
  while the other aims. Mid-set the free hand is on a synth or the mixer, so these verbs are
  effectively unavailable while playing; the box is close to unusable one-handed. [your report.]
  Only tap and sel-view have one-hand paths today. → §4.8 (sticky modifiers).
- General shape of the wart: **no clean tap-vs-modifier split**, and **no one-hand path to the
  modifier layer**. A primitive that separates "deliberate one-key action" from "hold-to-modify"
  retires both classes at once. → §4.8.

### 2.4 The select-mode module — the panel's real organizing idea

Step back and the whole left/center cluster is *one mechanism*: a bank of **select-mode buttons**,
each **momentary** (`BUTTON_BEH_*_SEL 0`), each latches a `seq_ui_sel_view` via
`TAKE_OVER_SEL_VIEW` while pressed, and each **reskins the B-row** (`SEQ_UI_Button_DirectTrack`
→ the `switch(seq_ui_sel_view)` in `seq_ui.c`). That's genuinely consistent — *hold a mode, the
bottom row becomes that thing*:

| Select-mode button | `sel_view` | B-row (bottom row) becomes | lso pin |
|---|---|---|---|
| TRACK_SEL | TRACKS *(default)* | track select (radio / multi-toggle) | `M1C 2` |
| PAR_LAYER_SEL | PAR | parameter-layer select | `M1C 3` |
| TRG_LAYER_SEL | TRG | trigger-layer select | `M7C 3` |
| INS_SEL | INS | **play surface** (keyboard / drum pads) or instrument select | `M7C 2` |
| MUTE | MUTE | per-track (or per-layer) mute toggles | `M7C 1` |
| PHRASE | PHRASE | phrase waypoints (tap=recall / hold=capture) | `M7C 0` |
| STEP_VIEW | STEPS | step select | `M1C 1` |
| BOOKMARK | BOOKMARKS | bookmark slots | `M1C 0` |

**This is the native version of the split-view (§4.2).** The panel already thinks "a mode bank
reskins the bottom row"; split-view is just *lifting the single latch into a two-half latch* so
two of these coexist on 8|8. Framed that way, split-view isn't a new concept bolted on — it's the
existing mechanism, finished. That reframes §4.2's build as "generalize `seq_ui_sel_view` to a
`{left, right}` pair," not "invent a splitter."

Three warts in the module as it stands:

1. **It's a *held* modifier → two-handed** (the §2.3 one-hand problem, in its highest-traffic
   form: choosing what the bottom row does is the most frequent move on the box). Sticky /
   double-click latching (§4.8) or a persistent split (§4.2) is the fix.
2. **The naming is half a taxonomy.** Some are `*_SEL` (TRACK/PAR/TRG/INS), some are bare verbs
   (MUTE, PHRASE, BOOKMARK, STEP_VIEW), and **SONG isn't a sel-view at all** — so which buttons
   reskin the row and which jump to a page is not learnable from the labels.
3. **SONG and PHRASE overlap confusingly** — next item.

**SONG vs PHRASE (your "essentially the same button").** Confirmed, and the fork already split
them — but *crosswise*:
- **SONG** (`M8B 6`) → **toggles the Capture page** (fork repurpose; `SEQ_UI_Button_Song`
  remembers the page you came from and returns). It is **not** a sel-view.
- **PHRASE** (`M7C 0`) → latches **`sel_view = PHRASE`** (B-row = phrase waypoints) **and**
  navigates to the **song-arrangement page** (`SEQ_UI_PAGE_SONG`).
- Historically both just went to `SEQ_UI_PAGE_SONG` (the in-code comment says so). Now the labels
  are inverted against the destinations: **PHRASE takes you to the *song* page; SONG takes you to
  the *capture* page.** Two adjacent-in-meaning buttons, each pointing at the other's name.

The three homes they touch — CAPTURE (the grab), LIBRARY (phrases), song-arrangement — are one
family (design doc's capture-centric model). So the reconciliation isn't "delete one," it's "make
the two buttons name what they do." Options in §4.9.

---

## 3. Principles for the rework

Carried from design doc §2 and this study:

1. **Stay in the room.** One *performance stance* runs the whole loop without a sel-view/page
   switch for the common moves.
2. **Harvest never drops the instrument.** The "keep that" verb must not touch the row you're
   playing on.
3. **If it's used mid-performance, it's one-touch — not a chord.** Chords are for the rare/
   destructive (the deliberate-two-button idiom stays for those).
4. **The panel always tells you the instrument's state.** Mode, scale/root, armed, depth —
   glanceable, so you never wonder "what will this key do."
5. **Spend latent real estate before inventing chords.** 15 encoder pushes, `‹/›`, datawheel-
   push, SOLO/LOOP, the TPD, dead LEDs — claim these before overloading SELECT or the B-row.
6. **Constraints are materials, not guardrails** (§2). Every new dial sweeps 0→max with a true
   pass-through.
7. **One hand must be enough.** The other hand is on a synth or the mixer mid-set. Every frequent
   verb needs a one-hand path; hold-chords stay legal but must never be the *only* path. Where a
   modifier layer is unavoidable, a **one-shot sticky** (double-click the modifier → latched for
   exactly the next press) is the escape hatch (§4.8).
8. **One grammar, not N bespoke moves** *(added 2026-07-03).* Every processor is selected the same
   way, operated the same way, and read the same way. A movement learned on one transfers to all —
   including ones not built yet. New capability rides *inside* the grammar as data, never alongside
   it as another one-off page. This is the principle that makes growth stop costing usability
   (§3.5).

---

## 3.5 The operating model — one grammar, many processors

*The turn (2026-07-03): this study stops being a wart-list and becomes a design language.* The §2
warts aren't N independent bugs — they're N symptoms of **one** cause: the instrument has no
operating grammar. Every processor was added bespoke — ChordMask as a mode-tile overlay; Echo /
LFO / Robotize / Limit as separate FX pages; GRAVITY as its own workbench; Euclid / Turing /
pitch-gen as generator pages; the self-bus as a par-type. Each invented its own way to be
selected, operated, and read. That's *why* "almost every movement feels bespoke" — it literally is.

**The license (your call).** Up to **100%** of this UI can change. Decades of accretion from a
thousand hands is exactly what we're allowed to melt down and re-cast as yours. POC discipline
(design §2) already says throwaway is the right answer when it proves the instrument — now that
extends to the whole interaction layer, not just the DSP.

**The resolution to "extensible *and* usable": make the grammar the constant and processors the
data.** Five invariants every processor obeys, so learning one teaches all — present and future:

1. **One rack.** Every note/timing transform is a processor in the track's render stack — which is
   already the internal truth (design §3). The UI shows *that* rack, not a scatter of pages. A
   track = *a source + an ordered rack of processors.*
2. **One selector.** A new **`PROC` sel-view** in the select-mode module (§2.4): the B-row becomes
   the track's rack — each key a processor slot, lit = active, bright = focused. Tap = focus,
   double-tap = enable/bypass. Picking a processor is the *same move* whatever it is.
3. **One operating surface.** The focused processor's parameters map to the **GP encoder bank** —
   always the same 16 encoders, always in the processor's declared order, always pass-through at
   0 / center (principle 6); encoder-push = snap-to-default (§4.8). Operating any processor is the
   identical movement.
4. **One readout.** Depth/activity on the processor's rack-key LED (bright = doing something,
   dark = true pass-through); headline value on the TPD dashboard (§4.7); and where a processor
   has a natural 16-object shape (ChordMask's 12 pitch classes, a step mask, an LFO curve) the
   **GP row paints it**. State is never CC-only or invisible again.
5. **One growth path.** A new processor is a **descriptor**: name, ordered param list (each with
   range · formatter · default), optional 16-key surface, optional feedback hook. It plugs into
   2–4 automatically — shows up in the rack, its params land on the encoders, its values reuse the
   shared `SEQ_LCD_Print*` formatters. Adding a processor is *filling in a template*, not wiring a
   page.

**Fully exploit the midiphy elements — give each one *one* job across every processor:**

| Element | Its single job in the grammar |
|---|---|
| **B-row** + 2-colour LEDs | select / focus / bypass the rack (and, split, the live play surface) |
| **GP encoders ×16** + pushes | operate the focused processor (push = snap-to-default) |
| **GP row** + 2-colour LEDs | paint the focused processor's 16-object shape (mask / curve / steps) |
| **TPD 8×8** | the dashboard — headline state of the whole instrument at a glance |
| **datawheel** | the fine ride on the single focused value |
| **`‹/›`** | page the encoder **banks** (more params / config — plane model below) |
| **select-mode module** | the top-level switch (what the B-row *is* right now) |

**The usability guarantee.** Invariants 2–4 are *fixed*. Muscle memory transfers to every
processor, forever. Extensibility lives entirely in invariant 5 — *inside* the constant grammar,
never as another bespoke page beside it. That is the mechanism by which growth stops costing
playability.

**Depth without re-fragmenting — the plane model (holistic, 2026-07-03).** A processor may need
more than one bank of 16 encoders, or want a bespoke expressive surface. The grammar absorbs both
*without letting bespoke-ness back in* by giving every processor the same small set of **planes**,
reached by a **uniform** gesture — even when a plane's *content* is custom:

- **OPERATE** *(default, always present).* Encoder bank = the processor's core params; GP row
  paints its shape (invariants 3 + 4). The plane you land on and the one muscle memory owns.
  **Every processor is fully usable from here — non-negotiable.**
- **CONFIG / more banks** *(optional).* Params beyond the first 16, or rarely-touched setup /
  routing. Same encoder-bank mechanism, just paged with **`‹/›`** ("more params this way") — a
  currently-underused pair, and semantically exact. This is your "second row for config."
- **CUSTOM** *(optional, per-processor).* The fun bespoke surface — ChordMask's playable
  pitch-class keyboard, an LFO curve drawn on the GP row, a Euclidean necklace. Declared in the
  descriptor; absent for simple processors. Entered by a **uniform toggle** (candidate: re-tap the
  focused processor's PROC key, or a dedicated VIEW toggle) so *how you get in and out is the same
  everywhere* even though what's inside is unique.

**The guard rail (the whole point).** CUSTOM is an *extra on top of* OPERATE, never a replacement.
A processor is always fully operable from the uniform plane; the custom surface is a bonus, entered
deliberately, exited the same way. If a processor can *only* be driven from its custom UI, we've
regressed to exactly the bespoke sprawl the grammar exists to kill. Custom surfaces are where
fragmentation would sneak back — this rule is the fence.

**Split-view synergy (your "right half" instinct).** The CUSTOM surface can live on the *right
half* while the left half stays the rack / standard controls (§4.2) — play the uniform controls
and the bespoke surface at once. A degree of freedom to explore, not a v1 requirement.

This extends invariant 5's descriptor: **core params + optional extra banks + an optional
custom-UI hook** (a render + input callback). Simple processor = params only; rich one = params +
a surface. The *toggle and paging are uniform*; only the CUSTOM plane's content is bespoke — and it
can never be the sole way in.

**The real payoff of consistency — meta-operations over the rack (design-ahead, 2026-07-03).**
Once every processor is a *uniform vector of 0..127 dials with pass-through at 0*, the whole rack's
state is a **fixed-shape vector of numbers**. Homogeneity makes the rack something you can do math
on — impossible when each processor is a bespoke page:
- **Rack morph / scenes** — store two rack states (A, B) and sweep between them on one control;
  every processor moves together. This is Elektron scenes + the performance-macro crossfader, done
  as a *continuous morph*. It **rides the morph engine that already exists** (`SEQ_MORPH_*`, the
  TRKMORPH page, phrase-morph, tension-morph) — point it at the rack vector; don't rebuild it.
- **Pass-through-at-0 makes morph musical** — morph an all-zero (bypassed) rack → a configured one
  = the whole processing chain fading in under one hand. Constraints-as-materials at rack scale.
- **Uniform modulation targets** — every param is addressable the same way (`SEQ_CC_Set` indices),
  so the self-bus / an LFO can reach *any* processor param generically — MOTION wiring into the
  rack with no per-processor glue.

This *raises the value of the consistency bet*: uniformity isn't only for muscle memory, it's the
substrate for morph/scenes/modulation. **Design toward it, don't build it into G0** — the only
thing G0 must respect is keeping the param model a clean addressable vector (it already is), so
these stay a later bolt-on, not a rewrite.

**This is assembly, not fantasy — every piece already half-exists:** the render-stack rack
(design §3); the select-mode module that already reskins the B-row (§2.4); 15 latent GP-encoder
pushes + the encoder row (§2.2); the two-colour GP and B-row LED planes (§1.8); the shared
`SEQ_LCD_Print*` formatters (surfaced during the self-bus work); the TPD dashboard (§4.7). The
work is *unifying* them under one grammar and migrating each processor onto it — not inventing new
hardware affordances.

**Prove it POC-style (don't build the framework speculatively).** Build the thinnest vertical
slice — the grammar operating **one real processor** end-to-end (rack-select it, operate it on the
encoders, read it on the GP row + TPD) — put hands on it, and only roll it out to the rest once it
*feels* better by hand and ear. ChordMask (§4.11) is the natural first tenant: it most needs a
home, and its 12-pitch-class mask exercises invariant 4's "paint the shape" the hardest.

---

## 4. The shaped flow (strawman)

The core proposal: define a **performance stance** — a stable assignment of the two 16-key rows
+ the wheel + a few claimed buttons — that lets one hand run get→harvest and the other shape,
without re-contexting. Then fix the warts that break it.

### 4.1 The stance (live default)

| Surface | Live meaning | Rationale |
|---|---|---|
| **B-row** | **the instrument, split** — e.g. left 8 = keyboard/pads, right 8 = phrase recall (§4.2) | play with one hand, recall/mutate with the other — literally two things at once |
| **GP row** | **the material** — the focused track's steps (see/toggle what you play & capture) | play and edit the same thing without flipping |
| **datawheel** | **the ride** — the one continuous gesture in context (scroll / morph) | keep it as the expressive dial |
| **1 claimed button** | **HARVEST** — "keep what just sounded → the focused track" | one-touch grab off the B-row (§4.4) |
| **GP-encoder pushes** | **per-stance verbs** (e.g., record-arm, freeze, recall) | reclaim the latent row |

The stance is entered once and *held* — the loop runs inside it.

### 4.2 The B-row — its states, and **split-view** (do two things at once)

The B-row (the fork's "second GP-button row" — `BUTTON_DIRECT_TRACK`) is the panel's *"two things
at once"* surface: 16 selection keys whose meaning follows `seq_ui_sel_view`. Today it holds
exactly **one** state at a time. The high-leverage move is to let it hold **two** — **split** the
row so each half is independently assigned. This directly collapses seam #1 (§2.1).

> This is the **select-mode module (§2.4), finished** — the panel already reskins the B-row from a
> mode bank; split-view just generalizes the single `seq_ui_sel_view` latch into a `{left, right}`
> pair. Not a new concept — the existing one, completed.

**States of the B-row:**
- **Full** — all 16 keys = one sel-view (today): TRACKS / INS (keyboard·pads) / PHRASE (waypoints)
  / PAR / TRG / STEP / MUTE / BOOKMARK.
- **Split (8 | 8 on the seam)** — left 8 = sel-view **L**, right 8 = sel-view **R**, each with its
  own LEDs. The split lands on the col 39/40 seam, so the zones never straddle (seam rule).

**Why split is the unlock:** instead of *play → switch to phrases → recall → switch back*, you
keep the **left half as the keyboard** and the **right half as phrase waypoints** — play a line
with one hand, recall/morph sections with the other, **no mode flip**. "Stay in the room," made
literal on the row.

**Pairings to try by ear** (the split *presets*):

| Left 8 | Right 8 | The move it enables |
|---|---|---|
| keyboard (≈1 octave) | PHRASE waypoints | play a line; recall / morph sections live |
| keyboard | track **MUTES** | play over a mix you're muting/unmuting |
| keyboard | CAPTURE dst / harvest targets | play, and place what you grab |
| track MUTES | PHRASE waypoints | arrange (mute + recall) with no keys at all |
| track-select | STEP view | edit while watching the step window |

**Mechanics to design in the bundle:**
- *Assign a half:* strawman — hold a sel-view button while aiming at a half (or a modifier picks
  L/R); a bare sel-view press = whole row (back-compat). A "last split" is remembered so re-entry
  is instant.
- *LEDs:* compute `select_leds_green/red` per half (left bits from sel-view L, right bits from R)
  and merge — cheap; the periodic handler already builds these masks.
- *Key routing:* a press dispatches to the half's handler by key index (0–7 → L, 8–15 → R).
- *8-key keyboard is fine:* the isomorphic row is width-agnostic — 8 keys at Jump=2 span an octave;
  scroll still reaches the rest. (A split may want a per-half Jump/scroll.)

### 4.3 GET — pick-up-and-play

- **Fix the INSTR page-bounce.** Make INSTR latch the play-surface *and keep its display*
  (either stop the momentary `PageSet` when the sel-view latches, or make the keyboard readout a
  persistent overlay while INS is latched). Result: tap INSTR → you're playing, and you can *see*
  the keyboard (scale/root/base/note-names) without holding the button.
- **Fold record-arm into the stance** or put it on a claimed encoder-push, so "play *and keep it*"
  is not a separate reach for RECORD.

### 4.4 HARVEST — in place, never dropping the instrument

- Move the "keep" verb **off the B-row** onto a dedicated one-touch. Default destination = the
  **focused track** (the common case needs no destination picking, which is what forces the
  B-row hijack today).
- **Candidate trade-offs** (the §6.3 promise, delivered):

  | Candidate | For | Against |
  |---|---|---|
  | **Datawheel-push** | Global (not page-contextual), completely unused today, big and unmissable, semantically "commit what I hear"; composes with the morph ride — push mid-morph = harvest *as-heard* | Sits left of the LCDs, a reach from the B-row; needs a guard so a push during a morph ride can't mis-fire the wrong verb |
  | **GP-encoder push** (one fixed encoder) | Right above the play row, one-hand; 16 latent pushes to spend | Pushes are page-contextual — claiming one *globally* collides with future page uses; pushing can nudge the encoder value; double-push is already earmarked snap-to-default (§4.8 — single/double compose, but the key gets busy) |
  | **SOLO** (repurposed) | Dedicated, LED-backed, near the track row | Costs a real live function — solo *is* set bread-and-butter for a techno set; only viable if you genuinely never solo from the box |
  | **LOOP** (repurposed) | Dedicated, LED-backed; loop-range play is arguably a niche | Same cost question; LOOP is also a candidate live tool for the stance (loop-a-bar-and-mangle) |

  Strawman rec: **datawheel-push** — it's the only candidate that's global, free, and semantically
  right, and "push the big wheel to keep it" is a gesture you can hit without looking. SOLO/LOOP
  stay what they are (they're performance tools in their own right — spending them contradicts
  principle 5's *spend latent estate*, since they're not latent).
- The deliberate multi-key capture gestures (UTILITY/PATTERN "hold-then-paint") **stay** for the
  aimed / cross-slot cases — but they're no longer the *only* way to harvest, so the fast path
  doesn't drop the instrument.
- The deliberate multi-key capture gestures (UTILITY/PATTERN "hold-then-paint") **stay** for the
  aimed / cross-slot cases — but they're no longer the *only* way to harvest, so the fast path
  doesn't drop the instrument.

### 4.5 SHAPE — in place

- A **live-tweak layer**: the active processor's top N dials mirror onto a fixed set of GP
  encoders regardless of page, so you shape MOTION from inside the stance; or a one-touch
  flip-and-return so shaping costs a moment, not a room.
- (Design-ahead: this is where MOTION-as-overlay from the capture-centric model lands.)

### 4.6 RETURN — a legible safety net

- The unified UNDO/REDO + CHECKPOINT/REVERT net exists; make its **state visible** (this session
  added BOOKMARK-lights-under-SELECT when a checkpoint exists — extend that idea to the whole net).

### 4.7 STATE — the dashboard (spend the TPD + dead LEDs)

- A **TPD musical mode**: show the live state — scale/root, stance (play/select/frozen),
  record-armed, capture depth, morph position — so the instrument is glanceable. The 8×8 is
  currently pure position; a musical mode is the highest-value unused surface on the panel.
- **First concrete element (your idea): a mute dot.** On each track's TPD column, a **lit top dot
  = that track is muted** — you preferred the *top* dot (reads as a flag above the position trail)
  over a bottom dot. Cheapest possible glanceable mix state, and a good first musical element to
  layer onto the existing position display. *Detail to settle:* the 8×8 shows 8 columns — which 8
  of 16 tracks (current group's, or 8-of-16), and keeping the dot clear of the position meter rows.
- Give the scattered modes a consistent LED language (steady = here/holding, flash = armed-mode,
  row = live aim — the convention drafted in control-surface-map §1.8).

### 4.8 A new primitive — **double-click** (and the SELECT de-clunk)

A **double-click** (two quick taps on one key) is a *deliberate* action — accident-safe like the
two-button idiom, but it frees the second key. It's the missing primitive for separating "one-key
action" from "hold-to-modify," and it buys back SELECT.

Where it simplifies:
- **De-clunk SELECT (exhibit A).** Shed the edit-recording toggle off SELECT entirely — onto
  **RECORD double-click** (steered; below). A single SELECT press becomes *purely* the modifier,
  no state to clean up.
- **One-shot sticky modifiers — the one-hand fix.** Double-click a *modifier* (MENU, SELECT) →
  it latches for **exactly the next press**, then releases. MENU-MENU, GP-n = page jump with one
  hand; SELECT-SELECT, CLEAR = undo with one hand. Same deliberateness, half the hands. This is
  what makes principle 7 real without redesigning every combo. (Classic sticky-shift; the latch
  shows on the modifier's LED — flash while armed.)
- **Shrink SELECT+X saturation.** Combos that exist only to be *deliberate* (fat-finger-safe) can
  become **double-click-X on the one key** — same safety, SELECT freed (e.g. double-tap CLEAR =
  undo). Reserve the two-button idiom for the genuinely destructive.
- **Latch a momentary.** Double-click a momentary (INSTR) to *latch* it persistently vs. single =
  momentary — directly relevant to the INSTR page-bounce (§2.2). [confirmed useful — you arrived
  at the same idea independently.]
- **Snap-to-default.** Double-click an encoder-push snaps that dial to its **default value** in
  one gesture — for processor dials that *is* the pass-through (0 / center detent, the §2
  principle); for ordinary params (Jump, clock-div, CC offset…) it's the sensible home. One
  gesture, works on every dial, nothing to remember per-dial.

Cost: one tuned timing window (~250–350 ms); a double-click key's single-tap meaning must stay
**immediate** — the single fires on first press as today, and the second press *absorbs* it
(reverts the single's effect, then applies the double's meaning). Keys where even a revertible
blip is unacceptable — GP step keys, B-row play keys, PLAY — **never** get a window. An empty
double is fine; not every key needs one. Prior art in stock firmware: STOP already means more on
a second press ("already stopped → reset song position", `SEQ_UI_Button_Stop`).

**The SELECT fix — steered: edit-recording → RECORD double-click** (fuses re-home + double-click):

Edit-recording *is* a recording mode — the EDIT page already treats them as one family
(`seq_record_state.ENABLED || midi_learn_mode == ON` is OR'd throughout the LCD/step-bracket
code, `seq_ui_edit.c`). RECORD is real, LED-backed panel estate on lso (`BUTTON_RECORD M5C 1`,
`LED_RECORD M5C 1`) whose single press = record-arm toggle and nothing else — double-click is
open there.

- **Single RECORD** — unchanged, immediate: arm/disarm live recording (punch-in feel preserved).
- **Double-click RECORD** — second press inside the window reverts the arm-toggle and toggles
  **edit-recording** instead. Net arm state unchanged; RECORD LED flashes while edit-recording
  is on (today the mode is visible only on the EDIT LCD).
- **SELECT on EDIT** sheds its tap-action entirely → pure modifier, nothing to clean up. The
  *momentary* hold-a-step MIDI-learn (GP-hold path, self-cleaning on release) is untouched.
- The **clean-tap** idea (fire a page's SELECT tap-action only if no other key was pressed during
  the hold) is no longer needed for EDIT; keep it in the back pocket as generic infrastructure if
  another page's SELECT tap-action turns out to leak the same way.
- By-hand check at build time: the ~300 ms arm blip on the first press of a double-click could
  swallow a live note into the pattern while playing; the absorb path must clean that up
  (`SEQ_RECORD_Enable(0)` already handles hanging notes — verify by ear).

Adopt **double-click as a first-class primitive** (shared window + absorb helper in `seq_ui.c`,
one implementation, per-key opt-in). Candidate map — provisional, empty-is-fine:

| Key | Single (unchanged) | Double-click (candidate) |
|---|---|---|
| RECORD | arm/disarm recording | **edit-recording toggle** (steered) |
| MENU | hold = page-shortcut layer | **one-shot sticky** menu layer (one-hand page jump) |
| SELECT | hold = modifier | **one-shot sticky** select (one-hand SELECT+X) |
| INSTR | play-surface toggle / sel-view | **latch** the play surface persistently |
| any GP encoder *push* | (page-defined) | **snap dial to default** |
| CLEAR | (with SELECT: undo/redo) | undo — one-hand, fat-finger-safe |
| EXIT | back out one level | jump all the way home (EDIT) |
| ALL | toggle change-all-steps | **jump to ALL's config** (§4.10 — replaces the menu dive) |

### 4.9 The select-mode module — finish the taxonomy (SONG/PHRASE + tempo)

Two concrete fixes fall out of §2.4, both small and both about *making the mode bank say what it
does*.

**(a) SONG / PHRASE — name what they do.** They're one family (CAPTURE · LIBRARY ·
song-arrangement) split crosswise. Reconcile so the label predicts the destination. Options:

1. **Straighten the two** *(minimal, recommended)*: **PHRASE** = pure phrase sel-view (B-row =
   waypoints), drop its jump to the song-arrangement page. **SONG** = the CAPTURE/arrangement
   home (it already toggles the Capture page). Now PHRASE is a *mode* (reskins the row) and SONG
   is a *place* (a page) — the two categories §2.4 wart #2 conflates, cleanly separated. The
   song-arrangement page stays reachable (MENU-shortcut or from the Capture page).
2. **Fuse into one button + a toggle**: a single LIBRARY button; single = phrase sel-view,
   double-click = the CAPTURE/song page (uses the §4.8 primitive). Frees the other pin (`M8B 6`
   or `M7C 0`) for a live verb — a **HARVEST** button (§4.4) or **TEMPO** (below) wants a home.
3. **Leave as-is, just relabel** in the manual/overlay — cheapest, fixes nothing structural.

Option 1 is the clean-taxonomy move; option 2 is the same *plus* it hands us a free, well-placed
button, which is scarce on this panel — attractive if HARVEST or TEMPO needs a home.

**(b) Tempo — give it a one-hand path.** Fixing §2.2's buried BPM, in order of preference:

1. **Hold-a-button + datawheel = live BPM, from any page** — the morph-ride pattern exactly (a
   global encoder intercept, like PHRASE+datawheel rides morph). The datawheel is *the* tempo
   knob on every groovebox; this makes it so without leaving the stance. Best fit; needs a spare
   modifier — a freed SONG/PHRASE pin (option 2 above) or a **double-click TAP-role** button.
2. **Double-click a transport/tempo button → BPM page** (§4.8 primitive). One-hand, no chord, no
   remembered GP position. Cheaper than (1) but still takes you to a page rather than adjusting
   in place.
3. **Re-home a dead pin**: `BUTTON_TEMPO_PRESET` / `BUTTON_TAP_TEMPO` handlers exist but are
   unwired on lso — if there's a free panel key, wire one to tap-tempo or a hold+wheel. Hardware
   question, not just firmware.

Strawman rec: **(1)**, sourced from the button that **option-2 of (a) frees** — fuse SONG/PHRASE
into one LIBRARY button, spend the reclaimed pin as a **hold-for-tempo** (hold + datawheel = BPM,
double-click = tap). That single move finishes the SONG/PHRASE taxonomy *and* un-buries tempo with
one freed button. (If you'd rather keep SONG and PHRASE separate, tempo falls back to the
double-click path (b2), which needs no free pin.)

### 4.10 Navigation legibility — the EXIT list + ALL config-jump

Two small "find-it-fast" fixes; both are about *destination*, not gesture.

**(a) Reorder the EXIT page-list into task groups.** The enum can't move (`old_bm_index` frozen),
so add a **display-order array** of `seq_ui_page_t` and have the menu page iterate *that* instead
of `FIRST_MENU_SELECTION_PAGE + offset + item`. Group by what you're doing, in loop order:

1. **Play / transport** — Edit, Mute, Patterns, Song/Capture, Live, Jam, Manual
2. **Track shape** — Events, Instrument, Mode, Direction, Divider, Length, Transpose, Groove,
   Triggers
3. **MOTION / FX** — LFO, Echo, Humanize, Robotize, RoboLoop, Limit, Duplicate, Morph, Random,
   Generators, Pitch-gen, Gravity, Scale, Loop
4. **Capture / library** — Capture, Bookmarks, Mixer, Remix
5. **Global / system** — BPM, Options, Save, Disk, MIDI, MIDI-Mon, SysEx, CV, Eth, About

Optional polish: a one-char group tag or a blank divider between groups on the LCD list. Change is
localized to `seq_ui_menu.c` (the nav math + the name render loop) plus the one array; enum,
bookmarks, and MENU+GP shortcuts are untouched. *(Alternative: alphabetize — trivial, but groups
serve a performer better than A–Z.)* Exact ordering is a taste call (§6.9).

**(b) Double-click ALL → its config** (the §4.8 primitive). Single ALL unchanged (toggle
change-all-steps); double-click jumps straight to the ALL settings instead of the OPTIONS dive.
Caveat worth your steer: today "ALL's config" is essentially **one** toggle (`ALL_RELATIVE`,
ramp vs. same-value). If what you pictured by *"select which buttons are All'd"* is a richer
**scope selector** (which par-layers / which tracks ALL reaches), that's a *new* screen to design,
not just a jump — the jump is cheap, the scope-selector is the bigger build. Which did you mean?
(§6.10)

### 4.11 ChordMask — give the processor a real home

ChordMask is the fork's first "playmode that's actually a processor," and it currently borrows the
Track Mode page badly (§2.2). Two ways to fix, plus the interesting third dimension.

**Option A — context-morph the Track Mode page.** When `MODE == ChordMask`, the flag row
(Bus/Note/Hold/…) is largely irrelevant to a chord processor; repaint the right half to expose
**Strength · Source-bus · (instrument mask)** in proper labelled fields instead of the overlay
hack. One page, adapts to the selected mode. Cheapest that's *not* a kludge; keeps everything
where you already look. Downside: the page is already full, and it doesn't scale when the next
processor-mode arrives.

**Option B — ChordMask gets its own page** (recommended; the consistent move). Every other
processor has one — LFO, Echo, Robotize, RoboLoop, Limit, and the Tension Workbench (GRAVITY).
ChordMask is the odd one out. Track Mode keeps just the *radio* (pick ChordMask); the processor
page owns Strength/Bus/mask, and selecting the mode can deep-link to it (mirrors how the FX pages
work). Removes the overlay hack from Track Mode entirely and gives room to grow.

**The third dimension — make the pitch-class mask *visible and playable*.** ChordMask snaps to a
**12-bit pitch-class set**. Twelve of the sixteen GP keys = one chromatic octave: light the PCs
currently in the chord (read live from the source bus) so you can **see the harmonic filter your
notes are being pulled into** — and, if we make it hand-settable, **play** the mask (tap keys to
open/close pitch classes). That turns ChordMask from an opaque snap into a legible, performable
harmony surface — the same "MOTION heart made legible" theme as the self-bus decode (§design §10)
and the TPD musical dashboard (§4.7). Open question: is the mask **bus-derived** (display-only) or
**hand-set** (playable), or bus-derived-with-hand-override? (§6.11)

Recommendation: **Option B**, and build the 12-PC LED display first (read-only, from the bus) —
it's the highest-legibility, lowest-risk slice, and it tells you by eye whether the feature is
even doing what you think before we invest in a hand-set editor.

---

## 5. Rework roadmap (build less, listen sooner)

Each bundle is the **smallest playable loop** (§2.7 discipline) with a by-ear GO/NO-GO before the
next. Two tracks now run in the study: **the grammar** (§3.5 — the spine that kills the bespoke
feel) and **the point-fixes** (§4 — the warts). The point-fixes are worth shipping for immediate
relief, but each is really a *first instance* of the grammar; where they conflict, the grammar
wins the eventual shape.

**The spine — prove the operating model on one processor, then roll out:**
- **G0 — The grammar, one tenant.** The `PROC` sel-view (rack on the B-row) + the encoder bank as
  the operating surface + one readout path, wired to **exactly one** processor end-to-end —
  **ChordMask** (§4.11: most needs a home, hardest test of "paint the shape" via the 12-PC mask).
  *Proves by hand/ear:* selecting and operating a processor feels the same and better. **The
  GO/NO-GO for the whole grammar.**
- **G1 — Migrate the rack.** Bring the other processors (Echo, LFO, Robotize, RoboLoop, Limit,
  GRAVITY, generators, self-bus) onto the descriptor template. *Proves:* one learned movement now
  drives all of them; the FX/mode/generator page-scatter collapses.
- **G2 — The descriptor is the extension point.** Lock the processor descriptor so the *next* idea
  is a template fill, not a page. *Proves:* growth without new bespoke UI.

**The point-fixes — immediate fluidity, each a grammar instance:**
- **B1 — Pick-up-and-play + harvest-in-place (the minimal loop).** Fix INSTR page-bounce + keep
  the keyboard display; **HARVEST-in-place** one-touch to the focused track. *Playable loop:* pick
  up → play → keep → keep playing, without leaving.
- **B2 — The B-row split (two things at once).** 8|8 split-view with per-half sel-views + LEDs +
  routing + a "last split" memory; start with **keyboard | phrase-recall**. *Proves:* get + return
  without a mode flip. (This is the select-mode module finished — §2.4 — so it's also grammar
  groundwork: the rack `PROC` view is one more thing a split half can hold.)
- **B3 — The dashboard.** TPD musical mode (start with the **mute dot**) + consistent mode LEDs.
  *Proves:* state at a glance. (= invariant 4's readout path.)
- **B4 — Shape-in-place.** Live-tweak the active processor from inside the stance. (= invariant 3
  on the encoder bank; folds into G0/G1.)
- **B5 — Modifier consolidation.** SELECT de-clunk (steered), double-click primitive + sticky
  modifiers, EXIT page-list regroup, ALL config-jump. *Proves:* the gesture grammar is legible &
  one-touch.

**Sequencing question (§6.12):** lead with **G0** (prove the system on ChordMask — highest
long-term leverage, directly answers "everything feels bespoke") or with **B1** (smallest
immediate playable win)? They're compatible — B4/B2 are grammar groundwork — but which itch is
louder decides the first commit.

---

## 6. Decisions for you (taste — where I shouldn't guess)

1. **Stance default:** should the B-row default to a **split** (e.g. keyboard | phrase-recall)
   during live play (strawman), a full-row **instrument**, or stay TRACKS with a fast one-touch
   into play? (Affects everything downstream.)
2. **Split-assign gesture:** how do you set the two halves — *hold a sel-view button + aim at the
   half*, a dedicated *split modifier*, or fixed *presets* you cycle? And is 8|8 the only split, or
   do you want asymmetric (e.g. 12|4)?
3. **The HARVEST button:** which latent control becomes the one-touch "keep" — a **GP-encoder
   push**, **SOLO**, **LOOP**, or **datawheel-push**? (Each has trade-offs; I'll lay them out.)
4. **INSTR fix:** stop the page-bounce so INSTR *stays* on the keyboard view, or keep it momentary
   but make the keyboard readout a persistent overlay on your current page?
5. **SELECT de-clunk + double-click:** ~~pick the SELECT fix~~ **STEERED 2026-07-02:**
   edit-recording moves to **RECORD double-click** (single RECORD unchanged); SELECT becomes a
   pure modifier on EDIT; **double-click adopted as a first-class primitive**, including
   **one-shot sticky modifiers** for one-hand play (§4.8). Remaining sub-choices: timing-window
   feel (~250–350 ms), which candidate rows of the §4.8 map ship first, and whether sticky-SELECT
   arms *all* SELECT+X combos or a whitelist (destructive ones may deserve to stay two-handed).
6. **SONG / PHRASE reconciliation:** **straighten the two** (PHRASE = pure phrase mode, SONG =
   the CAPTURE/arrangement place — recommended), **fuse into one LIBRARY button** (frees a pin for
   HARVEST or TEMPO), or **relabel only**? (§4.9a)
7. **Tempo path:** **hold-a-button + datawheel = live BPM** (best, needs a freed pin), **double-
   click a transport button → BPM page** (no free pin needed), or **wire a dead pin to tap-tempo**
   (hardware)? (§4.9b) — couples to #6 if you want the freed SONG/PHRASE pin to host it.
8. *(superseded by #12 — the point-fix "first bundle" choice now sits under the grammar-vs-fix
   sequencing question.)*
9. **EXIT page-list order:** the grouped order in §4.10a (as drafted), a different grouping, or
   just alphabetize? (Taste — give me the buckets and I'll wire the display array.)
10. **ALL double-click target:** just a **fast jump** to the existing ALL option (ramp toggle), or
    a new **scope selector** ("which layers/tracks ALL reaches")? The jump is cheap; the selector
    is a real build (§4.10b).
11. **ChordMask home:** ~~context-morph vs own page~~ **RESOLVED by #12's grammar decision** —
    ChordMask is a **rack tenant** (PROC sel-view to focus it; params on the GP encoder bank; the
    12-PC mask painted on the GP button row). The old "own page" option is superseded — no bespoke
    page. *Still open (this is the interesting one):* is the mask **bus-derived/display-only**,
    **hand-set/playable**, or **bus-derived with hand override**? (§4.11)
12. **The big one — grammar first, or point-fix first?**
    **STEERED 2026-07-03 — the two load-bearing bets are ACCEPTED (by-ear enthusiasm GO):**
    *rack = a new `PROC` sel-view on the B-row* (invariant 2) and *operate = the GP encoder bank*
    (invariant 3). The five invariants (§3.5) are the spine. Physical model locked: **B-row picks
    the processor · the 16 GP encoders shape it · the GP button row + LEDs paint what it's doing.**
    *Still open:* the **sequencing** — lead with **G0** (grammar proven on ChordMask, the GO/NO-GO
    for the model) or slot a fast **point-fix** (B1/B2) first for immediate playable relief; and
    the **fallback** sub-question from bet (b): what the 16 encoders mean when *no* processor is
    focused (current-page meaning vs. the focused track's default dials).
    **PROC home DECIDED 2026-07-03: steal the LIVE button** (`M6C 1`, has its own LED; its
    `FWD_MIDI` function → permanent-on default). PROC is **latched** (tap-in/dwell/tap-out), unlike
    the held sel-views — first payoff of the sticky-sel-view idea. (G0 plan updated.)
13. **Planes & the toggle** *(raised 2026-07-03).* The plane model (§3.5: OPERATE / CONFIG-banks /
    CUSTOM) is adopted. Open sub-choices: the **CUSTOM toggle** gesture — *re-tap the focused
    processor's PROC key* (no new real estate) vs. a *dedicated VIEW button* (clearer, costs a
    pin); whether **`‹/›`** is the bank-pager (recommended); and whether the CUSTOM surface can
    take the **right split-half** while the left stays standard (§4.2 synergy — likely a later
    refinement, not v1). ChordMask's mask is the first CUSTOM plane, so G0 will force these.

> Running notes (your drive-by ideas, folded in as they land): **split-view B-row** (§4.2),
> **TPD mute-dot, top** (§4.7), **double-click primitive + SELECT de-clunk** (§2.3 / §4.8),
> **edit-recording → RECORD double-click** (§4.8, steered), **one-hand rule + sticky modifiers**
> (§3 principle 7 / §4.8), **encoder double-push = snap-to-default** (§4.8), **select-mode module
> = native split-view** (§2.4), **SONG/PHRASE reconciliation + buried-tempo fix** (§4.9),
> **self-bus legibility — decode Ctrl step → target units** (design doc §10, floated 2026-07-03),
> **EXIT page-list regroup + ALL config double-click** (§4.10), **ChordMask needs a home + a
> visible/playable 12-PC mask** (§4.11), **operating grammar + two load-bearing bets ACCEPTED**
> (§3.5 / §6.12), **plane model — OPERATE/CONFIG-banks/CUSTOM + uniform toggle** (§3.5 / §6.13).
> Keep them coming — I'll keep integrating and the study stays the shared canvas. Once you steer
> §6, I turn the chosen bundle into a build plan and we prove it by ear.
>
> *Rack-as-vector (design-ahead, 2026-07-03):* uniform processors = a fixed-shape param vector →
> **morph / scenes / modulation** over the whole rack ride the existing `SEQ_MORPH_*` engine
> (Elektron scenes as continuous morph). Raises the value of the consistency bet. Design toward it;
> keep the param model addressable, don't build it into G0. **G0 build plan written:**
> `doc/plans/2026-07-03-g0-processor-grammar-chordmask.md`.
>
> *Theme → spine (2026-07-03):* the "new processors bolted onto old pages" pattern was the tell
> that the instrument has **no operating grammar**. That observation is now promoted to **§3.5 —
> the operating model** (one rack · one selector · one operating surface · one readout · one growth
> path), under the clean-slate license (up to 100% of the UI can change). The individual §4 fixes
> are now understood as *first instances* of that grammar. *The MOTION heart made visible and
> playable* is the readout invariant; the grammar is the whole of it.
