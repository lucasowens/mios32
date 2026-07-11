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

| # | Item | Sev | Where | Fix shape |
|---|---|---|---|---|
| F3 | Only compiler warning in the build: `win_o` may-be-uninitialized (false positive) | P4 | `SEQ_CORE_CaptureSpanReSim`, seq_core.c ~2690 | `u8 win_o = 0;` |
| F4 | Notes: `SEQ_UI_PROC_LfoWaveName` static sprintf buf (UI-only, comment it); 1-step track never fires `SEQ_GENERATOR_Tick` wrap-detect; `SEQ_PAR_Type_Waypoint` not classified in `ResetGenerativeForBounce` (inert today — dir resets to Forward) | P4 | — | opportunistic |

## 2. Adversarial-review tail (2026-07-01; still open)

Source: [reviews/2026-07-01-adversarial-review.md](reviews/2026-07-01-adversarial-review.md)
(72 findings; all but these closed by the 07-01/07-02 sweeps).

| # | Item | Sev | Note |
|---|---|---|---|
| #54 | Catch-up/prefetch can run ~120 `SEQ_CORE_Tick`s in one 1 ms slot under MUTEX_MIDIOUT | P2 rt-timing | bound ticks-per-service; **prerequisite for the jitter arc** |
| #55 | CAPTURE tape note-off back-walk up to 768 ring entries inside the MIDI drain | P2 rt-timing | cap scan depth or index opens by note |
| #2 | Fwd/Rew `bpm_tick` read-modify-write races the master timer ISR | P3 concurrency | wrap in IRQ-disable (SEQ_BPM_Start/Stop pattern) |

## 3. Held — hardware-gated (deliberate future sessions)

Fix plans live in [plans/2026-07-02-held-findings-35-46.md](plans/2026-07-02-held-findings-35-46.md) (durable, do not archive).

- **#35 UART "NonBlocking" send busy-waits on the +4 task** — 3-layer fix (RS-safe −2 path,
  defer-not-drop drain, FlushQueue contract). **Now load-bearing: it is the prerequisite for
  the <1 ms ISR-edge/timer drain** (jitter ladder, 2026-07-10 review §4). Needs DIN-analyzer
  + by-ear validation.
- **#46 BSL flash erase/verify** — unreachable from the app build; only with a deliberate
  BSL-update session + the full validation ladder (sacrificial board first).

## 4. Cleanup queue (code surfaces / repo)

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

- **2026-07-11** | F1 | Arp tenant never neutralized on bounce/capture — **FIXED, HIL 250/250**:
  `arp_mode`/`arp_bus` zeroed in `SEQ_CC_ResetGenerativeForBounce` + `SEQ_CORE_ProcessorBounce`; sibling
  stale-slot fix on the to-track paths (`CaptureToTrack`/`CaptureSpanPrepDst` get `SEQ_CORE_AllSlotSync`
  — ARP renders from `slot->strength`, so a stale-armed slot re-arps even with tcc zeroed). Pins in
  `test_arp_bounce.py`. Full derivation: DECISIONS_LOG 2026-07-11.
- **2026-07-11** | F2 | Slot-capture restore fans skipped Arp — **FIXED, HIL 250/250**: new
  `SEQ_CORE_AllSlotSync()` helper (all 5 tenant syncs) replaces the 4-sync fans in the 3 restore loops
  + the `seq_file_t.c` preset-import fan. Bystander pin (both slot verbs) in `test_arp_bounce.py`.
