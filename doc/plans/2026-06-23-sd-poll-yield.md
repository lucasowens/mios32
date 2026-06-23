# SD completion-poll yield — keep the control surface live during SD writes (2026-06-23)

**Status:** queued / not started. Fresh-session kickoff plan. **This is POLISH, not a freeze fix.**
**Goal:** the ~640 ms control-surface hang (LEDs/LCD/buttons dead) during an SD write — phrase
capture, working-slot save, CHECKPOINT, recall-writeback, session ops — should go away, so the box
stays responsive while it writes. The *audible clock already does not freeze* (proven on-device
2026-06-23); this is about the +2 UI task being starved, which the user called "feels fine," so it's
deliberate gigability polish, not urgent.
**Durable home for findings:** design §9 (2026-06-23 block) + the memories below.
**Repo state at authoring:** main @ `85dda632` (capture-perf bundle + test hygiene landed; ahead of
origin, not pushed).

Archive or fold this into the design doc once executed (plans are scaffolding).

---

## Kickoff prompt (paste to start the fresh session)

```
Make SD writes stop hanging the control surface. Today a phrase capture (and every other SD
write — working-slot save, CHECKPOINT, recall-writeback, session ops) busy-waits the CPU for
~640 ms, during which the +2 UI task is starved: LEDs/LCD/buttons go dead while the music keeps
playing. This is POLISH (the user already called the capture "feels fine"), so do it carefully and
diagnostic-first — it touches shared platform code on the SD path that every file op uses.

START COLD by reading: design doc §9 "2026-06-23" block (doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md),
and the memories project-capture-freeze-overturned-2026-06-23 + reference-seq-task-topology. Those
already establish the architecture (verified on-device). Then VERIFY the source specifics below
against THIS fork before building (symbols are the anchors; line numbers drift — per CLAUDE.md).

DO NOT re-litigate the audible freeze: it was MEASURED that whole-organism PhraseCapture does NOT
starve the +4 emission task (1 ISR tick of 570; CMD_CAPTURE_PERF / board.capture_perf already exist
as the regression guard). The target here is the +2 UI task (SEQ_TASK_Period1mS — LED/LCD), and
same-priority button scan (TASK_Hooks +3), being starved by the +3 SD write.

WHAT WE BELIEVE (verify, don't trust):
- The bottleneck is the completion busy-wait in MIOS32_SDCARD_SectorWrite (mios32/common/
  mios32_sdcard.c, the for(i=0; i<32*65536; ++i){ ret=MIOS32_SPI_TransferByte(...); if(ret!=0x00)
  break; } loop ~:556). It spins the CPU while the card programs (~8 ms/sector measured: ~640 ms /
  ~76 sectors for a 4-record capture). No yield; the app never defines MIOS32_SDCARD_TASK_SUSPEND_
  HOOK, and that hook only wraps the ~0.28 ms DMA payload anyway — NOT this poll.
- This is a SHARED MIOS32 driver (mios32/common/), used by every app/SD op on the fork. Editing it
  is editing the platform — that is the main risk. Keep the change tiny, guarded, reversible.

BUILD THE DIAGNOSTIC FIRST (measure the hang before fixing — same discipline that overturned the
last bundle's premise):
- Add a +2 UI-task service-gap tracker, mirroring the existing +4 one (SEQ_CORE_ServiceGapReset /
  SEQ_CORE_ServiceMaxGapGet in seq_core.c, recorded at the top of SEQ_CORE_Handler). Put the +2
  tracker at the top of SEQ_TASK_Period1mS (core/app.c) — it advances only when the UI task runs —
  using SEQ_BPM_TickGet() as the ISR time base (bpm_tick keeps counting through the stall, which is
  exactly why it works as the clock here). Expose reset/get like the +4 pair.
- Extend CMD_CAPTURE_PERF (0x48, core/seq_testctrl.c) to ALSO reset+report the UI gap (add a 5x7
  field; update board.capture_perf() + test_capture_perf.py). Confirm on current firmware that the
  UI gap ≈ wall_ticks (hang present) while the emission gap stays ~0 (clock fine). THAT is the
  before/after metric the fix must drive down.

THEN FIX IT:
- Add a periodic yield INSIDE the completion poll. It MUST be vTaskDelay(1), NOT taskYIELD —
  taskYIELD only switches among equal/higher priority, but the UI is +2 (BELOW the +3 writer), so
  only a real sleep lets it run. The card programs autonomously in parallel, so sleeping the poll
  doesn't slow the write meaningfully; it just frees the CPU. GUARD with xTaskGetSchedulerState()
  == taskSCHEDULER_RUNNING (SectorWrite also runs during boot config-load before the scheduler
  starts). Benefits ALL SD writes.
- TRAP to handle: the poll's 32*65536 iteration bound is a card-timeout guard. Once iterations can
  sleep, that becomes a huge wall-clock timeout — convert it to a TIME bound (or cap the yield
  count separately) so a genuinely stuck card still fails fast.
- Note: yielding while holding the J16/SPI bus mutex (xJ16Semaphore) is fine for the UI (SRIO is a
  different bus) but blocks other J16 users (ethernet) for the write — acceptable; nothing else
  needs SD during a capture. MUTEX_SDCARD serializes SD users, so a poll-yield frees the clock/UI
  but not a second concurrent SD writer (there isn't one here).
- NOT the SUSPEND_HOOK alone (covers only the DMA, ~0.28 ms — useless). NOT incremental save (a
  fresh capture is all-new data). A faster physical SD card is a free complementary win (shortens
  every write, zero code) but doesn't make the box responsive DURING the write — mention it, don't
  rely on it.

DISCIPLINE: prove by EYE after the diagnostic+fix (LEDs/buttons stay live through a capture while
playing) and confirm the UI-gap metric collapses; run the full HIL suite (currently 196/196 — many
tests do real SD I/O, so a driver regression would surface) plus the extended capture-perf
assertion; commit to main after the GO; fold the result into design §9 (+ retire §8 timing-test (8)
note if fully realized). If multi-step, drop a plan in doc/plans/. Don't flash while HIL runs
(memory reference-autotest-a3-corruption). Build: source ./source_me_MBHP_CORE_STM32F4 then make in
apps/sequencers/midibox_seq_v4; flash project.hex via MIOS Studio (manual). Main @ 85dda632.
```

---

## Why this shape

- **Diagnostic-first, again.** Last bundle, building the perf test first overturned the whole
  premise (the clock never froze). Same move here: build the +2 UI-gap probe and confirm the hang
  is real and bounded before touching the shared driver.
- **The fix is one well-understood line with two traps** (vTaskDelay-not-taskYIELD; convert the
  iteration timeout to a time bound). Naming them up front keeps the change tiny and safe.
- **Strong safety net.** It's platform code, but 196 HIL tests exercise real SD I/O and would catch
  a regression; the change is trivially reversible.
