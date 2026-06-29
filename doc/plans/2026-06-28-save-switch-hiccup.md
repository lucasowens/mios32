# Save/switch hiccup — deep dive + measure-first bench (2026-06-28)

Residual: user STILL feels a hiccup on plain pattern save/switch despite the shipped
2026-06-23 control-surface fix. Reported symptom = **both audible (clock/notes) AND visual
(LED/LCD freeze)**, on **same-group save + cross-group switch**, triggered by **on-device GP
buttons** while playing. (Phrase/morph and external-MIDI/EXT_CTRL paths confirmed OUT of scope
for this user.)

Method: 21-agent workflow (map → 3-lens diagnose → merge → options → adversarial critic). All
code anchors below re-verified against THIS fork's source.

## Diagnosis — one root, two amplifiers

**ROOT (sev5, all 3 lenses #1): the switch does its SD I/O inside a critical section on the +4
emission task.** `SEQ_PATTERN_Handler` wraps `WritebackIfDirty` (~290ms SD write) + `Load` (SD
read) in `MUTEX_SDCARD_TAKE; portENTER_CRITICAL()` (`seq_pattern.c` ~1439–1501). In synched mode
(GP-button live config) that runs on the +4 task via `SEQ_CORE_Tick` (`seq_core.c` ~4414), *before*
`SEQ_MIDI_OUT_Handler` drains the queue (`app.c` ~848 before ~851, one `MUTEX_MIDIOUT`). For the
whole I/O window the clock (0xF8) and due notes freeze; `bpm_tick` keeps advancing in the ISR, so
on return the queue flushes everything past-due in one **burst** — clock pauses then sprays, notes
bunch. The shipped 2026-06-23 poll-yield (`TASKS_SDCardPollYield`/`vTaskDelay`) is **inert here** —
`vTaskDelay` cannot context-switch with the scheduler suspended inside `portENTER_CRITICAL`. The
cure already exists in-tree: `SEQ_PATTERN_SnapshotRead` (`seq_pattern.c` ~561–578) deliberately
does the recall read with **interrupts ON**; the switch path never got that treatment.

**AMPLIFIER 1 (sev5): the forward-delay margin is fed garbage.** `SEQ_CORE_SwitchMarginMs` sizes
the pre-fire window from `seq_core_pattern_switch_measured_ms`, but the stopwatch `Reset()` is
gated behind the terminal-only `seq_pattern_log_load_time` flag (`seq_pattern.c` ~1442), so in
live use TIM6 is never reset → the delta is "time since some unrelated tick", ratchets to a sticky
MAX for the whole power cycle, and the 1µs/16-bit stopwatch **saturates at 65.5ms** (pins to 65)
so it can't even represent a 290ms write.

**AMPLIFIER 2 (sev4): even measured right, the window can't cover the write.** The pre-fire is
clamped `pre_ticks >= 95` and phase-anchored to one 96-tick interval (`seq_core.c` ~4380–4383), so
raising the 250ms cap alone does nothing (critic correction: the candidate mis-blamed the cap; the
real wall is the clamp). And the writeback fires on the **un-gated** dirty bit, so a group that
only *wandered* under a generator pays a full ~290ms save — the cost the recall path already
drift-gates away (`WritebackIfDrifted`, `seq_pattern.c` ~347).

### Already shipped — do NOT re-propose
Lock-free REQ pre-check in `SEQ_PATTERN_Handler` (12ad28cb); `MIOS32_SDCARD_WAIT_HOOK` →
`TASKS_SDCardPollYield`, write-only (78d308b2); DRIFT-gated **recall** writeback (2026-06-19/22);
`SnapshotRead` interrupts-ON recall read (2026-06-22); render change-detection (371aef66).

### Critic-flagged gaps (parked, out of scope for this user)
EXT_CTRL switch on +3 inside a caller-held `portENTER_CRITICAL` with a synchronous immediate
branch (`seq_midi_in.c` ~1124); BLM ALT bare `SEQ_PATTERN_Save` on +2 (`seq_blm.c` ~725); the
sustained-NoteOff cancel mechanism (`seq_core.c` ~6630) was UNVERIFIED.

## Option menu (tiered)

- **Tier 0 · measure-first** — this bench. ~0 engine code.
- **Tier 1 · cheap pair (recommended fix, approved direction):**
  1. Drop `portENTER_CRITICAL` in `SEQ_PATTERN_Handler` (mirror `SnapshotRead`, interrupts-ON,
     MUTEX_SDCARD-only) → re-enables the poll-yield, unmasks the clock ISR. ~−16 B.
  2. Drift-gate the switch writeback (`WritebackIfDirty` → `WritebackIfDrifted` at `seq_pattern.c`
     ~1454). ~10 B. **User approved the semantic: switch no longer auto-banks generator wander**
     (already true on recall).
  3. Free companions: same-slot short-circuit; drain MIDI-out queue just before the switch.
- **Tier 2 (if pair insufficient by ear):** move switch I/O off +4 (arm on +4, service on +2);
  fix the margin measurement (bpm-tick delta, not shared TIM6) + lift the pre_ticks clamp; split
  deferred-writeback from the load.
- **Tier 3 (RAM-bound, ~9KB free):** double-buffer load (read-ahead + atomic swap, I/O-free
  boundary at any grid); async dirty-flush queue; collapse working-slot persistence onto the
  RAM/journal model (the capture-centric MATERIAL/LIBRARY split).

## The bench (built 2026-06-28)

Passive freeze probe — reads the existing +4/+2 service-gap probes across a window; **zero code in
the switch critical path → no observer effect** (the `log_load_time` flag is the wrong instrument:
its `DEBUG_MSG` does a blocking SysEx send INSIDE the critical section and lengthens the freeze).

- Firmware: `CMD_SWITCH_PERF` 0x44 (TESTCTRL-only), `cmd_switch_perf` in `seq_testctrl.c`.
  sub 0 = ARM (reset `SEQ_CORE_ServiceGapReset`/`UIServiceGapReset` + stamp t0); sub 1 = READ →
  `wall, max_gap(+4 emission = audible freeze), ui_gap(+2 = visual freeze), measured_ms, margin_ms,
  pre_ticks, grid16ths`.
- Harness: `Board.switch_perf_arm()` / `switch_perf_read()` (`tests/harness/board.py`),
  `CMD_SWITCH_PERF` (`tests/harness/sysex.py`), runner `tests/diag_switch.py` (manual by-ear
  default; `--auto` harness-driven repeatable).

Run: `cd tests && .venv/bin/python diag_switch.py` (by-ear, GP-button switch into a slaved device)
or `--auto`. Reference: ~290ms write ≈ ~260 ISR ticks @140BPM/384ppqn.

**Freeze PROFILER (`tests/diag_freeze.py`, added 2026-06-28):** the probe is passive/gesture-
agnostic, so this runner arms it around EVERY SD-writing gesture in turn and prints a ranked table
(worst audible freeze first): switch clean / switch dirty / save / slot-capture static / slot-
capture live-span / phrase-capture (4-group) / checkpoint. Reuses CMD_SWITCH_PERF — NO re-flash.
WRITES to SD on the active session (scratch session only). This subsumes the standalone dirty-
switch measurement and answers "is capture the real hiccup, not switch?" in one pass. The
phrase/whole-organism capture also still has its own dedicated inline probe CMD_CAPTURE_PERF
(0x48, board.capture_perf(n)).

**Decision gate:** if `max_gap` spikes to write-sized (~hundreds of ticks) and the ear hears the
lurch → ROOT confirmed → ship the Tier-1 pair. If `max_gap` ≈ 0 → the +4 path is not the residual;
re-aim (margin/pre_ticks or elsewhere). Re-run after each fix; watch the gap shrink.

## Status (PAUSED 2026-06-29)

Measure-first ran on-target. **No fix shipped — none warranted yet.** The hiccup did NOT reproduce
above perception on the bench. What we learned:
- User config: **Synched Pattern Change OFF + switch-quantize grid = Instant.** => the forward-delay
  MARGIN subsystem (Amplifiers 1 & 2) is MOOT for this user. Drop that whole branch.
- Switch (clean) froze +4 ~54-57 ticks (~60ms) and +2 ~72-89 ticks; **user could NOT hear it** — the
  switch is below perception. Switch is no longer a concern.
- User pinned the real complaint as **CAPTURE-while-playing** (the harvest). But the capture write is
  ALREADY the good pattern: `SEQ_CORE_CaptureSpanToSlotTrack` (seq_core.c:3068) writes under
  MUTEX_SDCARD only, NOT in portENTER_CRITICAL (comment :3061 "must run in task context"), on +3,
  with the poll-yield => audible (+4 preempts) AND UI (+2 via yield) already largely protected.
- Repeated capture runs by ear: "sounded good," no delay. Same shape as the 2026-06-23
  capture-freeze-overturned: a remembered freeze the shipped fixes already cured.

Net effect: the 2026-06-23 work (poll-yield + lock-free pre-check + DRIFT writeback) appears to have
already handled the audible/UI freeze for both switch and capture on this config. Nothing left to fix
that we could hear — UNLESS a real worst-case load surfaces something (untested: no live set at pause).

## >>> RESUME HERE NEXT TIME (when it hiccups in a real set) <<<

The deliverable from this session is a PERMANENT, zero-cost freeze net (TESTCTRL-only, already on the
device). Next time you actually hear/feel a hiccup live:

1. **Don't rebuild/reflash** unless firmware changed — the probe (`CMD_SWITCH_PERF` 0x44) is already
   flashed. (Sanity: `diag_switch.py` should reply, not time out.)
2. **Catch it red-handed:** `cd tests && .venv/bin/python diag_switch.py` → ARM → reproduce the exact
   gesture that hiccuped → READ. Read `peak EMISSION (+4)` (audible freeze) and `peak UI (+2)` (visual).
3. **Or survey all gestures ranked:** `diag_freeze.py --bpm <tempo> --cap-k 8 --repeats 5` (writes to
   scratch slots; "live span k=8" is the heavy capture). Best run under your densest set + generators +
   GRAVITY engaged (the all-16 render load is the true stressor — never tested at pause).
4. **Decision tree from the number:**
   - +4 spikes (audible) on capture → go after the `MUTEX_MIDIOUT` span-render hold + the
     `MIOS32_IRQ_Disable` RAM-swap window in the capture path (seq_core.c:3061, :3233).
   - only +2 spikes (visual) → move the slot write OFF the live task (async/deferred writeback).
   - neither spikes but you hear it → it's musical (deposit lands off-downbeat), chase the timing,
     not a freeze. See [[as-heard-windowing]] work.
   - switch ever becomes audible → the approved cheap pair: interrupts-ON Handler (drop
     portENTER_CRITICAL, mirror SnapshotRead) + drift-gate the switch writeback (seq_pattern.c:1454).

The full option menu (Tiers 0-3) above is intact if a confirmed target needs a bigger fix.
