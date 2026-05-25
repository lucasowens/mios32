# AUTOTEST session — content contract

The dedicated test session on the device's SD card. Tests assume the patterns
described here exist with exactly the state specified. See
[`doc/plans/2026-05-25-test-session-rig.md`](../../../doc/plans/2026-05-25-test-session-rig.md)
for the rationale.

## Status

**Phase 1** — manual build, doc-as-contract. You build the session on the
device once; this file is the source of truth until phase 2 commits the SD
files under `tests/fixtures/AUTOTEST/`.

## Addressing

The plan refers to patterns as **A1, A2, ...** (UI shorthand). In harness code
that maps to `pattern_load(group, bank, pattern)` with `group=0, bank=0`.

| Plan name | Harness call                              | UI position |
|-----------|-------------------------------------------|-------------|
| A1        | `b.pattern_load(group=0, bank=0, pattern=0)` | G1 · A · 1 |
| A2        | `b.pattern_load(group=0, bank=0, pattern=1)` | G1 · A · 2 |
| A3        | `b.pattern_load(group=0, bank=0, pattern=2)` | G1 · A · 3 |

## Activation

Once built and verified, flip [`tests/conftest.py`](../../conftest.py):

```python
TEST_SESSION_NAME = "AUTOTEST"  # was: None
```

The session fixture then swaps the device to AUTOTEST at session start and
restores your previous session on teardown.

## On-device build

Before you start:

1. Stop transport.
2. Save your current session (SAVE menu) — switching sessions discards in-RAM
   edits.
3. SAVE menu → **New Session**, name `AUTOTEST`, confirm. New session loads
   built-in defaults (track 0 gets a preset trigger pattern; tracks 1–15
   have triggers cleared; par-layers initialised to type-defaults — Note
   layers all read C-3). Use **New**, not **Save As** — save-as would clone
   the current session instead.
4. Build the three patterns below. Save the session after each (SAVE → save)
   so a mistake doesn't cost you the others.

---

### A1 — *empty*

**Purpose:** truly-blank canvas. A polyrhythm regen on top should produce
exactly N hits per M-step cycle, with nothing preexisting to contaminate
output. This is what fixes `test_polyrhythm_3_in_8`.

**Contract (track 0):**

- Event mode = `Note` (default for a cleared track).
- Trigger layers: gate trigger empty (no steps lit). Accent / roll / glide /
  skip / random gate / random value — all empty.
- Par-layer A: at the Note-type default (`C-3` everywhere — MBSEQ's CLEAR
  initializes par-A this way). Values don't matter musically because no
  gates fire.
- Length: 16 steps.
- MIDI port / channel: don't care — per-test `track_config()` overrides.

**Build:** select track 1 in EDIT, press CLEAR, save.

### A2 — *4-on-the-floor*

**Purpose:** "default factory" baseline. Tests that need a small amount of
preexisting structure to interact with start here.

**Contract (track 0):**

- Same layer config + par-A defaults as A1.
- Gate trigger lit at steps **1, 5, 9, 13** (every 4th step, starting at 1).
- Length: 16 steps.

**Build:** select track 1 in EDIT, press CLEAR, toggle gates at GP1, GP5,
GP9, GP13, save.

### A3 — *full*

**Purpose:** dense input for tests that *reduce* — robotize skip with high
probability should observably thin this out; mask processors with short
masks should produce a regular subset.

**Contract (track 0):**

- Same layer config + par-A defaults as A1.
- Gate trigger lit on **every** step (1..16).
- Length: 16 steps.

**Build:** select track 1 in EDIT, press CLEAR, toggle gates at GP1..GP16,
save.

---

## Verifying after build

After saving AUTOTEST, from `tests/`:

```python
from harness import Board, Page
with Board() as b:
    b.session_load("AUTOTEST")
    for pattern_idx, label in [(0, "A1 empty"), (1, "A2 4-floor"), (2, "A3 full")]:
        b.pattern_load(group=0, bank=0, pattern=pattern_idx)
        b.page_set(Page.EDIT)
        print(f"\n--- {label} ---")
        print(b.lcd_snapshot().text)
```

On EDIT page, the bottom row's gate-trigger view should match:

- A1: all dots blank
- A2: dots at columns 1, 5, 9, 13
- A3: dots at every column

If any pattern is wrong, fix it on the device, save, re-verify. The harness
view IS the test contract — if it looks right here it'll pass in tests.

## Reserved slots

Banks B–D and patterns A4..A16 are reserved per the plan (bus chord sources,
multi-track rhythm, generator stress). Don't pre-populate; build when a test
actually needs the content.

## Editing this file

If you change pattern content on the device, update this doc in the **same
commit** as the tests that rely on the change. Phase 2 (committing the SD
files themselves) is the durable answer to drift; until then this doc is
the only source of truth.
