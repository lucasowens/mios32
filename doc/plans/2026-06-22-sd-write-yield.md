# SD-write yield — capture-while-performing must not freeze the clock (2026-06-22)

**Status:** queued / not started. Fresh-session kickoff plan.
**Goal:** whole-organism phrase capture (and every other SD write) must not freeze the
sequencer clock while playing. Capture-while-performing is fundamental; today a phrase
capture freezes the clock ~1.16s.
**Durable home for findings:** design doc §9 "2026-06-22 (cont.)" + §10
"capture-while-performing freeze"; memory `project-phrase-review-2026-06-22`.
**Repo state at authoring:** main @ `5638e97a` (freeze cure + morph kept + morph-feel
tuning + lean capture all landed; 2 ahead of origin, not pushed). This is the next bundle.

Archive or fold this into the design doc once executed (plans are scaffolding).

---

## Kickoff prompt (paste to start the fresh session)

```
Make whole-organism phrase capture not freeze the sequencer clock while playing.
Capture-while-performing is fundamental to this instrument, and today a phrase
capture freezes the clock ~1.16s — a showstopper live. This is the queued next
bundle.

START COLD by reading: design doc §9 "2026-06-22 (cont.)" block + the §10
"capture-while-performing freeze" note (doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md),
and the memory project-phrase-review-2026-06-22. Then VERIFY everything below
against THIS fork's source before building on it (line numbers drift; symbols are
the anchors — per CLAUDE.md).

WHAT WE BELIEVE (verify, don't trust):
- Capture path: SEQ_PATTERN_PhraseCapture → SEQ_PATTERN_SnapshotWrite → 4×
  SEQ_FILE_B_PatternWrite (seq_file_b.c, ~290ms each ≈ 1.16s of SD saves).
- Root cause: every SD sector write ends in a busy-wait poll for the card to
  finish programming (MIOS32_SDCARD_SectorWrite, the for(i=0;i<32*65536;...) loop
  ~mios32_sdcard.c:556) with NO yield, and the app NEVER defines the DMA yield
  hook MIOS32_SDCARD_TASK_SUSPEND_HOOK (~mios32_sdcard.c:537-542) — so a capture
  spins the CPU for the whole write.
- THE PARADOX (resolve this first): preemption is on (configUSE_PREEMPTION=1),
  the clock task SEQ_TASK_MIDI (app.c) takes only MUTEX_MIDIOUT — never the SD
  mutex — and the SD spin runs interrupts-on (MIOS32_SPI_TransferByte does not
  disable IRQs). So the higher-priority clock task SHOULD preempt the spin and
  keep running, yet it empirically froze. Re-verify which FreeRTOS task actually
  runs the capture (button → SEQ_UI_Button_Handler) and its priority vs
  SEQ_TASK_MIDI (tasks.c task creation + priorities). A prior pass claimed
  capture=TASK_Period1mS(+2), clock=TASK_MIDI(+4) — that contradicts the freeze,
  so it's probably wrong or incomplete. Resolving the paradox tells you whether a
  simple yield fixes it or a deeper priority/scheduling change is needed.

BUILD THE PERFORMANCE TEST FIRST (explicitly wanted — measurable, not hand-vibes).
Goal: an automated on-device measurement of "did the clock keep running during a
capture." Design notes:
- The 1µs stopwatch saturates at 65.5ms and SYS_TimeGet is seconds-only on
  STM32F4 (memory reference-stm32f4-timing-sources) — so DON'T time the write with
  the stopwatch. Instead measure the CLOCK'S OWN ADVANCEMENT: with the transport
  running, sample SEQ_BPM_TickGet() before vs after the capture write and check
  the BPM tick count advanced ~proportionally to elapsed time. A freeze = ticks
  stall during the write; a healthy clock = ticks keep counting.
- Drive it from the testctrl SysEx harness (it already has CMD_TRANSPORT 0x4a and
  a clock-step verb; free CMD bytes 0x41-0x48). Add a verb that, with the clock
  running, fires a phrase capture and reports the max inter-tick gap / whether
  ticks stalled. The test must FAIL on current firmware (proves it catches the
  freeze) and PASS after the fix. Also expose the per-sector/per-record count so
  the ~290ms figure can be re-confirmed by accumulation.
- Wire it into the HIL harness if it fits the existing pattern.

THEN FIX IT:
- Primary candidate: wire the SD task-yield — define MIOS32_SDCARD_TASK_SUSPEND_
  HOOK / _RESUME_HOOK app-side (mios32_config.h has FreeRTOS access; the driver
  is #ifdef'd to call them) for the DMA transfer, AND add a yield to the
  completion poll (the poll has no hook today). GUARD any yield with
  xTaskGetSchedulerState()==taskSCHEDULER_RUNNING — SectorWrite also runs during
  boot config-load before vTaskStartScheduler. This benefits ALL SD writes
  (capture, working-slot save, CHECKPOINT, recall-writeback), not just capture.
- NOT incremental save: a fresh phrase slot's first capture is all-new data, so
  diffing-against-disk saves nothing. (It only helps re-saves.) The forward-delay
  margin (SEQ_CORE_AddForwardDelay) also can't help — it caps at 250ms vs ~1.16s.
- Consistency note: a capture spanning ~1s reflects live state across the window
  (generators wandering, etc.) — already true today, so no worse. If it matters,
  snapshot the organism to RAM first, but RAM is TIGHT (~9KB free morph-on /
  ~15.6KB with make PHRASE_MORPH=0) — size it before committing to staging.

DISCIPLINE: prove by ear after the test goes green; commit to main after the GO;
fold the result into the design doc (§9 + §10) and retire the deferred note. If
this turns multi-step, drop a plan in doc/plans/YYYY-MM-DD-sd-write-yield.md.
Don't flash while HIL is running (memory reference-autotest-a3-corruption). Build:
source ./source_me_MBHP_CORE_STM32F4 then make in apps/sequencers/midibox_seq_v4
(reference-build-and-flash).

Current main is at commit 5638e97a (2 ahead of origin, not pushed): freeze cure +
morph kept + the morph-feel tuning + lean capture all landed; this bundle is the
next thing.
```

---

## Why this shape

- **The performance test is the diagnostic, not just validation.** Measuring the clock's
  own BPM-tick advancement during a capture both proves the freeze and reveals whether a
  simple yield resolves the paradox or the cause is deeper — and it dodges the 65.5ms
  stopwatch-saturation problem.
- **The paradox comes first.** Preemption is on and the clock task doesn't share the SD
  mutex, so on paper it shouldn't freeze; resolving why it does prevents bolting on a yield
  that doesn't address the real cause.
- **The fix benefits every SD write,** not just capture — it's the SD-write-doesn't-stall-
  the-clock primitive the platform has been missing (the recall freeze was *avoided* via
  drift-gating, never solved at the write level).
