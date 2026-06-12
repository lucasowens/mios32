# Rosetta — the new model, in Ableton terms

*Scaffolding doc (2026-06-12): a grokking aid, written against the questions
from Stage-C prep. Not design-canonical — the design doc owns the model.
Delete or fold once it has done its job.*

---

## 1. The three grids

### Ableton session view — what your hands already know

```
              T1      T2      T3      T4      T5    ...
  scene 1   [clip]  [clip]  [clip]  [clip]  [clip]
  scene 2   [clip]  [    ]  [clip]  [clip]  [    ]
  scene 3   [    ]  [clip]  [    ]  [clip]  [clip]

  • launch any SINGLE cell, any time, quantized        ← cell-grain
  • launch a row (scene)                               ← row-grain
  • THE GRID IS THE INSTRUMENT — performing = navigating it
```

### The SEQ as it shipped — the same grid, welded into 4-packs

```
            GROUP 1              GROUP 2              GROUP 3   ...
            T1  T2  T3  T4       T5  T6  T7  T8
  pat A1   [===============]    [===============]
  pat A2   [===============]    [===============]
  pat A3   [===============]    [===============]

  • the ONLY launchable unit is a welded 4-pack (a pattern)
  • single cells: no way to launch one                 ← the missing grain
  • and the welds are PERMANENT: you must decide at setup time
    which tracks will forever move together            ← "the decision
                                                          I'm never ready
                                                          to make"
```

That second bullet-pair is the whole pain, stated twice. The welds exist
because of how the files are packed on the SD card — not because of
anything musical.

### The new model — the grid leaves the stage

```
  ┌─────────────────────────────────────────────────────────────┐
  │  THE ORGANISM = the 16 live tracks, RUNNING                 │
  │  T1 T2 T3 T4 T5 [T6] T7 ... T16                             │
  │  This is the instrument now. You sculpt THIS.               │
  └───────▲──────────────────────────────────▼──────────────────┘
          │ PULL                              │ CAPTURE (push)
          │ one cell → one live track,        │ one live track → one cell,
          │ on the bar  (SHIPPED, tonight)    │ on SD  (shipped in May)
  ┌───────┴──────────────────────────────────┴──────────────────┐
  │  THE SHELF = storage, re-projected: 16 columns × 64 slots    │
  │  of SINGLE-TRACK cells (the welds only exist on disk —      │
  │  no verb has to respect them anymore)                       │
  └─────────────────────────────────────────────────────────────┘
```

The inversion, in one sentence: **Ableton makes the grid the instrument and
the live state disposable; this makes the live state the instrument and the
grid its memory.** The grid view doesn't disappear — it moves to the desk
(the future "librarian" page) where grids are the right tool. It just never
again stands between your hands and the music.

---

## 2. The travel question ("moving through what I've made")

Your question: *"to travel through patterns I've created, I still pick a
group + pattern and 4 tracks move, right?"* — Yes, today. Here's the full
trajectory, in Ableton terms:

```
              what moves        Ableton analog              status
  ──────────────────────────────────────────────────────────────────────
  PULL        1 track           drag ONE clip from the      ✅ tonight
              on the bar        browser onto a PLAYING
                                track, quantized (Live
                                can't actually do this
                                in one move — you can)

  group       4 welded          launching a scene where     legacy —
  switch      tracks            someone else chose which    still 4-at-
                                clips are in the row        a-time

  PHRASE      any combination   a scene — but one you make  planned
  (waypoint)  of all 16,        by BOOKMARKING the whole    (next-ish)
              as a bookmark     desk AFTER it sounded
                                good, not a row you had
                                to pre-build

  THE TAPE    nothing moves —   Capture-MIDI / session-     planned
              takes pile up     record: keep what just
                                happened without aiming
```

So the end-state set-management story is:

```
   PERFORMING (stage + rehearsal)            CURATING (the desk, never live)
  ┌──────────────────────────────┐          ┌─────────────────────────────┐
  │ a set = a PATH of waypoints  │  takes   │ trawl the tape              │
  │   wp1 ──► wp2 ──► wp3 ──► …  │ ───────► │ promote keepers to shelf    │
  │ between them: sculpt, pull,  │          │ name things, build/repair   │
  │ capture to tape, undo        │ ◄─────── │ waypoints (grid view OK     │
  │ (no grid, no group math)     │ waypoints│ here — it's a desk tool)    │
  └──────────────────────────────┘          └─────────────────────────────┘
```

"A set is a path, not a grid": in Ableton you walk *rows somebody (you,
weeks ago) pre-built*. Here you walk *moments you flagged because they
sounded right* — and the organism keeps living between them.

---

## 3. Rosetta table

| Ableton | This instrument | The difference that matters |
|---|---|---|
| a clip | a stored track (one cell on the shelf) | identical idea — finally addressable one at a time |
| drag clip → track | **the pull** (hold track + aim + commit) | lands on the bar, into the *running* set, one move |
| Cmd+Z | **SELECT+CLEAR** | musical undo: the old track returns *on the bar* |
| a scene | **a phrase / waypoint** (planned) | scene = row you pre-build; waypoint = bookmark of any combination, made after the fact |
| Capture MIDI / session record | **the tape** (planned) | capture spam with zero aiming; curate tomorrow |
| saving your Live set | **auto-writeback** (planned) | the set always persists; protection (CHECKPOINT) becomes the deliberate act, not saving |
| session view grid | **the librarian** (planned) | the grid still exists — at the desk, where grids belong |
| *(no analog)* | pulling a track as a **spring** (posture face, post-tape) | a pulled clip that isn't a recording but a *behavior* that re-evolves tonight — session view has no equivalent; this is where the instrument stops being Ableton-shaped at all |

---

## 4. Tonight's test, located on the map

You're testing exactly one arrow of the §1 picture: **PULL** (plus its
undo). The question under the question: when single cells can move freely,
does the weld — the decision you were never ready to make — stop mattering?
Everything else (waypoints, the tape, auto-writeback) builds on a yes.
