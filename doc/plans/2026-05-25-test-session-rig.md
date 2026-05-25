# Test-session rig (HIL harness) — design

**Status:** designed, not implemented. Scheduled as a focused work session
after §8 step 5 phase A commit (2026-05-25). User explicitly greenlit the
investment: *"future development hinges on a solid test rig."*

## Why this is needed

The harness has 21 tests, 1 known-baseline failure
(`test_polyrhythm_3_in_8`). Investigation 2026-05-25 traced the failure to
**device pattern state**, not generator behavior:

- The polyrhythm generator correctly writes a sparse 3-of-8 gate trigger
  pattern (EDIT page LCD confirms gates at steps 3, 6, 8, 11, 14, 16
  after setup).
- The user's loaded session has multiple Note-typed par-layers (A1, A5–A8)
  populated with values at every step, so each gate step fires several
  near-simultaneous NoteOns regardless of which gates the generator armed.
- Net result: the captured NoteOn stream fires on every step at ~107 ms
  intervals, breaking the interval-regularity assertion.
- Loading pattern A1:1 on this device gives different content depending on
  what was last saved; `pattern_load` is not deterministic across machines.

Generalising: the harness has no way to *prove* the device's pattern state
when a test starts. Tests that need a specific starting state silently
inherit whatever the user was last jamming on. This will keep biting as
we add tests for harmonic alignment, transposing, multi-track scenarios —
all of which depend on specific par-layer content.

## The proposal

Build a **dedicated test session** (`AUTO_TEST` working name), check the
session SD files into source control, and switch to it at the start of
every test session. The session has banks of hand-crafted patterns whose
content is fully specified so tests can rely on them.

## Session content (first cut)

Four banks (A–D), 16 patterns each.

**Bank A — baselines.** Single-track, deterministic content for generator
tests.
- A1 — *empty*. Track 0 normal Note mode, par-layer A1=Note, A2–A8=None,
  link_par_layer_velocity = -1, no gates set, par-layer 0 all zeros. The
  truly-blank canvas — a polyrhythm regen on top gives exactly N hits per
  M-step cycle.
- A2 — *4-on-the-floor*. Same as A1 but gate trigger has gates every 4
  steps and par-layer 0 has note=C-3 at every step. The "default factory"
  for tests that need preexisting structure to interact with.
- A3 — *full*. Gates every step, par-layer 0 = C-3 at every step. Useful
  for testing things that *reduce* (robotize skip, mask processors).
- A4..A16 — reserved.

**Bank B — harmonic alignment.** Multi-track patterns where track 0 is
the test target and tracks 1–3 are bus chord sources with deterministic
PC-sets on a Bus port. Lets us test ChordMask, scale-force, future
transpose work without needing an external Ableton chord source.
- B1 — *C maj triad*. Track 1 emits {C, E, G} on Bus 1; track 0 silent.
- B2 — *A min triad*. Track 1 emits {A, C, E} on Bus 1.
- B3 — *modulating*. Track 1 emits a 4-bar chord progression on Bus 1.
- B4..B16 — reserved.

**Bank C — multi-source rhythm.** Patterns where multiple tracks fire on
different channels — for testing isolation, mute/solo, polyphony,
multi-port routing.
- C1 — *one-per-track*. 4 tracks, each on a distinct channel, distinct
  gate pattern. Test fixture for "did the right track fire?" assertions.
- C2..C16 — reserved.

**Bank D — generator stress.** Patterns sized for the longer-loop
generator work (§A5: up to 64-step cap, eventually 128 via tiling).
- D1..D16 — reserved.

Bank/pattern names will eventually be canonical so test code can refer to
them by symbolic name (`fixtures.bank_a.empty`, etc.).

## Firmware surface (new SysEx)

In `seq_testctrl.c`:

- **`CMD_SESSION_LOAD <name>`** — switch active session by name. Drives
  the existing `SEQ_FILE_*_Load` chain (GC/HW/MIXER/B/etc.) to swap the
  session in. Reply: `[status, name_len, name...]`. Name length is bounded
  by the SEQ V4 session-name limit (8 chars in mainline, maybe more in
  this fork).
- **`CMD_SESSION_NAME_GET`** — returns the active session name. Lets
  conftest snapshot the user's prior session for restore on teardown.

**Deferrable but valuable:**
- **`CMD_PAR_BYTE_SET <track> <offset> <value>`** — direct byte write to
  `seq_par_layer_value[track][offset]`, bypassing the EDIT-encoder path.
- **`CMD_TRG_BYTE_SET <track> <offset> <value>`** — same for trg layer.

The byte-write commands enable test fixtures to construct ad-hoc states
without needing a pre-baked pattern for every scenario. They're the
ergonomic answer to "I want a 5-of-8 gate pattern just for this test."
Phase A's render-cache means both writes need to also
`SEQ_CORE_RenderDirtySet(track)` so the next tick refreshes the output
mirror — same plumbing the SEQ_PAR_Set / SEQ_TRG_Set primitives already
do.

**Recommended ship order:** `CMD_SESSION_LOAD` + `CMD_SESSION_NAME_GET`
first (unblocks the test session), then add the byte-write commands when
the first test wants ad-hoc state.

## Harness wrappers (Python)

In `tests/harness/board.py`:

```python
def session_load(self, name: str, timeout: float = 5.0) -> None: ...
def session_name_get(self, timeout: float = 1.0) -> str: ...
def par_byte_set(self, track: int, offset: int, value: int, ...) -> None: ...
def trg_byte_set(self, track: int, offset: int, value: int, ...) -> None: ...
```

Session-load is genuinely slow (multi-file SD read); a 5s timeout is
realistic and matches the existing `pattern_load` timeout pattern.

## Conftest changes

```python
TEST_SESSION_NAME = "AUTO_TEST"

@pytest.fixture(scope="session")
def _board_session():
    b = Board()
    prior_session = None
    try:
        prior_session = b.session_name_get()
        b.session_load(TEST_SESSION_NAME)
    except (TimeoutError, ProtocolError):
        pytest.skip(f"{TEST_SESSION_NAME} session unavailable on device")
    yield b
    if prior_session and prior_session != TEST_SESSION_NAME:
        try:
            b.session_load(prior_session)
        except Exception:
            pass  # best-effort restore
    b.close()
```

Per-test fixture stays as today (transport stop + page reset). Tests that
need a specific pattern call `board.pattern_load(group, bank, pattern)`
themselves — the pattern they load is now deterministic because the
session is.

## Content provisioning (two-phase)

**Phase 1 — manual session, doc'd contract.** User builds `AUTO_TEST`
session on the device once with the content above. We commit a
`tests/fixtures/AUTO_TEST/CONTENTS.md` that specifies every pattern's
content as the *contract*; tests rely on the contract. The SD files
themselves aren't committed yet — distribution is "build it once per
machine following the doc."

**Phase 2 — committed fixture, distributable.** Once the session is
stable, copy the SD files (`SESSIONS/AUTO_TEST/`) into
`tests/fixtures/AUTO_TEST/` and commit. New dev machines can `cp -R` to
their SD card. Eventually a `make test-fixture-install` target.

**Phase 3 (much later, only if needed) — auto-provision.** Conftest
detects missing session, uploads from `tests/fixtures/` via a new
`CMD_SD_WRITE` command. This is genuinely risky (corrupting the SD card
on a bug) and only worth doing for CI scenarios.

## Open questions / decisions to make at implementation time

- **Session name length.** Verify the SEQ V4 session-name byte cap in
  this fork (`seq_file.c` / `seq_file_hw.c`); `AUTO_TEST` is 9 chars,
  might need shortening.
- **How `SEQ_FILE_*_Load` actually switches.** May not be one call —
  could need to chain through `SEQ_FILE_C_Load`, `SEQ_FILE_GC_Load`,
  `SEQ_FILE_B_Load` × N banks, etc. Map this out from the existing
  "load session" UI flow (likely in `seq_ui_menu.c` or `seq_file.c`)
  before writing the SysEx.
- **Teardown restore robustness.** If `prior_session` is the same name
  as `TEST_SESSION_NAME` or empty (fresh device), skip the restore.
  Best-effort on errors so tests don't fail just on cleanup.
- **What happens to the user's *current* in-RAM edits** when we
  session-switch? They're lost. Doc this loudly in the test README.
- **Test that depends on pattern A1:1 in the *user's* session.**
  Currently the failing-after-pattern_load encoder/smoke tests
  implicitly relied on the user's loaded content. Once we switch to
  AUTO_TEST those tests will need to be rewritten against AUTO_TEST
  patterns OR explicitly load a non-AUTO_TEST pattern to test against.

## Implementation order for the work session

1. Map `SEQ_FILE_*_Load` chain — what does loading a session actually
   call? (read-only investigation, no code changes)
2. Add `CMD_SESSION_LOAD` + `CMD_SESSION_NAME_GET` to seq_testctrl.c.
3. Add harness wrappers.
4. Wire conftest, with TEST_SESSION_NAME unset (no-op) for first pass.
5. User builds AUTO_TEST session content on device per the spec above.
6. Write `tests/fixtures/AUTO_TEST/CONTENTS.md` documenting the contract.
7. Set TEST_SESSION_NAME, run suite, verify `test_polyrhythm_3_in_8`
   passes against the empty A1 baseline. Identify which other tests
   break against the new baseline; rewrite them to load the appropriate
   AUTO_TEST pattern.
8. Add byte-write commands when the first need arises.
9. Phase 2 (commit SD files) when content has stabilised for a few
   sessions.

## What this *doesn't* address

- The Phase A "EDIT shows sparse but playback fires every step" puzzle
  from 2026-05-25's investigation. The proximate cause is multi-Note-layer
  par content (which the new session structure removes), but the deeper
  question — what gate path is the playback actually reading? — is still
  open. Worth a focused dig once we have a clean baseline to A/B against.
- CI execution. The harness requires a real device. Some form of
  hardware-in-the-loop CI is a separate piece of work.
