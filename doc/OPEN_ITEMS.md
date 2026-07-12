# MBSEQ V4 fork — Open items (the one board)

**The single place that answers "what's open?"** for defects, hardening, and
housekeeping. Created 2026-07-11 (docs consolidation). What does NOT live here:

- **Design forks / deferred-by-choice builds** → design doc **§10** (the standing forks).
- **The musical roadmap** → design doc **§8** (system-derived queue).
- **Decision history** → [MBSEQV4_DECISIONS_LOG.md](MBSEQV4_DECISIONS_LOG.md).

Full derivations stay in their source docs (linked per item); this board is
pointers + status only. **Update discipline:** when an item ships, move its line
to "Closed" at the bottom with the date — don't delete it.

---

## 1. Code defects (open)

From the 2026-07-10 whole-fork review (report artifact linked in the session memo):

*(all closed — see the Closed section)*

## 2. Adversarial-review tail (2026-07-01)

Source: [reviews/2026-07-01-adversarial-review.md](reviews/2026-07-01-adversarial-review.md)
(72 findings; the last software-only three — #54/#55/#2 — closed 2026-07-11, see Closed.
The hardware-gated pair #35/#46 stays in section 3.)

## 3. Held — hardware-gated (deliberate future sessions)

Fix plans live in [plans/2026-07-02-held-findings-35-46.md](plans/2026-07-02-held-findings-35-46.md) (durable, do not archive).

- **#35 UART "NonBlocking" send busy-waits on the +4 task** — 3-layer fix (RS-safe −2 path,
  defer-not-drop drain, FlushQueue contract). **Now load-bearing: it is the prerequisite for
  the <1 ms ISR-edge/timer drain** (jitter ladder, 2026-07-10 review §4). Needs DIN-analyzer
  + by-ear validation.
- **#46 BSL flash erase/verify** — unreachable from the app build; only with a deliberate
  BSL-update session + the full validation ladder (sacrificial board first).

## 4. Cleanup queue (code surfaces / repo)

- **ext-CC block V5 bump — persist `voice_inv/strum/drop/tilt` (0xA0..0xA3)** — licensed by
  the Voicing GO (2026-07-11, LOG cont. 3; drop/tilt added same day, ladder rung 1). Follow
  the V2→V3 precedent in `seq_file_b.c/.h`: freeze the V3/V4 count (32), widen
  `SEQ_FILE_B_TRK_EXT_CC_LAST` to a new boundary with headroom, new tag 0x05. **Cautions:**
  the writer silently skips ext blocks that don't fit old-sized pattern slots (check
  capacity math); the phrase-morph Loop A iterates the live count — `voice_inv` (nibble,
  discontinuity at raw 8) and `voice_drop` (discrete selector) → SNAP list, `voice_strum`/
  `voice_tilt` (64-biased linear) → lerp is fine. Until then these reset to neutral on
  power-cycle only (they survive pattern switches in RAM). Full ladder:
  [plans/2026-07-11-chord-mode-expressiveness-ladder.md](plans/2026-07-11-chord-mode-expressiveness-ladder.md).
- **`seq_midexp` MIDI export ignores the Voicing strum stagger** — export renders unstrummed
  onsets (spread/inv ARE rendered — they're in the expansion). Decide: teach export the
  per-voice offset, or document as accepted (the tape/capture path already hears strum).
- **Flake trail: `test_as_heard_slot_track_threads_phase`** — failed once in a full-suite
  run 2026-07-11 (rotation match off by ~2 steps + one foreign head byte = capture-vs-
  playhead phase race), passed the same day's other full run + 3/3 isolated re-runs on
  identical firmware. If it reds again, suspect the test's phase-sampling window, not the
  capture engine.
- **Retire `seq_ui_trkpitchgen.c` ("Pitch Gen (POC)" menu page)** — superseded by the PROC
  PitchGen row. Coupled: harness `Page.PITCHGEN` + `tests/capture_now.py`; needs an HIL run.
  (Queued since 2026-06-22, design doc §7 reclaim ledger.)
- **GRAVITY page vs PROC Tension row** — decide once: retire the workbench page or mark it
  diagnostic. Two homes for the field will drift.
- **ROBOLOOP page vs PROC Robotize plane B** — same decision (same verbs in both).
- **"GENERATE" menu label → TRKEUCLID** — old-paradigm destructive fills under a name that
  suggests the living generators; rename/annotate.
- **`tests/diag_*.py` one-offs** — fold the still-useful ones into the suite, delete the rest.
- **Ethernet/uIP compile-out (if OSC-over-ETH is truly unused)** — the single biggest RAM
  reclaim (~6–10 KB main SRAM: uip_buf + conns + UIP task stack). Needs a build-flag pass.

## 5. Deferred test pins

- **Waypoint direction modes HIL pin** — deferred 2026-07-10: harness has no verb to observe
  `t->step` / paint a Waypoint layer; needs firmware+harness plumbing plus a hardware run.
- **HIL timeout gotcha** — real-time-wait tests need per-test `@pytest.mark.timeout`
  overrides (global 10 s); pattern documented in the build/flash memory + conftest.

## 6. Mainline TODO tiers (stock-codebase debt, opportunistic)

Full catalog: [MBSEQV4_TODO_TRIAGE.md](../apps/sequencers/midibox_seq_v4/doc/MBSEQV4_TODO_TRIAGE.md)
(66 markers, tiered 2026-05-18; #2 pattern-stall RETIRED 2026-06-12). Highest-value quick sweep
still unstarted: **#1 overwrite confirm** (3 sites), **#4 bank-empty feedback** (2 sites),
**#5 song bpm_start**. Tier 3+ = latent/cosmetic, leave until touched.

## 7. Fork hardening backlog (low-sev, fix opportunistically)

Inherited from TRIAGE's 2026-06-14 assessment (full text there):
SnapshotRead marks groups dirty+loaded on partial failure · `phrase_present_mask` updated
outside the IRQ guard · FREEZE hold-mode can stick if MENU grabbed mid-hold · unguarded FREEZE
read in `SEQ_GENERATOR_Tick` (self-corrects) · capture name-stamp inherits A-group name on
`PhraseWriteName` failure · GRAVITY clamp asymmetry (−64 vs +63) · testctrl `generator_query`
reply-buffer bound needs a `static_assert` · idea: small undo ring (2–3 deep).

---

## Closed (move lines here with a date, don't delete)

- **2026-07-11** | #54 | Catch-up/prefetch burst under MUTEX_MIDIOUT — **FIXED**: prefetched
  (not-yet-due) ticks capped at 8/service (`SEQ_CORE_PREFETCH_TICKS_PER_SERVICE`), remainder carried
  in `bpm_tick_prefetch_carry` (separate from `prefetch_req` so a spread batch can't refuse a new
  forward-delay; carry holds the PRE-offset-pad goal so the pad can't creep the target). Due ticks
  never capped. First rung of the jitter ladder.
- **2026-07-11** | #55 | CAPTURE tape note-off 768-entry ring back-walk — **FIXED**: O(1) per-note
  open-index (`seq_core_cap_tape_open_idx[128]`, index+1 sentinel, re-validated against ring wrap;
  +256 B). Accepted edge: same-note overlap keeps the OUTER note at default gate.
- **2026-07-11** | #2 | Fwd/Rew `bpm_tick` RMW race — **FIXED**, but not by the suggested IRQ-wrap
  (that would mask IRQs across `PlayOffEvents`/`FetchPos` — the #17 anti-pattern): the actual
  lost-update was the redundant trailing `SEQ_BPM_TickSet` re-pin after `SEQ_CORE_Reset` had already
  landed the jump atomically (TickSet is IRQ-guarded since #1's fix); dropped it in both verbs.

- **2026-07-11** | F1 | Arp tenant never neutralized on bounce/capture — **FIXED, HIL 250/250**:
  `arp_mode`/`arp_bus` zeroed in `SEQ_CC_ResetGenerativeForBounce` + `SEQ_CORE_ProcessorBounce`; sibling
  stale-slot fix on the to-track paths (`CaptureToTrack`/`CaptureSpanPrepDst` get `SEQ_CORE_AllSlotSync`
  — ARP renders from `slot->strength`, so a stale-armed slot re-arps even with tcc zeroed). Pins in
  `test_arp_bounce.py`. Full derivation: DECISIONS_LOG 2026-07-11.
- **2026-07-11** | F2 | Slot-capture restore fans skipped Arp — **FIXED, HIL 250/250**: new
  `SEQ_CORE_AllSlotSync()` helper (all 5 tenant syncs) replaces the 4-sync fans in the 3 restore loops
  + the `seq_file_t.c` preset-import fan. Bystander pin (both slot verbs) in `test_arp_bounce.py`.
- **2026-07-11** | F3 | `win_o` may-be-uninitialized warning — **FIXED**: `u8 win_o = 0;` in
  `SEQ_CORE_CaptureSpanReSim`. Build now has ZERO warnings — treat any new one as a defect signal.
- **2026-07-11** | F4 | Three P4 notes — **CLOSED** (comments/classification, no behavior change):
  `SEQ_UI_PROC_LfoWaveName` static-buf constraint documented (one call per printf);
  `SEQ_GENERATOR_Tick` 1-step-track wrap hole documented as accepted (degenerate musically, not
  worth a per-track advance counter); `SEQ_PAR_Type_Waypoint` classified in
  `ResetGenerativeForBounce` as **PRESERVE** (painted path = deterministic step data like Note;
  inert on the frozen copy since dir_mode resets to Forward, but re-arming a Wp mode re-uses it).
