# MBSEQ V4 Reference Catalog

Living reference for the MIDIbox SEQ V4 firmware (this app: `mios32/apps/sequencers/midibox_seq_v4/`). Compiled from `CHANGELOG.txt` (98 versions, V4.0beta1 → V4.098) and the source tree kept current; last full sweep 2026-06-14.

When something changes, **update this doc**, not session memory. Memory keeps preferences and in-progress mental state; this doc owns derived facts about the codebase.

Sections:
1. Core design principles
2. TK quirks & idioms
3. Bus tracks — deep-dive (corrected model)
4. Feature catalog by version
5. Feature index by category
6. Open TODOs / FIXMEs / XXX (65 items, grouped by file)
7. Deferred / wishlist features from CHANGELOG

---

## 1. Core design principles

These are the invariants TK enforces across the codebase. Violating any of them silently breaks something — usually latency or persistence.

### Real-time before everything

- Musical resolution is **384 ppqn** ([seq_core.c:267](../core/seq_core.c#L267), `SEQ_BPM_PPQN_Set(384)`) — 16× MIDI clock. That is the unit step events are scheduled in.
- FreeRTOS runs the sequencer tick batch in `SEQ_TASK_MIDI()` every 1 ms ([app.c:830](../core/app.c#L830), driven by `TASK_MIDI` on `vTaskDelayUntil(…, 1/portTICK_RATE_MS)` with `configTICK_RATE_HZ = 1000` — *not* `SEQ_TASK_Period1mS()`; a long-standing doc slip corrected 2026-06-08). Inside that task `SEQ_CORE_Handler` → `SEQ_CORE_Tick()` processes any BPM ticks that fell due since the last call in a batch, then `SEQ_MIDI_OUT_Handler` emits them. So a single 1ms scheduling slot may carry zero, one, or several musical ticks depending on tempo — the per-slot processing budget is the jealously guarded resource, not "one event per ms." (See the **Output timing, latency & jitter** deep-dive below for the full trigger→wire pipeline.)
- At 120 BPM, 384 ppqn ≈ 768 ticks/sec ≈ 1.3 ms/tick; at higher tempos several ticks may need to fire inside one 1ms slot. This is exactly why `bpm_tick_prefetch_req` / `bpm_tick_prefetched` exist ([seq_core.c:130](../core/seq_core.c#L130)) — to pre-compute upcoming events so the slot only has to dispatch them.
- `STOPWATCH_PERFORMANCE_MEASURING` at [seq_core.c:66](../core/seq_core.c#L66) toggles instrumentation; results are shown in the System Info page.
- Long-running work is moved *outside* the per-tick path — see [seq_core.c:547](../core/seq_core.c#L547) ("this code is outside SEQ_CORE_Tick() to save stack space!"). Pattern switching is one example; it runs in `SEQ_CORE_Handler()`, not `SEQ_CORE_Tick()`.

### Output timing, latency & jitter — full pipeline (verified 2026-06-08)

How a sequencer event reaches the wire, and where timing accuracy is won and lost. The **mechanism is source-verified** (cross-checked by an adversarial multi-agent pass over this fork); the absolute **magnitudes are inferred from structure, not yet scope-measured** on hardware — see the measurement gap at the end. Symbols are the durable anchors; line numbers drift.

**Headline:** long-term tempo is rock-solid, but *per-event output placement is quantized to ~1 ms by a software polling task* — and that quantum applies to **DIN output too**, not just USB. The µs-accurate hardware timer's resolution is discarded at the output stage. The often-assumed "high-priority clock task ⇒ tight timing" is misleading here: the task is high-*priority* but only 1 ms-*resolution*.

**1 — The clock source emits nothing.** `SEQ_BPM_Timer_Master` / `_Slave` (`seq_bpm.c`) run in a `TIM2` IRQ at `MIOS32_IRQ_PRIO_HIGHEST` and do **only** counter increments (`++bpm_tick; ++bpm_req_clk_ctr;`) — no MIDI. Master fires per internal tick (≈1.3 ms at 120 BPM / 384 ppqn; per-BPM `MIOS32_TIMER_ReInit`, period floored at 250 µs); slave is a fixed 250 µs interpolation timer with **no PLL/averaging** of incoming clock (only the *displayed* BPM is averaged). Tempo derivation is µs-accurate; emission is not driven from here.

**2 — Emission is task-bound at ~1 ms (Quantizer #1).** All generation + dispatch happen in `SEQ_TASK_MIDI()` ([app.c:830](../core/app.c#L830)). Per pass: `SEQ_CORE_Handler` → `SEQ_CORE_Tick` drains all due ticks (generating notes + queuing the 0xF8 clock via `SEQ_MIDI_ROUTER_SendMIDIClockEvent`), then `SEQ_MIDI_OUT_Handler` flushes every queued item with `timestamp ≤ bpm_tick`. An event becomes *due* at a precise µs moment but is *released* 0–1 ms later, at the next task wake. ~1 ms is a floor, not a guarantee — under load the task can wake later (catch-up logic drops missed wakeups), never sooner.

**3 — The scheduler itself is well-built.** `SEQ_MIDI_OUT` (`seq_midi_out.c`) is a sorted linked list keyed on `bpm_tick` (not wall-clock — so queued events stay in sync across tempo changes), with deterministic same-tick ordering `Clk/Tempo < CC < Note`. Pool `SEQ_MIDI_OUT_MAX_EVENTS` = **256** on this fork ([mios32_config.h:142](../mios32/mios32_config.h#L142); static `.bss` via malloc-method 3, ~4 KB; mainline default 128). Overflow **drops the new On/OnOff event silently** (`seq_midi_out_dropouts`; reserves 2 slots so an Off always fits) — a missing note, never a stuck one. Insert is O(n) (explicit "needs a better algorithm" TODO) — cost grows with queue depth inside the 1 ms budget. Per-port signed `ppqn_delay` trim exists and is enabled (`SEQ_MIDI_OUT_SUPPORT_DELAY`; the "MClk Delay" UI item, ±ms per port).

**4 — Transports diverge:**
- **DIN/UART:** `MIOS32_MIDI_SendPackage` → UART TX ring; the USART **TXE IRQ** shifts bytes at 31.25 kbaud (~320 µs/byte) asynchronously. `MIOS32_UART_MIDI_Periodic_mS` does **not** flush TX. So DIN carries **only** Quantizer #1 + byte serialization — *no* second 1 ms stage.
- **USB (primary):** USB OTG **Full-Speed**, **bulk** endpoints (so `bInterval` is ignored — *not* a polling-interval-bounded interrupt endpoint), 64-byte packets = 16 events/transfer, single TX buffer (no double-buffering), 64-package rings. TX is **not** SOF-synced (SOF class callback is `NULL`, `mios32_usb.c`); it's flushed by a *separate* 1 ms task (`MIOS32_MIDI_Periodic_mS` → `MIOS32_USB_MIDI_TxBufferHandler`, from `main.c`) **(Quantizer #2)** plus EP-completion IRQ chaining, then rides the USB **1 ms FS frame (Quantizer #3)**. A full ring busy-waits up to 10000 spins (the "MIDI Protocol TIMEOUT" stall when a host stops draining), blocking the `MUTEX_MIDIOUT`-held task.

**Net (inferred):** DIN worst-case ≈ ~1 ms + serialization; USB worst-case ≈ ~2 ms+ for an EP-idle first event (two independently-phased 1 ms task stages + the FS frame). A *continuous* USB stream collapses follow-on latency to well under 1 ms via EP-completion chaining. The USB transport is **byte-for-byte mainline MIOS32** — only `MIOS32_USB_MIDI_NUM_PORTS = 4` differs from stock.

**Structural vs. tunable:**
- ❌ Structural (not firmware-fixable): the USB FS 1 ms frame — only USB-HS escapes it, and HS is not wired up on this core.
- ✅ Tunable / real headroom: shrink **Quantizer #1** (drive `SEQ_MIDI_OUT_Handler` from a faster periodic or the tick-IRQ bottom-half); the **0xF8 clock is queued like a note** — emitting it directly from the `TIM2` IRQ is the single highest-value jitter fix; SOF-synced USB flush; larger pool / better insert; replace the busy-wait with non-blocking + an explicit drop policy.
- Practical, zero-code: route the master clock + timing-critical tracks over **DIN/TRS**, not USB — it skips Quantizers #2/#3 today.

**Measurement gap:** no scope capture of this device exists yet; the ~1 ms / ~2 ms figures are reasoned from the 1 ms task + 1 ms frame structure and bracketed by analogous-device literature (TD-3 ~0.08 ms DIN vs ~0.44 ms USB; Sound on Sound ~0.2 vs ~1.9 ms). The design-doc **§A4 HIL test plan** (tick-interval σ, MIDI-out scheduling jitter) is the right instrument and is **not yet built** — that is the empirical confirmation step.

### Per-track determinism

- Randomness used in playback must be **reproducible** given a recorded seed. Robotize uses a per-track xorshift32 with explicit `state` ([seq_random.c:70](../core/seq_random.c#L70)), not the shared `jsw_rand`.
- State of 0 is reserved as "uninitialized"; the code substitutes `0xdeadbabe` ([seq_random.c:75](../core/seq_random.c#L75)) so an uninitialized track still produces a deterministic stream.
- Robotize bar anchors store one PRNG state per bar of the loop, so retroactive "freeze" works.
- **Per-track-RNG keystone (first cut, 2026-06-19)** extended this beyond robotize: **generators** carry a per-pool-slot `u32 seed` ([seq_generator.h](../core/seq_generator.h), in `seq_generator_t`), minted fresh from the global RNG at ENGAGE then advanced by every reroll/perturb/rate-gate draw via `SEQ_RANDOM_GenRangeXorshift`; **random traversal** (Random_Dir/Step/D_S) draws from `seq_core_trk_t.random_traverse_state` ([seq_core.h](../core/seq_core.h)), minted at run-start in `SEQ_CORE_Reset`, save/restored around `SEQ_CORE_Scrub`'s out-of-band `NextStep`. Per-**slot** (not per-track) for generators so draw order is independent of pool alloc order. **Still on the global RNG (deliberately deferred to CAPTURE):** the emission-time coin-flips — step-probability ([seq_core.c](../core/seq_core.c) normal + drum + [seq_layer.c:411](../core/seq_layer.c#L411)), random-gate, humanize ([seq_humanize.c](../core/seq_humanize.c)), echo. Seeds are RAM-only (no bank-format bump yet); `SEQ_GENERATOR_SlotSet` re-mints a zero (file-loaded) seed to avoid `0xdeadbabe` aliasing. Determinism HIL: [tests/apps/seq_v4/test_rng_determinism.py](../../../../tests/apps/seq_v4/test_rng_determinism.py); seed get/set via testctrl `CMD_RNG_SEED` 0x4d.

### Retroactive CAPTURE (first cut, 2026-06-20)

The keystone's determinism made spans re-simulable; CAPTURE is the retroactive grab. All in
[seq_core.c](../core/seq_core.c); verbs in [seq_testctrl.c](../core/seq_testctrl.c).

- **The ring** — always-on, per-bar **generative frame** of the **visible** track:
  `seq_core_cap_ring[SEQ_CORE_CAP_RING_BARS]`, **`SEQ_CORE_CAP_RING_BARS=17`** (the live in-progress
  bar + **16 grabbable** completed bars; **explicit-modulo** index `% SEQ_CORE_CAP_RING_BARS`, not a
  power-of-2 mask) (main SRAM, ~12 KB), each frame = `random_traverse_state` + step/progression
  counters + the engaged generator slots (`SEQ_GENERATOR_TrackSnapshot`) + **`u32 robotize_seed`**.
  The frame is **self-contained** (carries its own robotize seed), so the re-sim no longer reads the
  16-deep `robotize_seed_snapshots` ring — that ring stays 16-deep, `& 0x0f`-indexed, and is shared
  with FREEZE (untouched). Appended in `SEQ_CORE_CaptureRingTick()` at the `ref_step==0`
  hook; reset on run-start (`SEQ_CORE_Reset`); **invalidated on visible-track switch** (bounds the
  costly generator snapshot to one track). The frame is the **pre-advance** state at the boundary
  prologue (before the body `NextStep` and before that measure's mutate, which fires the next tick).
- **`CMD_CLOCK_STEP` 0x4b** — drives N bpm-ticks of `SEQ_CORE_Tick` synchronously **while the
  transport is stopped** (`SEQ_CORE_Handler` no-ops Tick when stopped → no +4-task race; the
  prefetch gate is in `SEQ_CORE_Handler`, not the direct drive path). `mute=1`. Closes the
  keystone's traversal-trajectory HIL gap. `board.clock_step(n)`.
- **`CMD_CAPTURE_SPAN` 0x4c** — sub-op 0 = capture `[src,K,dst]`, sub-op 1 = ring query. Calls
  `SEQ_CORE_CaptureSpanReSim(src,dst,K)`: snapshot ALL live state → rewind src to the
  window-start frame (restore gen slots + `SEQ_GENERATOR_ForceRewrite` to push the loop into source,
  traverse, robotize seed **from the frame**, step/counters; `last_seen=frame->step` so the wander mutate
  fires on the natural tick; **no FIRST_CLK** so the first driven tick does a real NextStep advance/
  draw) → drive K bars **with wander on** at a measure-aligned base `B=(steps_per_measure+1)*96`,
  recording the **emitted** stream via 4 installed `SEQ_MIDI_OUT` hooks (a quantizing sink + synthetic
  BPM, reusing the MIDI-export pattern) → `SEQ_MIDI_OUT_FlushQueue()` before uninstalling → restore.
  `SEQ_GENERATOR_ReSimOnlyTrackSet(src)` gates auto-mutate to src so round-0 loopback tracks can't
  wander/corrupt their (un-snapshotted) pool slots. Refusal codes: −3 running, −4 wrong-track,
  −6 history, −8 not-whole-measure (`spm != steps_per_measure+1`), −9/−12 par/trg overflow,
  −11 arp. `board.capture_span(src,k,dst)` / `board.capture_ring_query()` (the ring-query reply
  gained a 4th byte: `max_k` = the par/trg-aware grabbable max, so the UI shows exactly what a grab
  will accept).
- **Materialize = record the EMITTED stream, NOT the output mirror.** An output-mirror snapshot
  (`CaptureToTrack`) is just BOUNCE: it loses robotize (emission-time, perturbs the *event* not the
  rendered buffer) AND traversal order (playback-order; `ResetGenerativeForBounce` forces
  `dir_mode=Forward`). The sink quantizes each emitted NoteOn to `step = (tick−B)/step_length` and
  writes the dst note/vel/gate (default gate length this cut). dst geometry inherits src (K-bar
  length, generative axis stripped, groove cleared since it's baked into the emitted positions).
- **Par-aware grabbable cap** — `SEQ_CORE_CaptureMaxK(src)` = min(ring depth−1, what the dst par/trg/
  256-step buffers hold for **src's layout** — the dst inherits it). The GP-LED thermometer, the
  "max N bars" LCD readout, and the ring-query reply (4th byte) all use it, so the lit LEDs equal
  exactly what a grab accepts (raw ring depth would over-promise). par and trg carry **independent
  instrument counts** (drum = par 16-instr / trg 1-instr) — each axis uses its own count for TrackInit
  + the overflow guard (a conflation bug once refused all drum captures). With `SEQ_PAR_MAX_BYTES=1024`
  / `SEQ_TRG_MAX_BYTES=256`, a 16-instrument drum-layout source caps at **K≤4**; a lean 1-voice melodic
  source grabs the full **16**. A true 1-voice Note-track init (clean drum-source grabs) needs Note
  par-layer typing — *not* a simple field write (`par_assignment_drum[4]` is drum-only) — still parked.
- **Physical gesture** (the first user gesture; host = **UTILITY** on the midiphy panel, all in
  `seq_ui.c`): transport STOPPED. **Hold UTILITY** → `capture_util_held` arms (GP LED row becomes the
  `CaptureMaxK` thermometer, UTILITY LED lit; `SEQ_UI_Button_Utility`). **Tap a select-row**
  (`SEQ_UI_Button_DirectTrack`) → stashes `capture_dst_track`, swallowed so the visible track / ring is
  unchanged. **Tap GP-n** (`SEQ_UI_Button_GP`) → `SEQ_CORE_CaptureSpanReSim(ring-track, dst, n)` +
  commit. Quick UTILITY tap (<`CAPTURE_UTIL_TAP_MS`=500ms, no sub-gesture) = the stock Utility page; a
  hold returns you where you were. LCD overlay "CAPTURE T<src> → T<dst>  max N bars" + result/refusal.
  `BTN_UTILITY=0x17` for testctrl button injection; the gesture path is **not** testctrl-gated (works
  in the gig `TESTCTRL=0` build).
- **Deferred:** while-playing capture (this cut is stopped-only), precise gate length, live-MIDI-in
  tape, emission coin-flips, ring persistence, the true 1-voice Note-track init. HIL:
  [test_capture_span.py](../../../../tests/apps/seq_v4/test_capture_span.py),
  [test_capture_span_gesture.py](../../../../tests/apps/seq_v4/test_capture_span_gesture.py),
  [test_clock_step.py](../../../../tests/apps/seq_v4/test_clock_step.py). Full HIL 187/187 green;
  by-hand/by-ear GO 2026-06-20.

### Persistence is versioned at the per-track level

- Bank file (`MBSEQ_B*.V4`) uses per-track extension blocks tagged with a version byte (`SEQ_FILE_B_TRK_EXT_TAG_V1 = 0x01`, `_V2 = 0x02`) — see [seq_file_b.c:45](../core/seq_file_b.c#L45). New CCs ≥ 0x80 live in the v2 ext block; the base loop `for(cc=0; cc<128; ++cc)` deliberately excludes them (TK's `seq_cc.h:166` note "For future V4 Plus (currently not stored in bank files on SD Card)").
- Old firmware reading a new bank treats ext bytes as harmless padding; new firmware writing to a too-small slot in an old-format bank degrades gracefully (skips the ext block).
- Bank file global magic numbers are *not* bumped for ext-block additions — versioning lives at the per-track tag byte.

### Measure boundaries are special

- Global `ref_step == 0` is the natural place to refresh global state (robotize loop phase, sync-to-measure, freeze-q transitions).
- `tcc->robotize_sync_to_master` realigns a track's local loop phase to the song-level measure.
- Pattern switches snap to measure boundaries unless `SynchChange` is off.

### Mutex discipline

- `MUTEX_MIDIIN_TAKE / GIVE` — gates incoming MIDI state across the MIDI task and `SEQ_CORE_*` loopback paths. See [seq_midi_in.c:716](../core/seq_midi_in.c#L716).
- `MUTEX_MIDIOUT_TAKE / GIVE` — serializes outgoing sends.
- `MUTEX_SDCARD_TAKE / GIVE` — only one task touches the SD card at a time.
- `MUTEX_LCD_TAKE / GIVE` — gates LCD refresh against UI updates.
- `MIOS32_IRQ_Disable() / Enable()` brackets multi-field track-state updates from the UI task (e.g. `SEQ_CORE_ManualTrigger` at [seq_core.c:2318](../core/seq_core.c#L2318)).

### Task boundaries

Priorities verified in [tasks.c:56-58](../mios32/tasks.c#L56) (corrected 2026-06-08 — the old note had the top two swapped):
- `SEQ_TASK_MIDI()` — **highest app priority (`tskIDLE_PRIORITY+4`)**. The task that actually emits all MIDI: sequencer tick batch / step generation (`SEQ_CORE_Handler` → `SEQ_CORE_Tick`), timestamped-scheduler drain (`SEQ_MIDI_OUT_Handler`), and CV update (`SEQ_CV_Update`), all under `MUTEX_MIDIOUT` ([app.c:830](../core/app.c#L830)); also receives MIDI / advances notestacks.
- `SEQ_TASK_Period1mS()` — priority `+2`. Pattern switching, high-prio UI LEDs, menu handler, BPM sweep, button handlers ([app.c:519](../core/app.c#L519)). Does **not** generate steps or emit MIDI.
- `SEQ_TASK_Period1mS_LowPrio()` — `+2`; UI, SD card, logging.
- `SEQ_TASK_Period1S()` — slow housekeeping.

### Trust TK's "odd" code

Per the long-running operating principle the user has adopted: odd-looking constructs in TK's code are usually intentional optimizations. The 10-bit-chunked random extraction in [seq_robotize.c:206](../core/seq_robotize.c#L206) (three independent probabilities pulled from one 32-bit draw) is the canonical example.

---

## 2. TK quirks & idioms

### Bitfield packing in `seq_cc_trk_t`

Track config aggressively packs small ranges into bitfields ([seq_cc.h:215](../core/seq_cc.h#L215)+):

- `event_mode:4`, `midi_chn:4`, `fx_midi_chn:4`
- `dir_mode:4`, `steps_replay:4`, `steps_forward:4`, `steps_jump_back:4`
- `transpose_semi:4`, `transpose_oct:4`, `morph_mode:4`, `humanize_mode:4`
- `robotize_*_probability:5` (0..31)
- `busasg.bus:2` (4 buses)

Adding a new small-range field: prefer a bitfield. Adding a new CC: place it in the right band of the CC number space (see below).

### CC number space allocation ([seq_cc.h](../core/seq_cc.h))

| Range | Purpose |
|---|---|
| 0x00–0x2f | Parameter layer constants A1..C16 (3×16 layer slots) |
| 0x30–0x38 | LFO parameters |
| 0x3d–0x3f | MIDI Bank / Program Change |
| 0x40–0x5e | Mode, direction, steps, transpose, groove, humanize, **bus assign at 0x45** |
| 0x60–0x68 | Trigger assignments (gate, accent, roll, glide, skip, random) |
| 0x70–0x76 | Echo parameters |
| 0x78–0x7b | Fx MIDI routing |
| 0x80–0x95 | Robotize (extended block — **not in base 0x00..0x7f persistence loop**) |
| 0x96–0x99 | ChordMask (strength / bus / drum-mask L+H) |
| 0x9a | Tension Workbench **GRIP** (per-track field strength) |

CCs ≥ 0x80 require the per-track ext block in `seq_file_b.c` to persist. As of the
**V3 ext-tag (2026-06-10)** the persisted range is 0x80–0x9F, so chord-mask (0x96–0x99)
and GRIP (0x9a) now survive reboot/recall. (Before V3 the range stopped at
`SEQ_FILE_B_TRK_EXT_CC_LAST = 0x95` and 0x96+ reset on reload — closed.)

### Extension-block format ([seq_file_b.c:45](../core/seq_file_b.c#L45))

Per-track block appended after the trigger layers, within each pattern slot:
- `tag:u8` (0x00 = no ext, 0x01 = v1 anchors only, 0x02 = v2 ext-CC, 0x03 = v3 ext-CC,
  0x04 = **v4 = v3 payload + generator sub-block**, FEARLESS Stage B)
- v2 body: 22 ext-CC bytes (0x80..0x95) + 64 `robotize_bar_anchors`; v3 body: **32** ext-CC
  bytes (0x80..0x9F) + the same anchors. Read path dispatches on tag; the **v2 byte-count
  is frozen separately** (`..._CC_COUNT_V2 = 22`) so old patterns still align — bumping
  `..._CC_LAST` alone would have mis-read every v2 pattern's anchors.
- **v4 body** = the v3 payload + a **fixed-stride generator sub-block**: 1 count byte +
  `SEQ_GENERATOR_PERSIST_SLOTS` (4) entry slots × **177 B** (9 header bytes — instrument,
  par_layer, engaged, range_min/max, rate, depth, contour, anchor_valid — + loop[64] +
  locks[8] + anchor[64] + mult[32]), unused entries zero-filled → 709 B sub-block, **806
  B/track** total. The stride is constant **on purpose** so `TrackRead` can index a single
  track's ext block by `slot_track × per_track_ext_size` (this field is **u16**, not u8 —
  806 truncates in a u8, caught at build by `-Woverflow`).
- `Create` allocates room from the current (v4) size; `Write` skips ext on slots too small
  to fit it. In practice existing banks had slack (par/trg layer data dominates
  `pattern_size`), so the wider block fit without re-formatting. `SEQ_FILE_B_Create` is
  **header-only** (the slot-fill loop is `#if 0`) — a created bank isn't loadable until its
  slots are written.
- **Per-pattern / bank totals (byte map).** `pattern_size` ≈ **8992 B** = 24-byte header +
  4 tracks × 2242 B (per-track = 156 B `seq_file_b_track_t` + 1024 par + 256 trg + 806 v4-ext).
  A full bank file is **~575 KB** worst case (64 × 8992 + header) but **sparse-grown** (Create
  is header-only; slots extend on write), so a lightly-used bank (~8 slots) is ~75 KB. Copying
  a session/set therefore scales with *used* slots, not capacity. *(Supersedes the design doc's
  old "≈ 6 KB/pattern" — that was the pre-v4 figure, before the generator ext-block.)*

### Ring-buffer for retroactive state capture

- `robotize_seed_snapshots[16]` ([seq_core.h:118](../core/seq_core.h#L118)) — captures live PRNG state at each measure boundary, indexed `measure_ctr & 0x0f`.
- `robotize_measure_ctr` ([seq_core.h:119](../core/seq_core.h#L119)) — monotonic, incremented at global `ref_step == 0`. Decouples loop bookkeeping from any single track's length, so polymetric tracks share the same robotize clock.

### Page-table enum invariant ([seq_ui_pages.c:32](../core/seq_ui_pages.c#L32))

> "Order of entries must be kept in sync with the seq_ui_page_t enum in seq_ui_pages.h"

Every `ui_menu_pages[]` row carries:
- `cfg_name` (used in `MBSEQ_HW.V4`)
- `short_name` (5-char menu)
- `long_name` (18-char main)
- `old_bm_index` (**never change** — used by old bookmark files)
- `init_callback`

Adding a UI page: append to both the enum and the table, leave existing `old_bm_index` values alone.

### Tension Workbench / GRAVITY field ([seq_core.c](../core/seq_core.c), [seq_ui_gravity.c](../core/seq_ui_gravity.c))

Render-stack harmony processor (design doc §8 second build, shipped 2026-06-10).
- **Processor:** `SEQ_PROCESSOR_ID_TENSION` (=2) in **slot 2** (Track 2 re-home
  2026-06-10: PITCH=0, CHORD_MASK=1, TENSION=2, LIMIT=3 — next section).
  `tension_render_range` mirrors `chord_mask_render_range` but swaps the live
  `SEQ_RANDOM` gate for a deterministic hash and feeds a computed band to the (reused)
  `chord_mask_snap`. `SEQ_CORE_TensionBandMask(gravity, bus, *zone)` builds the ladder→
  zone band (public — HIL pins it). Dispatched in BOTH render switches + the per-tick
  implicit-dirty loop, beside chord-mask.
- **Grip gate:** `grip_hash(track, instr, step, zone)` — local xorshift32 mix,
  `% 127` (renamed from `tension_grip_hash` 2026-06-19; now **shared with CHORD_MASK**,
  which uses zone `GRIP_ZONE_CHORD_MASK` 0x20, disjoint from TENSION's `{0,4,5,6}`).
  Keyed on `zone` only when `gravity > 0` (push variety); pull collapses all zones to
  class 0 (monotone — deeper pull only ADDS gripped voices). Threshold =
  `(|gravity| × GRIP) >> 6`. **The TENSION HIL suite pins `grip_hash`'s determinism;
  CHORD_MASK's determinism rides on the same code path** (a refactor here touches both).
- **State:** `seq_core_tension_gravity` (s8 global, −64..+63; config-persisted, NOT
  pattern). `SEQ_CORE_TensionGravitySet` clamps + touches field tracks (live sweep).
  GRIP = `SEQ_CC_TENSION_GRIP` 0x9a per-track → `SEQ_CORE_TensionSlotSync` arms the
  tension slot (shares chord-mask's bus + drum scope).
- **RESOLVE:** `SEQ_CORE_TensionResolve` (arm) / `…ResolveTick` (per-tick glide in the
  `SEQ_CORE_Tick` prologue, step sized to ticks-to-next-downbeat) / `…ResolveBoundary`
  (pin to 0 at `ref_step==0`) / `…ResolveCancel` (a manual turn aborts). Instant when
  stopped. **Transport STOP also lands an in-flight ramp at 0** (Boundary in the
  ChkReqStop handler, 2026-06-10) — the glide reaches 0 *before* the downbeat by
  design, and stopping in that window used to strand `tension_resolve_active`,
  silently zeroing GRAVITY at the next play's first downbeat.
- **Cockpit:** `seq_ui_gravity.c`, `SEQ_UI_PAGE_GRAVITY` (enum value 60), reached via
  GP16 from FX_SCALE. GP1 GRAVITY / GP2 SHADE (writes `seq_core_global_scale` along the
  brightness ladder indices {15,12,16,13,17,14,18}) / GP3 GRIP / GP4 track select
  (enc + button) / GP8 RESOLVE. The select row keeps its stock global track-select on
  this page — that, plus GP3, is the per-track GRIP edit path. LCD2 row0 = bipolar
  fill meter (`tension_meter`) with zone-boundary ticks; row1 = 16-char per-track
  GRIP bar.

**Gotchas:**
- `SEQ_LCD_PrintFormattedString` has **no `+` flag** — `"%+4d"` renders as literal
  "4d". Format signed values as `"%c%2d"` (sign + magnitude), the codebase idiom.
- `pattern_load` updates source par/trg but the **stopped output mirror** that
  `SEQ_PAR_Get` reads only refreshes on a render; a read right after a load can be stale
  (and a recently-touched track renders *sweep* = a partial window). Read from an
  untouched group, or force a render.
- **testctrl verbs** (seq_testctrl.c): `CMD_TENSION_SET` 0x69 (gravity ±64-biased),
  `…_BAND_GET` 0x6a (pure-function band pin), `…_GET` 0x6b, `…_RESOLVE` 0x6c,
  `CMD_PATTERN_SAVE` 0x6d. `CMD_BANK_CREATE` 0x6e exists but is **incomplete/unused**
  (`SEQ_FILE_B_Create` is header-only) — byte 0x6e is allocated; remove or fix-with-fill
  if ever needed.
- **Rig tooling:** `tests/harness/rigs.py` — `build_tension` + CLI
  (`python -m harness.rigs tension`) sets up a clean playable rig in one command (loads
  AUTOTEST baseline, routes the lead to USB2, GRIP on, GRAVITY page up).

### Track 2 — stack-resident pitch chain ([seq_core.c](../core/seq_core.c), shipped 2026-06-10)

Transpose → force-to-scale → note-limit live in the render stack (design doc §8
Track 2; plan `doc/plans/2026-06-10-pitch-chain-migration.md`). HIL 108/108.
- **Slot homes** (= stack order, the musical chain): `SEQ_CORE_PITCH_SLOT 0`
  (`SEQ_PROCESSOR_ID_PITCH`=3, transpose+FTS fused — snap-after-transpose keeps
  planing in-scale), `CHORDMASK_SLOT 1`, `TENSION_SLOT 2`, `LIMIT_SLOT 3`
  (`SEQ_PROCESSOR_ID_LIMIT`=4, the final octave-fold). FTS upstream of TENSION
  retired the Track-1 "FTS off on gripped tracks" POC rule.
- **Renderers:** `pitch_render_range` (Normal/Transpose/ChordMask playmodes; global
  transpose = notes only; CC+PB value shift in CC event mode — PC/AT deliberately
  excluded, the legacy shift wrote a don't-care byte; transposer-no-key → mirror
  RESTS, with a dedicated emission branch releasing held glides) and
  `limit_render_range` (`SEQ_CORE_Limit` parity + per-step no_fx TRG escape,
  OOB-guarded). Per-step Scale/Root via `pitch_step_scale_root` — reads the buffer
  being built, NOT `SEQ_PAR_Get`.
- **Syncs (phase-C bridge pattern):** `SEQ_CORE_PitchSlotSync` /
  `SEQ_CORE_LimitSlotSync`, armed iff non-neutral; triggered from `SEQ_CC_Set` on
  MODE / MODE_FLAGS / TRANSPOSE_* / BUSASG / EVENT_MODE / LIMIT_* / ASG_NO_FX /
  lay_const writes. **GOTCHA: any writer that bypasses `SEQ_CC_Set` (direct tcc
  writes) MUST re-run all four slot syncs** — `SEQ_FILE_T_Read` (preset import)
  and the `CaptureToSlotTrack` restore both shipped that bug before review.
- **Three emission fences** (the per-event `legacy_pitch` gate in `SEQ_CORE_Tick`
  keeps the legacy chain): Arpeggiator playmode (multi-arp runtime state), Drum
  event mode (a 0 Note byte = lay_const fallback, NOT a rest), Chord par layers
  (the byte is a chord index).
- **Dirty model:** PITCH joins the per-tick implicit dirty only when live-varying
  (`pitch_slot_live`: Transpose playmode / global transpose); every global
  scale/root/keyb-root write site carries a change-guarded `RenderDirtySetAll`
  hook. `seq_core_global_transpose_enabled` has NO runtime writer on this fork.
- **Emission carve-out:** humanize-note / LFO-note re-snap + re-fold IFF the
  mutator actually moved the note. Robotize needs nothing (walks scale degrees
  under FORCE_SCALE itself).
- **`SEQ_CORE_BakeForceScale` is DELETED** — capture = mirror copy +
  `SEQ_CC_ResetGenerativeForBounce`; capturing a planed groove is faithful (the
  bake never was). `SEQ_CORE_ProcessorBounce` untangles pitch/tension/limit tcc.
- **HIL:** `test_pitch_chain.py` (12 pins incl. planing-in-scale, FTS+GRIP push
  surviving to emission, capture-planed-groove); `test_capture_force_scale.py`
  reshaped — the mirror now holds the SNAPPED pitch (precondition inverted).
  **Test gotchas:** AUTOTEST A3 ships with trkmode HOLD set (clear MODE_FLAGS for
  transposer pins — a held transposer keeps the last released key by design);
  standalone `Board()` scripts run on the USER session, not AUTOTEST (pytest's
  conftest swaps and restores it).

### Two-pass robotize at runtime ([seq_core.c:1227 & :1423](../core/seq_core.c#L1227))

`SEQ_ROBOTIZE_Event` is called twice per step. Any future bounce-style capture must replicate this or it'll be audibly faithful only to one of the two passes.

### Scale-walking helpers

`SEQ_SCALE_WalkScale()` and `SEQ_SCALE_PrevNoteInScale()` in [seq_scale.c](../core/seq_scale.c) — needed when `FORCE_SCALE` is on so robotize moves in scale degrees, not semitones. Range is halved in scale mode to avoid clamp-induced clustering (see [seq_robotize.c:255](../core/seq_robotize.c#L255) commentary).

### Glide note tracking

`glide_notes[4]` ([seq_core.h:97](../core/seq_core.h#L97)) — up to 4 simultaneous glide destinations; allows polyphonic legato.

### Sustain has three flavors

- `SUSTAINED` (sustain CC / pedal)
- `STRETCHED_GL` (gatelength extended)
- `ROBOSUSTAINED` (robotizer-asserted hold)

All three cleared in `SEQ_CORE_PlayOffEvents()` at [seq_core.c:601](../core/seq_core.c#L601).

### Layer mute shadowing

`layer_muted` (persistent) / `layer_muted_from_midi` / `layer_muted_from_midi_next` ([seq_core.h:106](../core/seq_core.h#L106)) — incoming MIDI can toggle layer mutes with proper step-boundary quantization.

### Loopback / external-bus reentrance

`last_bus_received_from_loopback` ([seq_midi_in.c:162](../core/seq_midi_in.c#L162)) — bitmask; if the bus's previous event came from loopback and the new one is external, `SEQ_MIDI_IN_ResetSingleTransArpStacks(bus)` clears stale transposer/arp notestacks so the two sources don't cross-contaminate.

### Page filing convention

- `seq_X.c` / `.h` — engine modules (`seq_core`, `seq_layer`, `seq_trg`, `seq_cc`)
- `seq_X.c` (no `ui` prefix) — fx engines (`seq_echo`, `seq_humanize`, `seq_robotize`, `seq_lfo`, `seq_morph`, `seq_groove`)
- `seq_ui_X.c` — UI pages
- `seq_file_X.c` — persistence (`seq_file_b` = bank, `seq_file_s` = song, `seq_file_gc` = global config, `seq_file_m` = mixer, `seq_file_bm` = bookmarks)

### "Stack space" budget is real

Pattern switching is hoisted out of `SEQ_CORE_Tick()` explicitly to save stack. Recursive helpers are rare; large local arrays are file-static instead of stack-local. Adding deep call stacks in the tick path is a code smell.

### NOTE patterns (informational, not action items)

[seq_file.c](../core/seq_file.c) has multiple `// NOTE — before accessing the SD Card, the upper level function should ...` comments. These are reminders about contract, not TODOs.

---

## 3. Bus tracks — deep-dive (corrected model)

**Bus tracks are not a separate track type.** They are a routing mechanism through 4 shared MIDI input buses. A track configured as a loopback can feed notes (and CCs) into a bus; another track set to Transpose or Arpeggiator playmode and assigned to that bus reads its control source from the bus.

So when the user says "set a track as a bus track to control another track," what they mean concretely is: loopback-output track → bus N → another track in Transpose/Arp mode listening to bus N.

### The 4 buses

`SEQ_MIDI_IN_NUM_BUSSES = 4`. Each bus owns three notestacks ([seq_midi_in.c:63](../core/seq_midi_in.c#L63)):

```c
#define BUS_NOTESTACK_NUM             3
#define BUS_NOTESTACK_TRANSPOSER      0
#define BUS_NOTESTACK_ARP_SORTED      1
#define BUS_NOTESTACK_ARP_UNSORTED    2
```

Notestack arrays: `bus_notestack[SEQ_MIDI_IN_NUM_BUSSES][BUS_NOTESTACK_NUM]` at [seq_midi_in.c:141](../core/seq_midi_in.c#L141).

### Inbound: who writes to a bus?

`SEQ_MIDI_IN_BusReceive(u8 bus, mios32_midi_package_t midi_package, u8 from_loopback_port)` ([seq_midi_in.c:699](../core/seq_midi_in.c#L699)):

- Called from **two** places: the MIDI input handler (`from_loopback_port = 0`) when an external MIDI port is configured as Bus1..Bus4, and `SEQ_CORE_*` loopback emission (`from_loopback_port = 1`) when a track's MIDI output port is one of the internal loopback ports (Bus1..Bus4 = MIDI port codes 0x70..0x73).
- On a source switch (loopback → external or vice versa), it calls `SEQ_MIDI_IN_ResetSingleTransArpStacks(bus)` to flush stale notestack content.
- It also handles CCs over loopback by routing them to `SEQ_CC_MIDI_Set(chn, cc, value)` — that's how loopback tracks can NRPN-style modify other tracks' parameters. Important: this is *the* mechanism behind "loopback can change CCs on bus-assigned tracks."

### Outbound: who reads from a bus?

Track config field: `tcc->busasg.bus` (2 bits, 0..3) — stored via `SEQ_CC_BUSASG = 0x45` ([seq_cc.h:105](../core/seq_cc.h#L105)).

Track type at [seq_core.h:132](../core/seq_core.h#L132):
```c
typedef union {
  u8 ALL;
  struct {
    u8 bus:2;
  };
} seq_core_busasg_t;
```

Reads happen in `SEQ_CORE_Transpose()`:

- [seq_core.c:1961](../core/seq_core.c#L1961): `SEQ_MIDI_IN_TransposerNoteGet(tcc->busasg.bus, tcc->trkmode_flags.HOLD, tcc->trkmode_flags.FIRST_NOTE)`
- [seq_core.c:1981](../core/seq_core.c#L1981): `SEQ_MIDI_IN_ArpNoteGet(tcc->busasg.bus, tcc->trkmode_flags.HOLD, !tcc->trkmode_flags.UNSORTED, key_num)`

These functions read from the bus's notestack. The result is then applied to the track's sequenced notes as transpose offset or arp note selection.

### Step Trigger Mode ([seq_core.c:2359](../core/seq_core.c#L2359))

```c
s32 SEQ_CORE_StepTriggerReq(u8 bus)
{
  for(track=0; track<SEQ_CORE_NUM_TRACKS; ++track, ++t, ++tcc) {
    if( tcc->busasg.bus == bus ) {
      t->state.TRIGGER_NEXT_STEP_REQ = 1;
    }
  }
}
```

When a transposer in `STEP_TRG` mode receives a new keypress on its bus, it advances every track listening to that bus by exactly one step.

### Global Root/Scale via loopback (V4.097)

Per CHANGELOG.txt V4.097: a loopback track assigned to Bus1..4 can drive the global Root or Scale parameter, enabling Force-To-Scale to be controlled at song level from within a sequence.

### UI page ([seq_ui_trkmode.c](../core/seq_ui_trkmode.c))

- `ITEM_BUS` index 2 ([seq_ui_trkmode.c:31](../core/seq_ui_trkmode.c#L31))
- Encoder: `SEQ_UI_CC_Inc(SEQ_CC_BUSASG, 0, 3, incrementer)` ([seq_ui_trkmode.c:141](../core/seq_ui_trkmode.c#L141))
- Display: 1-indexed for users (`SEQ_CC_Get(...) + 1`) ([seq_ui_trkmode.c:303](../core/seq_ui_trkmode.c#L303))

### Interactions / constraints

- `tcc->busasg.bus` is only meaningful when track playmode is **Transpose** or **Arpeggiator**. In Normal or Off, the field is inert.
- Loopback tracks affected by mute and Fx like any other track. Their output enters the bus regardless of muted layers because the bus receive happens at MIDI emission time.
- Limit Fx applies *after* the transpose/arp pass ([seq_core.c:1347](../core/seq_core.c#L1347)).
- BLM Live/Jam page also writes to buses — search for `SEQ_MIDI_IN_NUM_BUSSES` to see all writers.

### Capture / bounce (unified 2026-05-30 — design doc §9 "Bounce unification")

The fork captures the **computed output** of a track, never an emission tape. The old
`seq_capture.c` ring-buffer tape (lossy; raced; 16 KB; hot-path tap in
`SEQ_CORE_ScheduleEvent`) was **deleted**. One shared primitive plus two verbs, all in
[seq_core.c](../core/seq_core.c):

- `SEQ_CORE_CaptureTrackOutput(track, par_dst, trg_dst)` — forces a full *quiet* render
  then snapshots `OutputActive` par/trg into caller buffers. The forced render is what
  makes capture **sweep-safe**: in sweep regime only a ~4-step slice of `OutputActive`
  is fresh, so any capture that reads it directly must force a whole-buffer render
  first (the old in-place `SEQ_CORE_ProcessorBounce` lacked this — fixed by routing
  through the primitive).
- `SEQ_CORE_CaptureToSlot(src_track, dst_group, dst_bank, dst_pattern)` — capture →
  pattern slot. (`dst_group` is vestigial — the write source-group is derived from
  `src_track`; kept for the testctrl payload shape.)
- `SEQ_CORE_CaptureToTrack(src_track, dst_track)` — capture → another track in the
  current pattern (RAM only). dst **inherits the source's full `0x00..0x7f` CC config +
  geometry** (mirroring the `PASTE_CLR_ALL` branch of `PASTE_Track` in `seq_ui_util.c`)
  before the raw layer copy. Geometry lives in `seq_par.c`/`seq_trg.c`, **not**
  `seq_cc_trk`, so `SEQ_PAR/TRG_TrackInit` with the src counts is still needed (a raw copy
  across mismatched geometry reads back as garbage). The full-CC inherit (was **lower-48
  only** until 2026-06-07) is what carries length/clock/groove/trigger-assignments so the
  frozen copy reproduces what was heard — see §9 "Freeze faithfulness".
- `SEQ_CORE_CaptureToSlotTrack(src_track, dst_track, dst_bank, dst_pattern)` — capture
  → one track of a pattern slot, **persisted to SD** (the PATTERN-hold gesture's verb,
  and the only one on a UI gesture). Read-modify-write of the slot file: `PatternRead`
  (remix_map=0) into the dst group → replace `dst_track` → `PatternWrite` → restore the
  dst group's live RAM (src captured first in case it shares the dst group). `seq_pattern[]`
  is never touched and **no group is auto-loaded by the verb itself** — it only lands on SD.
  (The PATTERN-hold gesture that calls it decides the load separately: a same-group destination
  → no load, the source keeps playing; a cross-group destination → loads the *destination
  group's own bank*, bar-aligned via `SEQ_CORE_ManualSynchToMeasure` — see design §9.) Unlike
  RAM-only `CaptureToTrack`, it survives switching the dst group's pattern away.

Because a track can be both a source (via loopback) and a sink (via `tcc->busasg`), the
capture point matters: these verbs capture the **post-processor render** (`OutputActive`),
which is *before* emission — so loopback/echo/global-transpose applied at emission are
**not** in the capture. That's intentional: the captured pattern is the editable
computed loop, not the literal performed MIDI.

The companion concern is destination-side modulation, split across **two axes**
(2026-06-07). The **generation axis** (generators, randomness, robotize, echo, LFO,
direction, transpose, per-step Probability/Nth, bus mode) would re-modulate or re-roll the
frozen tape, so `SEQ_CC_ResetGenerativeForBounce()` ([seq_cc.c](../core/seq_cc.c)) resets it
on the destination after the output is written. The **shaping axis** is DETERMINISTIC —
groove especially: per-step swing/velocity/length keyed to step position, applied at
emission ([seq_core.c:1869](../core/seq_core.c#L1869), [:2113](../core/seq_core.c#L2113)),
never baked into `OutputActive` — so re-applying its CC reproduces the heard sound exactly
(incl. groove's negative timing delays, which can't be baked into per-step delay params).
That axis is kept faithful by one of **two mechanisms**:
- **PRESERVE the CC** when re-applying it on playback reproduces the heard sound exactly —
  **groove** (per-step swing/velocity/length; its negative timing delays can't be baked into
  step params anyway). `ResetGenerativeForBounce` leaves groove (style+value) alone.
- **BAKE into the captured notes** when preserving the flag would couple the frozen copy to
  *mutable global state* — **FORCE_SCALE** (2026-06-07). The snap was emission-time and
  `SEQ_CORE_BakeForceScale(track)` reproduced `noteLimit(forceScale(transpose(raw)))` at
  capture. **SUPERSEDED 2026-06-10 (Track 2): the bake is DELETED.** The pitch chain renders
  into the output mirror, so the capture copy holds the heard pitch by construction — same
  observable contract (frozen copy = heard pitches, FORCE_SCALE reset), no bake code. The
  bake-vs-preserve mechanism choice above remains the live rule for any FUTURE emission-time
  effect (which the §3 born-as-processors rule says not to write in the first place).
  - **GOTCHA (still live for any "operate on just-captured par data" code):** `SEQ_PAR_Set`
    writes the source buffer `seq_par_layer_value` ([seq_par.c:258](../core/seq_par.c#L258))
    but `SEQ_PAR_Get` reads the double-buffered **output mirror**
    ([:285](../core/seq_par.c#L285)). A freshly-written capture destination has the captured
    notes ONLY in the source buffer (the mirror is stale until the next render+flip) — read
    and write `seq_par_layer_value` directly and mark the track render-dirty. Such code must do the
    same, not call `SEQ_PAR_Get`.
  - (The old "bake reproduces the post-snap note limit" gotcha died with the bake —
    the LIMIT processor folds in the mirror, slot 3, Track 2.)

`transpose` (for non-force-scaled tracks), `echo`/`LFO`/`direction` are emission-time
deterministic too and are the remaining shaping-axis candidates. When you add a new `SEQ_CC_*`,
classify it: generation → reset here; deterministic shaping → preserve-as-CC or bake-into-notes
(bake when the effect reads mutable global state). See §9 "Freeze faithfulness" in the design doc.

Source-state preservation across `CaptureToSlot` uses an **in-RAM snapshot** of
`seq_cc_trk[src_track]` + layer/trigger buffers + pattern name + `play_section`,
restored after `PatternWrite`. The earlier SD-based round-trip (`SEQ_PATTERN_Save` →
mutate → `SEQ_PATTERN_Load`) had two failure modes: a non-zero `seq_pattern_remix_map`
silently skipped the source track on reload (leaving the sanitized RAM in place), and
`play_section` lives in `seq_core_trk_t` runtime state — never in the pattern file — so
the SD reload couldn't restore it at all. The snapshot path makes source byte-identical
after the capture regardless.

### Track-grain load (the pull) + track undo (RECOMBINE bundle, shipped + by-ear GO 2026-06-12)

The mirror of the capture verbs: load ONE stored track section into an arbitrary
live track. Fills the missing grain cell (track-save / group-save / group-load
existed; track-load didn't).

- **`SEQ_FILE_B_TrackRead(bank, pattern, slot_track, dst_track)`**
  ([seq_file_b.c](../core/seq_file_b.c)) — streams a single section into any live
  track; adapted from `PatternRead`'s remix-skip / load / ext-block arms (per-track
  geometry walk, no fixed stride). Contract: the fixed section header is read into
  locals and status-checked **before the first live write** — every pre-write
  failure (bad bank/pattern/section, `slot_track >= num_tracks`, header error)
  leaves the live track untouched; new error `SEQ_FILE_B_ERR_INVALID_TRACK -136`.
  Unlike `PatternRead`, the bulk par/trg reads are **status-bearing** (PatternRead
  silently swallows `FILE_Read*` failures — inherited mainline flaw, not fixed
  there). The optional ext phase uses a separate `ext_status` and degrades to
  "loaded without ext"; the indexed ext read additionally requires
  `tag == first_tag` (the stride came from track 0's tag — random-access indexing
  makes a stride/tag disagreement possible where PatternRead's sequential read
  can't). NOTE: the `slot_track >= num_tracks` refusal is not a reliable
  unwritten-slot guard — `SEQ_FILE_B_Create`'s slot zero-fill is `#if 0`'d, so
  sparse banks (testctrl `bank_create` + selective saves) hold undefined FatFs gap
  data; stock `SEQ_FILE_Format` writes all 64 slots.
- **`SEQ_CORE_LoadTrackFromSlot(dst_track, bank, pattern, slot_track)`**
  ([seq_core.c](../core/seq_core.c), by the capture cluster) — the verb. Arms the
  track undo, then runs the proven group-change recipe around the read:
  `SEQ_CORE_AddForwardDelay(margin)` (only while running) + `MUTEX_SDCARD` +
  `portENTER_CRITICAL()` (without it, the mid-read CC replay arms the sweep regime
  and ticks emit the half-loaded source for the whole SD window). On a post-write
  `ERR_READ` the verb is **transactional**: it auto-restores the armed undo victim.
  Then the group-load side-effect fan translated per-track (the §3.4 census):
  `SEQ_CORE_CancelSustainedNotes`, **new** `SEQ_LAYER_ResetLatchedValuesTrack`
  (mainline reset is all-16-tracks only), `SEQ_LAYER_SendPCBankValues` under
  `MUTEX_MIDIOUT`, the `UNMUTE_ON_PATTERN_CHANGE` bit; **RATOPC is subsumed** by
  an unconditional `ManualSynchToMeasure(1 << dst)` (an immediate reset mid-bar
  would drop the track off-phase; SYNC_MEASURE delivers the intent on the bar);
  mixer coupling skipped; `seq_pattern[]` never touched (a pull is a transfusion,
  not a switch). Finally a **forced full quiet render** (`touched_ms=0; dirty=1;
  RenderTrack`) — `RenderDirtySet` alone can be consumed by a sweep-regime tick
  that refreshed only a window, and the mirror is the emission source.
- **Track undo** — one-deep global victim snapshot in CCM (~1.65 KB: geometry,
  name[81], full CC image 0x00..0x9f, robotize anchors, par/trg sources,
  play_section; `kind` field reserves an SD-slot-victim variant for a future
  push-side arm). `SnapLive` armed inside every pull; `Restore` is one-shot,
  write phase under `portENTER_CRITICAL`, then the same external fan rows
  (latch reset + PC/bank send — without them the rig stays on the pulled track's
  program after an undo) + forced render + bar-aligned drop. CCM 52.9 → 54.4/64 KB.
- **Gestures** ([seq_ui.c](../core/seq_ui.c)) — the pull intercepts live beside
  the PATTERN-hold capture intercepts (same `button_state` stuck-bit maintenance):
  hold select-row = destination (its own press/release flows through stock, so
  release-select still fires and the cursor follows the transfusion); held +
  other select = source column (shadows stock multi-track chord-select while
  held); GP1-8 letter, GP9-16 commit; LCD overlay gated on the first aim input.
  `SEQ_UI_Button_Pattern`'s press disarms a live pull hold (its select-row
  intercept would otherwise eat the release → phantom pulls). **SELECT+CLEAR =
  track undo** (user-picked; every shipped hwcfg maps `BUTTON_UNDO 0 0`); it
  never falls through to a destructive clear. The unmapped-UNDO-button handler
  also prefers the track undo (for hwcfgs that map it); known arbitration gap:
  an armed-but-unconsumed pull victim wins over a copy/paste edit made after it.
- **testctrl**: `TRACK_LOAD 0x6f`, `TRACK_UNDO 0x70`, `TRACK_UNDO_QUERY 0x71`,
  `SESSION_CREATE 0x72` (makes /SESSIONS/<name>, arms the async format; host
  polls `SESSION_NAME_GET`, then waits ~6 s for `load_sd_content`'s SD I/O).
  Host: `Board.track_load/track_undo/track_undo_query/session_create`; rigs:
  `recombine` (builds the PULLJAM jam session). Pins: `test_track_load.py` (5),
  `test_pull_gesture.py` (4).

### FEARLESS SWITCHING — auto-writeback + gen-state + CHECKPOINT/REVERT (the save-model inversion, 2026-06-13)

The save model is inverted: the working state always persists; protection is the
explicit CHECKPOINT/REVERT act (design §9 2026-06-11, §10 forks resolved). Shipped
in three stages; by-ear hard GO 2026-06-13, HIL 135/135.

- **Dirty model (Stage A).** `u8 seq_pattern_dirty` (bit/group) set by
  `SEQ_PATTERN_DirtySetTrack` from the source-write chokepoints (`SEQ_PAR_Set`,
  `SEQ_TRG_Set`/`Set8`, `SEQ_CC_Set` — the render mirror never passes through
  them, so per-tick rendering can't false-dirty). A per-group `pattern_loaded`
  gate suppresses boot-init debris (CC defaults run through the chokepoints
  before the first session load). **Bypass writers** that memcpy source directly
  (preset import, clears, undo restores) must call `DirtySetTrack` explicitly —
  the by-ear pass found this class (heard, then discarded on switch).
- **Writeback hook.** `SEQ_PATTERN_WritebackIfDirty(group)` → `SEQ_PATTERN_Save`
  into the group's working slot, fired before any load at the two switch points:
  `SEQ_PATTERN_Handler` (the serviced/synched switch, inside the SDCARD-mutex +
  critical section) and `SEQ_PATTERN_Change`'s immediate branch; `WritebackAllDirty`
  on the session-switch path. `seq_core_pattern_switch_margin_ms` widened 50→**100**
  to cover the added write within the forward-delay window. The long-standing
  stall-race TODO was **retired with rationale, not built** (per-group requests:
  an overwrite loses only an intermediate target, never the writeback decision).
- **Gen-state (Stage B).** Carried by the v4 ext tag (above). `SEQ_GENERATOR_SlotSet`
  re-seeds a pool slot wholesale (no `Engage` → no undo re-snapshot, no
  ForceRewrite) so the loop resumes ENGAGED at the next wrap; `TrackClear` first.
  Track-undo (`track_undo.gen[4]`, CCM) and capture-trample (`slottrk_gen_snap`,
  main RAM) carry gens too; a capture is a freeze (generator-less by construction).
- **CHECKPOINT/REVERT anchor (Stage C).** The anchor is an internal fifth "bank"
  `MBSEQ_AN.V4` (8.3 base ≤8 chars — FatFs `_USE_LFN=0`), reached by the
  **sentinel** `SEQ_FILE_B_ANCHOR_BANK` (`0xfe`) + a parallel `seq_file_anc_info`
  slot, resolved by `SEQ_FILE_B_InfoPtr`/`BuildPath` at the five B-file entry
  points. The sentinel is **outside** `0..NUM_BANKS-1`, so every load/unload/save
  loop skips it (never auto-loaded, not navigable, untouched by session
  writeback) — and there's no `seq_pattern[4]` OOB that a `NUM_BANKS` bump would
  cause. `SEQ_PATTERN_Checkpoint` lazy-creates the file and writes groups 0..3
  **ascending** (a fresh 34-byte-header file extends contiguously — no
  seek-past-EOF gap) via `PatternWrite` (gen state rides the v4 tag); it reads
  live only, leaving dirty untouched. `SEQ_PATTERN_Revert` re-opens the anchor
  against the **current session** (the file is per-session; re-open avoids a
  stale cross-session read), then reads the 4 records via `PatternRead` **directly**
  (not `SEQ_PATTERN_Load` — that sets `seq_pattern[group]` to the source bank,
  which would repoint the live group at the anchor), runs the
  `LoadTrackFromSlot` fan ×16 tracks (forced quiet render + `CancelSustainedNotes`
  + latch reset + PC/bank send + `ManualSynchToMeasure(0xffff)`), and sets every
  group dirty + loaded **last** (the inversion: the next switch writes the
  reverted state into the working slot). Refuses cleanly when no anchor exists.
  *Accepted POC cost:* a mid-op SD failure can leave a partial anchor (parity with
  the working-slot writeback's power-loss exposure); atomic temp+rename is the fix.
- **Gesture** ([seq_ui.c](../core/seq_ui.c) `SEQ_UI_Button_Bookmark`):
  **SELECT+BOOKMARK tap = CHECKPOINT, hold ≥1 s = REVERT** (`MIOS32_TIMESTAMP`
  ms-accurate; armed only if SELECT is down at BOOKMARK press; measured at
  release; swallows press+release so the bookmarks view never flips). The
  destructive verb gets the deliberate hold — mirrors SELECT+CLEAR=undo; midiphy
  maps SAVE/UNDO to no key.
- **testctrl**: `CHECKPOINT 0x77`, `REVERT 0x78`, `ANCHOR_PRESENT 0x79` (reply
  `[ok/present, status]`; REVERT status `0x03` = clean no-anchor refuse, distinct
  from an I/O fail). Stage A added `DIRTY_QUERY 0x73`/`DIRTY_SET 0x74`/`PATTERN_CHANGE
  0x75`; Stage B `GENERATOR_LOCK_SET 0x76` + a `with_anchor` flag on `GENERATOR_QUERY`.
  Host: `Board.checkpoint/revert/anchor_present` (`revert()` returns False only on
  no-anchor, raises on a real I/O fail). Pins: `test_fearless_switching.py` (8),
  `test_genstate_v4.py` (6), `test_fearless_checkpoint.py` (3); the gen-faithful
  pins use the V4-sized `genv4` session, the refuse pin a never-checkpointed `NOANC`.

### PHRASES — the snapshot library ("a set is a path", 2026-06-13/14)

Phrase mode promoted to *the* scene system (design §9 2026-06-11/13/14). A phrase is a
whole-organism committed snapshot — capture/recall GENERALIZE CHECKPOINT/REVERT from the one
anchor to N named slots. By-ear GO 2026-06-13 (Stage A/B) + 2026-06-14 (Stage B-rest); HIL 148.

- **Storage.** Second sentinel bank `SEQ_FILE_B_PHRASE_BANK 0xfd` → `MBSEQ_PH.V4`
  (`seq_file_phr_info`, resolved by `SEQ_FILE_B_InfoPtr`/`BuildPath`), a 64-pattern bank holding
  `SEQ_FILE_B_NUM_PHRASES`=16 phrases × 4 group-records (phrase N → patterns `4N..4N+3`). Like the
  anchor it's **outside** `0..NUM_BANKS-1` (never auto-loaded/navigable, survives session writeback).
- **Capture/recall.** `SEQ_PATTERN_PhraseCapture/Recall` parameterize the FEARLESS
  `SnapshotWrite`/`SnapshotRead` halves with `(bank, base_pattern=4N)`. Recall passes
  `writeback_dirty_first=1` (never-lose-work: a live nudge writes back to the working slot first) and
  **phrases stay IMMUTABLE** (recall restores the pristine snapshot). Two-face recall = the global
  FREEZE switch (frozen tape vs living posture), not a per-recall clear.
- **Occupancy (cross-session).** `phrase_present_mask` (u16) re-seeded on session load by
  `SEQ_PATTERN_ProbePhrasesOnLoad` → `SEQ_FILE_B_PhraseOccupancyProbe` (probe-by-content: reads each
  phrase base header, occupied iff `num_tracks∈[1,4]`; out-of-order-capture gaps carry an EMPTY
  marker `SEQ_FILE_B_PatternWriteEmpty` so `f_lseek` garbage can't false-light). `last_recalled_phrase`
  (s8, −1=none) = the "current" waypoint; **set by recall AND capture**.
- **Drift signal (Stage B-rest).** `phrase_drift` (u8/group, seq_pattern.c) = "deliberately edited
  since the last recall/capture" — the clean signal `seq_pattern_dirty` can't be (recall's inversion
  ORs it). Set at the same `SEQ_PATTERN_DirtySetTrack` chokepoint, **gated** to exclude the generator's
  ambient auto-mutate via `seq_generator_in_automutate` (set around `SEQ_GENERATOR_Tick`'s auto-mutate
  write; `seq_pattern_dirty` still sets for writeback). Cleared at the recall/capture tails (after
  CC-replay), `ProbePhrasesOnLoad`, and `PhraseResetState`. `SEQ_PATTERN_PhraseDrifted()` drives the
  PHRASE-view drift LED (current waypoint winks amber↔green on `ui_cursor_flash` when drifted).
- **Naming (Stage B-rest).** `seq_phrase_name[16][21]` (RAM, space-padded; blank ⇒ UI shows the slot
  number) persisted FREE in the base (group-0) record name field — `SEQ_FILE_B_PhraseWriteName`
  (20-byte write, no format change), re-seeded by the occupancy probe (now fills a names array).
  `SEQ_PATTERN_PhraseName` (edit-in-place ptr) / `PhraseNameCommit` (rename-without-recapture).
  Entry: stock `SEQ_UI_KeyPad_*` in a global modal `phrase_name_edit` over the PHRASE view (keypad on
  GP/step row + encoders, LCD gesture-overlay; waypoints stay on the select row). Provisional gesture:
  hold-capture opens the namer; GP16/EXIT save. Cleared on harness reset (`SEQ_UI_GestureStateReset`).
- **PHRASE view** ([seq_ui.c](../core/seq_ui.c) `SEQ_UI_SEL_VIEW_PHRASE`): select-row GP tap = recall,
  hold ≥`PHRASE_CAPTURE_HOLD_MS`(1s) = capture; LED nav-map green=occupied / amber=current / wink=drift.
- **testctrl**: `PHRASE_CAPTURE 0x7a`, `PHRASE_RECALL 0x7b` (status `0x03`=empty refuse), `PHRASE_PRESENT
  0x7c`, `FREEZE_SET 0x7e`, `PHRASE_META 0x7f` (sub-ops 0=drift query / 1=name get / 2=name set / 3=name
  commit — 0x7f is the last free 7-bit opcode; `PAYLOAD_BUF_MAX` 16→24 for a 20-char name). Host:
  `Board.phrase_capture/recall/present/drift/name_get/name_set/name_commit`. Pins: `test_phrases.py` (12),
  V4-sized `genv4` session.

### POSTURE-MORPH (Loop A) — per-group posture interpolation (2026-06-16)

By-ear GO 2026-06-16; HIL 159 (10 morph pins). Design §10 "Phrase morphing". Glide ONE focused group's
posture from its LIVE state (pos 0 = true pass-through) toward a target phrase's same-group slice (pos
`PHRASE_MORPH_MAX`=16): `live = A + pos/MAX·(B−A)` per ext CC, applied per-measure.

- **What morphs:** the **ext-CC posture block 0x80..0x9f only** (robotize mask/density/probabilities,
  chord-mask strength, tension GRIP). NOT transpose/groove/length (main `cc[128]` array) or generator
  dials (V4 gen sub-block) — those are follow-ons (design §10; user wants transpose+groove next).
- **Engine** ([seq_pattern.c](../core/seq_pattern.c)): `SEQ_PATTERN_PhraseMorph{Arm,Set,Tick,Cancel,
  Target,Value}` + static `phrase_morph_apply`. State ~260 B .bss: `phrase_morph_{target=0xff disarmed,
  group,pos,dirty}` + `phrase_morph_a/b[4][32]` (A snapshotted from live at arm = reversible; B read once
  from disk). Endpoint reader `SEQ_FILE_B_PhraseReadCCs(bank,pattern,slot_track,cc_out)` — read-only
  ext-CC subset of `SEQ_FILE_B_TrackRead` (no live writes). Driven per-measure at the `ref_step==0`
  boundary (`SEQ_PATTERN_PhraseMorphTick`, beside `TensionResolveBoundary` in seq_core.c). `phrase_morph_apply`
  skips a CC whose value is unchanged (true pass-through at 0; also skips the unimplemented 0x9b..0x9f).
- **Gesture** ([seq_ui.c](../core/seq_ui.c)): SELECT+tap a waypoint = arm (toward that phrase, focused
  group); re-tap same slot = disarm. **Datawheel** = fine throw; **GP row** = 16-seg coarse bar + LED
  thermometer. Controls gate on `ui_page == morph_armed_page` (the page armed on) — robust to
  `simplified_antilog_frontpanel`. Released whenever the focused group's live CCs are replaced out-of-band:
  `SnapshotRead` (recall/revert) → `PhraseMorphCancel`; `SEQ_PATTERN_Load`, `SEQ_CORE_LoadTrackFromSlot`
  (pull), `SEQ_CORE_TrackUndoRestore` (UNDO) → `PhraseMorphInvalidateGroup(group)`; `ProbePhrasesOnLoad`
  (session load) → cancel. Morph counts as drift; disarm is leave-as-live.
- **testctrl** `CMD_PHRASE_MORPH 0x4f` (sub-ops 0=arm/1=set/2=query). **NB:** the 0x7a-0x7f phrase cluster
  was full but the opcode space is NOT exhausted (~71 free) — the "0x7f last free" note above means "top
  of that cluster", not the table. Host: `Board.phrase_morph_{arm,set,query}`. Pins: `test_phrase_morph.py` (10).

### Phrase-recall landing feel + the interrupts-on timing fix (2026-06-16)

- **Modes.** `seq_core_options.RECALL_SEAMLESS` (OPT menu "Phrase Recall lands Seamless", persisted
  `RecallSeamless` in MBSEQ_C.V4): 0=**QUANTIZE** (keep bar-aligned restart) / 1=**SEAMLESS** (no re-phase).
  Both drop the immediate sustain-cancel (the switch *click*). Implemented via `SEQ_PATTERN_SnapshotRead`
  land-flags `SEQ_SNAPSHOT_NO_CANCEL` / `_NO_RESYNC`; `PhraseRecall` sets them from running + the option;
  REVERT / stopped recall pass 0 (immediate hard restore, unchanged).
- **Timing-glitch fix (platform).** Phrase recall read the 4-group snapshot inside `portENTER_CRITICAL`
  (interrupts OFF for the whole multi-ms SD read). Recall runs in `TASK_Hooks`; emission/clock in
  higher-priority `TASK_MIDI` — interrupts-off blocked `TASK_MIDI` mid-bar = an audible groove stall. Fix:
  read with **interrupts ON** (drop the critical section in `SnapshotRead`, keep `MUTEX_SDCARD`), mirroring
  `SEQ_PATTERN_Load` (the clean pattern-change path) so `TASK_MIDI` keeps emitting through the read. The
  *true* deferred clip-launch ("nothing until the bar") is deferred — needs in-tick SD (mutex-nesting,
  hang risk) or a ~tens-of-KB scratch buffer (RAM); see design §10.

---

## 4. Feature catalog by version

Per-version highlights. Bugfix-only releases compressed to one line.

### V4.098
- Phrase Mode pattern changes start at step 1 when Pattern Change Sync enabled.
- LFO Clock Divider (sync LFO to track clock divider; GP8).
- LFO inverted waveforms (iSin, iTri, iSaw).
- Edit Layer Nth1/Nth2 now only meaningful ranges.
- Option 15/34: "ALL allows relative changes/ramps".
- Note C-2 (0) playable via lower-octave transpose.
- Fixes: TPD+MIDI Section Control crash, Ctrl-layer events, CC humanize Fx, auto-select-unmuted on Antilog.

### V4.097
- Morph page GP6: store morphed values into original positions.
- Jam MIDI Auto Track Selection: track switches on incoming MIDI channel (1s cooldown).
- Edit page Root/Scale display at upper-right.
- **Loopback Bus Root/Scale Control** (bus track expansion).
- CV page clock rates: 0.063/0.125/0.25/0.5 PPQN + Stop/Start + pulse fns.
- DUPL Fx "First Channel" correct in alt/random modes.

### V4.096
- V4+ CV expansion to 32 outputs (4 AOUT modules chainable); AOUT renamed CV1..CV4.
- Utility GP11 → CV Configuration page.
- Configurable trigger width replaces DOUT_1MS_TRIGGER.
- midiphy frontpanel selection handling.
- Custom label lists: TRKLABEL.V4P, TRKCATS.V4P, TRKDRUMS.V4P.
- Option 9/33: unmute tracks on pattern changes.
- Option 14/33: change steps of current view only.
- Mixer CC output to bus.
- **"Ctrl" parameter layer** (changes track parameters; see `mbseqv4_cc_implementation.txt`).
- CV calibration with Hz/V curve handling.

### V4.095
- midiphy frontpanel support (LH/RH).
- **MBSEQV4+ introduction** (STM32F4-only features).
- V4+ CC layers for drum tracks.
- Trigger/Layer edit views for drums.
- Independent pattern-change reference steps vs sync-to-measure.
- Pattern change immediate via SELECT (regardless of sync).
- Options page overhaul (print/modify w/o gate, transposed-note print, swap LED colours, invert mute LEDs, TPD options).
- AOUT gate pin expansion (channels 9..12/13..15 set pins 1/3/5/7; pins 2/4/6/8 = accent on vel>100).
- V4+ AOUT calibration with per-voltage points + interpolation.

### V4.094 / V4.094b
- Antilog frontpanel support.
- Enhanced multi-track copy/paste/clear for PATTERN, PARSEL, TRGSEL, INSSEL.
- Pattern selection workflow (GP 1/2, 5/6, 9/10, 13/14 = step + group).
- Mute page auto-selection of unmuted tracks/layers.
- Edit page chord handling improvements.
- Config moved into Options (MIDI remote key, Track Selection CC, MIDI OUT runtime status, MENU button).
- **Shadow Out**: forward selected track MIDI to external port/channel.
- Metronome page merged into Options.
- Pattern bank modification locked by default.
- v094b: bug fix MUTE LED stuck on common frontpanels.

### V4.093
- Chord3 (108-chord set from EsotericLabs).
- Transposed note display in edit screen.
- Drum-track probability layer fix.
- Default layer init: 6-note poly (8 layers, 128 steps).
- PC/Bank re-send option (disable on pattern changes).

### V4.092
- **Per-step Root and Scale parameter layers**.
- New drum configs: 4×16/2×64, 4×16/2×128, 4×16/1×256.
- Track Mode "Note Last/First".
- **Step Trigger (STrg) Mode** — step progression controlled from transposer bus.
- Step disable via encoder-to-128 for CC/PitchBender/Program Change/Aftertouch.
- CLEAR button live recording = clears only selected step.
- CC control extensions: Play/Stop, Record assignable.
- Groove sync to global vs local reference step.

### V4.091
- Chord2 (32-chord alternative set).
- **Aftertouch parameter layer**.
- MBSEQ_HW.V4 functions: FX, MOVE, SCROLL; LED_MEASURE.
- CC layer label prints "#<number>".
- Jam page AStart records from first step in live mode.
- Record/Live button toggles record/live on any page.
- JAM_LIVE/JAM_STEP combo buttons.
- BLM16x16+X can record into any track.
- Transpose scale-based selection.
- Option 10: initial gate trigger layer.
- Drum config 2×64/2×128.
- AOUT Suskey (fingered portamento).
- Removed pattern-based scale control.

### V4.090
- FX→Scale display.
- Track duplication via COPY+PASTE on UTILITY page.
- Per-track UI selection (parameter/trigger layer, instrument, step view).
- BLM up to 8 faders.
- BLM keyboard mode transpose only outside normal mode.
- Sustain note cancellation if all gates clear.
- Jam recording options persisted to MBSEQ_C.V4.
- Song mode measure sync: switch at next measure when steps-per-measure < pattern sync.

### V4.089
- DIN testmode.
- Roll gate trigger layer.
- Euclid generator parameter-layer target selectable.
- Quick track dup: hold COPY then PASTE.
- Groove applies globally by default (GP7 for local).
- BPM page output delay per port (experimental).
- UNDO only on STM32 (RAM-bound).

### V4.088
- Improved glide for polyphonic steps.
- BLM16x16+X mute/solo support, ALT clears.
- **Robotizer Fx** (Borfo's contribution).
- LCD screensaver (30 min default).
- DETENTED4/5 encoder modes.

### V4.087
- Track Instrument page.
- CV Gate/Clock outputs (CV_GATE_SR1, CLK_SR).
- 8 DIN sync clock outputs with divider/pulsewidth, optional Start/Stop.
- Mixer channel dump + live-sending toggle.
- Mute/Unmute sync override via FAST.
- Drum step recording display.
- Datawheel mode persisted.
- STM32F4 bootloader "enforce_usb_device".

### V4.086
- BLM dimension improvements.
- EDIT ALL button fix.
- MIDI Learn for drum notes (hold GP12 on Track Event).
- Edit page step recording (hold GP step).
- SELECT toggles step recording; SELECT+hold opens config.
- Record page FTS/FX flags.
- Step recording note length.
- CLEAR clears played steps while held.
- LFO Extra CC enable/disable toggle.
- Echo enable/disable toggle.

### V4.085
- MENU_SHORTCUT customization.
- Bookmark config uses page names (auto-converted).
- SMS-like keypad name entry.
- Per-device SysEx port/delay (`/SYSEX/<device>/DEVCFG.V4`).
- Delay between multi-dump SysEx messages.

### V4.084
- STM32F4 USB host mode.
- FTS edit display (forced notes lower line).
- Live page FTS no longer applied to drum tracks.
- BLM16x16+X grid edit FTS option.
- Euclid generator auto-velocity.
- RECORD toggles RECORD↔EDIT.
- Song page unmute-all action.
- Mixer CC ordering (before/after PC).
- NRPN channel 127 = current track.

### V4.083
- Fix: potential hang during pattern change (V4.081 regression).

### V4.082
- Live page options persisted to MBSEQ_C.V4.
- STM32F4 initial release.

### V4.081
- USB MIDI Windows workaround.
- Song mode sync fix.

### V4.080
- SELECT+PASTE transfers active parameter layer.
- SELECT+CLEAR clears current layer only.
- CC-based step control (per-track CC).
- Manual clock mode (steps only via MANUAL page / CC / external MIDI).

### V4.079
- DUPL Fx (forward all / alt / alt-on-taps / random; CC/PB/Pres/PC forward flags).
- Groove intensity for all tracks.
- BLM SR syntax M1..M8.
- SRIO_NUM_SR up to 23.
- Auto MBSEQ_HW.V4 reload on upload.
- TPD official support (wilba_tpd hwcfg).
- Terminal: new, saveas, save, load, delete, session, sessions.

### V4.078
- MSD mode removed.
- Options page overhaul.
- Live/MIDI Router section-control forwarding.
- ALL mute/unmute menu.
- 6 dedicated mute/unmute button functions.
- CC-based mute (first CC + 15).
- CC-based mixer dump.
- CC layers excluded from LFO Extra CC sends.
- New CC layers default "off".
- Auto CC layer assignment on record.

### V4.077
- Parameter change → MIDI port for DAW recording.
- **Quick Save / Phrase A-P**: store mixer + pattern set in bank positions, accessed via SELECT+GP16 in SONG.
- Initial CC value config.
- LFO CC modulation fix.
- Mixer/pattern coupling toggle.

### V4.076
- MIDI router SysEx forwarding.
- SAVE / SAVE_ALL button functions.
- Mixer map naming (SELECT+GP16).
- LFO GP14 = send CC manually.

### V4.075
- IIC MIDI STM32 fix.
- Track Event page manual event send (GP10..13).
- MIDI remote keyboard NoteOn vel 0 deactivates remote.

### V4.074
- Improved ALL function (hold-and-move, ramp generation).
- Step selection pattern for ALL.

### V4.073
- MIDI Router "Sel.Trk" destination.

### V4.072
- USB single mode (bootloader V1.012).

### V4.071
- Rew/Fwd step-view selection out of song mode.
- MIDI Router "Track" destination (map channel→track).
- Track Selection CC (TRACK_CC in MBSEQ_HW.V4).

### V4.070
- **Ext.Ctrl MIDI Configuration page** (assign CCs to Morph, Scale, Song, Phrase, Pattern, Bank, All Note Off, etc.).
- Improved loopback transpose (sustain + glide).
- Note-stack auto-clear on loopback→manual transition.
- Chord transpose from loopback.
- `sdcard_format` terminal command.
- BLM_GP_ALWAYS_SELECT_MENU_PAGE option.

### V4.069
- USB MSD endpoint conflict fix.
- Layer-type confirmation on Event page.
- Session deletion from main page.

### V4.068
- GM5 Windows driver.

### V4.067
- MIOS32 bootloader V1.010 required.
- MIOS Filebrowser support.

### V4.066
- LPC17 stack overflow fix.

### V4.065
- DEBOUNCE_DELAY on Wilba BLM8x8.
- Mixer setup auto-load.
- Session SAVE stores mixer map.

### V4.064
- Tap tempo fix.

### V4.063
- **Guide track sync** (independent of steps per measure).
- Guide track changeable/disable from song position.
- RATOPC synchronous reset.
- Immediate Clear (no 2s delay).
- Copy/Paste/Undo includes track and drum instrument names.

### V4.062
- Preset-storage crash fix.

### V4.061
- STM32 MBHP_ETH init fix.

### V4.060
- **Guide track** (song mode track length = loop length).
- Phrase mode measure sync option.
- Phrase mode manual sync.
- Default encoder = MIOS_ENC_MODE_DETENTED3.

### V4.059
- **Euclidean rhythm generator** (SELECT&hold in edit → GP16).
- OSC feedback loop prevention.
- BUTTON_DIRECT_TRACK1..16 fix.

### V4.058
- **Pattern Remix** (Midilab) — see `pattern_remix_manual.txt`.
- LPC17 SysEx output fix.

### V4.057
- LPC17 MIDI OUT throughput.

### V4.056
- Pattern page ALL.
- ALL auto-disables on page change.
- Song "End" action (loop current set).
- Song page pattern-set takeover (SELECT+GP16).
- Song mode pattern selection via GP.
- Song page Fwd/Rwd edit position.
- Song mode direct start.
- Main page effective BPM.
- BPM digit display in slave mode.
- **Program Change layer**.

### V4.055
- LPC17 MIDI clock IN1..4.
- MIDI router 16 nodes.
- SysEx router support.
- Terminal: network, OSC, MIDI router, BLM; store/restore session.
- BLM Lemur iPad (OSC IP).
- BLM note recording in keyboard page.

### V4.054
- Datawheel multi-purpose modes (SELECT+GP7/8): scroll cursor/view, change value, select layer.
- Footswitch record/delete.
- Note/Chord non-immediate edit (hold EDIT).

### V4.053
- STM32 MIDI IN port fix.

### V4.052
- BPM/STEP digits output.
- CC value display fix.

### V4.051
- Improved live CC recording.
- Poly recording warning.
- Drum live/step recording (no drum-key remap).
- BPM digit optional 4th digit.
- Step LED digit display.
- Optional dedicated BPM encoder.

### V4.050
- MIDI event LEDs.
- BPM LED digit hardware support.

### V4.049
- MIDI OUT4 / IN4 on LPC17.

### V4.048
- CC/PB bandwidth optimization (only on change).
- Step record poly auto-clear.
- Step record note duration.
- Live record MIDI forward improved.
- Terminal `msd` command.
- Move-step no longer overwrites.

### V4.047
- Non-beta release.
- MIOS32 bootloader V1.005.
- LPC17 J5C signal reassignment.
- OSC TouchOSC protocol.

### V4.0beta46
- Multi-copy / multi-paste of all selected tracks (`COPY*.V4T` in `/PRESETS`).
- Wilba debouncing.

### V4.0beta45 — beta42
- beta45: macOS 10.7 USB; chord+velocity display fix.
- beta44: session selection fix.
- beta43: delayed mute/unmute synced to measure; trigger assignment auto-select; random page improvements.
- beta42: slave/auto start/stop/pause; **Live page**; parameter layer mutes (velocity/length/probability/delay/roll/roll2); Echo "Zero Delay"; move-function beyond step 16.

### V4.0beta41 — beta40
- beta41: bookmarks split global/session; 16 bookmark buttons; manual trigger sync; drum edit param display; RATOPC option.
- beta40: **Bookmark function** with MBSEQ_BM.V4 file.

### V4.0beta39 — beta36
- beta39: BUTTON_PAR_LAYER_A fix.
- beta38: 20%+ CPU load reduction, faster SD ops.
- beta37: length page quick selection, length/loop preset storage, direct track-selection buttons, track-selection DOUT.
- beta36: metronome steps-per-measure; MIDI song position handling.

### V4.0beta35 — beta31
- beta35: step view ≥17 fix; multi-note edit comfort.
- beta34: **Edit Views** (Step / Trigger / Layer / 303 / Step Select); glide more 303-like; testaoutpin command.
- beta33: SaveAs name check.
- beta32: songs/mixer/groove/local config import/export.
- beta31: session import/export.

### V4.0beta30 — beta28
- beta30: TRACK_MODE/GROOVE/LENGTH/DIRECTION button funcs; MORPH/TRANSPOSE renamed TRACK_*; FAST2; echo dotted delays; record-page forward-MIDI option; recording quantization configurable.
- beta29: tap tempo + accent-on-stretched-notes fix.
- beta28: session load missing-GC fix; udpmon command; **CV Configuration page**; individual CV channel notestacks.

### V4.0beta27 — beta23
- beta27: rescheduled-notes crash fix; BLM keyboard mode inversion; copy/paste channel/port; Paste/Clr global setting.
- beta26: SD error → terminal; play/stop terminal commands; status LED beat flash; OSC Pianist Pro; third MIDI IN/OUT port; Gate 7/8 reassigned.
- beta25: per-port OSC config; MIDI mode / text msg / SysEx OSC transfer modes; MIDI router → OSC; BLM OSC.
- beta24: **OSC support** (4 ports); network config page; debug terminal "network"; MBSEQ_GC.V4 global config; probability increment fix; gate-clear behaviour; BLM trigger display.
- beta23: external restart FA timing; transpose page default semitones.

### V4.0beta22 — beta18
- beta22: RS optimisation; BLM chaselight fix; trigger drum-mode fix; bus parameter load; preset-name overflow fix.
- beta21: **BLM16x16 prep**; **Bus routing concept** (4 buses for T&A or Play mode); track-mode bus selection; record port exclusion; Roll trigger default 2D10; Roll2 param layer; UNDO button; MIDI clock USB5..8 fix.
- beta20: pattern-page group selection; song-page cursor positioning; **MIDI file import**.
- beta19: MIDI file import implementation; hihat positions 3/4/5.
- beta18: **Session concept** (`/SESSIONS/<name>`).

### V4.0beta17 — beta12
- beta17: default 256 steps / 4 param layers.
- beta16: **MIDI Section Control** keyboard forwarding; copy section support; follow function.
- beta15: track sections via MIDI keyboard; MIDI config overhaul; section selection; CC#123 All Notes Off.
- beta14: MBHP_BLM_SCALAR.
- beta13: DIN_SYNC_CLK_PULSEWIDTH; layer selection inc/dec.
- beta12: **Preset track storage** (`.V4T` files in `/PRESETS`).

### V4.0beta11 — beta6
- beta11: paste partitioning logic.
- beta10: **MIDI file export**; **MIDI In/Out monitor page**; pattern/measure length separation; paste/clr behaviour option.
- beta9: MIDI file player sync.
- beta8: **MIDI file playback** (`/midi`).
- beta7: SMS-style label editor; synchronous pattern change; follow song mode; CC confirmation.
- beta6: **SysEx dump sending**.

### V4.0beta5 — beta1
- beta5: menu page list browser; About page.
- beta4: **NRPN parameter control**; internal loopback (Bus1); **MIDI remote keyboard**.
- beta3: global loop mode `*LOOPED*`; smooth startup; CLEAR 2s press.
- beta2: song page copy/paste/clear/insert/delete; 16-step ALL selection pattern; drum instrument labels editable.
- beta1: STM32 ARM Cortex M3 initial release; SD FAT32; USB MIDI 100× faster; 384 ppqn; track memory partitioning; parameter & trigger layers; track progression; **drum mode**; 32 chords; groove templates; Echo / LFO / Limiter / Loop Fx; real-time recording; MBSEQ_HW.V4.

---

## 5. Feature index by category

### Patterns / Song / Groove
- 4 groups × 8 patterns = 256 patterns on SD (`MBSEQ_B1..B4.V4` in `/SESSIONS/<name>`).
- Track length 1–256 steps, configurable partitions (64/4, 128/4, 256/4, 64/16, …).
- Pattern change sync (measure-aligned), synchronous change (after x steps), immediate via SELECT.
- Pattern preset files (`.V4T`) in `/PRESETS`.
- Multi-copy/paste of all selected tracks (`COPY*.V4T`).
- Pattern name editing (SMS keypad).
- Auto-save on change (option).
- Pattern↔mixer-map coupling toggle.
- Songs: pattern-sequence playback with events. End action loops current set.
- Guide track in song mode: track length = loop length, all tracks resync to step 1 after.
- Phrase mode: pattern selection via GP (alternative to song); Phrase A-P quick-save slots (V4.077).
- Groove templates: customizable swing; 21 styles; global or per-track; sync to global vs local ref step.

### Track types & modes
- Normal track (multi-note).
- Drum track: 16 instruments, drum mode editing, customizable map (`TRKDRUMS.V4P`), per-instrument velocity, configurable parameter assignments per layer, V4+ CC layers.
- Drum configurations: 4×16, 4×16/2×64, 4×16/2×128, 4×16/1×256, 2×64/2×128 etc.
- Track modes (playmode): Normal / Transpose / Arpeggiator / Off.
- Track Event page configures mode, port, channel, instrument name, PC/Bank on start.
- Track sections (slicing): keyboard zones, individual track section selection.

### Bus tracks (see §3 for deep-dive)
- 4 buses, each with transposer + arp sorted + arp unsorted notestacks.
- Loopback writes (internal MIDI port codes Bus1..Bus4 = 0x70..0x73), external MIDI port can also be configured as a bus.
- Track reads via `tcc->busasg.bus` when in Transpose or Arp playmode.
- Loopback CCs reach `SEQ_CC_MIDI_Set` and so can change CCs on bus-assigned tracks.
- Step Trigger (STrg) Mode: transposer keypress advances all bus-listening tracks (V4.092).
- Loopback bus can drive global Root/Scale (V4.097).
- Bus MIDI note ranges per bus (keyboard zone).

### Generative / random / probability
- **LFO Fx**: waveforms (sin, tri, saw, rect 5%–95%, inverted variants), bipolar amplitude, phase, interval (1–256 steps), interval reset, oneshot, assigns to Note/Vel/Len/CC, Extra CC with separate PPQN, clock-divider sync (V4.098).
- **Probability layer**: 0–100% gate probability per step.
- **Humanize Fx**: random note/vel/len intensity.
- **Robotizer Fx** (V4.088, Borfo): randomize note/vel/len/sustain/echo/duplicate/NOFX. Persisted via v2 ext block in `seq_file_b.c` (this work block, 2026-05-16). See §2 for the bar-anchor / measure-counter mechanism added 2026-05-16.
- **Random generator page**: generate values per parameter/trigger layer; drum mode.
- **Euclidean rhythm generator** (V4.059): SELECT&hold in EDIT → GP16; target layer selectable; auto-velocity (V4.084).
- **Cellular automaton generator** (new 2026-05-18, this codebase): elementary 1D Wolfram CA on the same TRKEUCLID page; per-trigger-layer type selector (GP12) flips between Euclid / CA / Polyrhythm / Subdivide / L-system; CA params Rule/Seed/Gens replace Len/Pulses/Offset on GP9/10/11. PG2 (GP16 toggle) exposes boundary mode on GP9 (Wrap / Zero / Mirror). Implementation: `CAGenerator()` in `seq_ui_trkeuclid.c` — two row buffers (max 256 cells) ping-pong for up to 32 generations; final row writes to trg or par layer via the same indirection as the euclid generator. Cost: `gens × num_steps × O(1)` ≤ 32×256.
- **Sub-page toggle pattern on TRKEUCLID** (new 2026-05-18, this codebase): GP16 flips `page_view[layer]` between PG1 and PG2 for the active generator type. Only Poly and CA currently have PG2 backings (`GenHasPG2()` gates both the LCD render and the toggle action). LCD row 0 labels and row 1 values swap with `pv`; the GP16 LED lights when PG2 is active; trigger preview at row 0 cols 60-79 is preserved across both pages. Pattern is intended as the canonical "more params for this generator" UX template for future types.
- **Polyrhythm / crossbeat generator** (new 2026-05-18, this codebase): `PolyrhythmGenerator()`, GP9=N, GP10=M (cycle length), GP11=phase. Bresenham-style "N pulses spread evenly across M steps" distribution; pattern tiles across the full track. PG2 (GP16 toggle) exposes a 2nd N2/M2/phase2 layer OR'd with the primary for compound polyrhythms (e.g. 3-against-8 OR 5-against-7). n2=0 disables. Cost: O(num_steps).
- **Recursive subdivide generator** (new 2026-05-18, this codebase): `SubdivideGenerator()`, GP9=depth (1..8), GP10=prob (0..100), GP11=seed. Builds a 2^depth pattern by recursive binary split with per-half xorshift8 survival probability; tiles across track. Local PRNG keeps the live `seq_random` xorshift32 untouched. Cost: O(2^depth) ≤ 256.
- **L-system generator** (new 2026-05-18, this codebase): `LSystemGenerator()`, GP9=preset (0..7), GP10=iter (0..6), GP11=seed (rotates read offset). 8 hardcoded (axiom, '1'→rule, '0'→rule) presets in `lsys_table[8][3]`; two 512-byte buffers ping-pong during expansion; expansion halts early if next pass would overflow. Cost: bounded by buffer size, single pass per generate.
- **ROBOLOOP page** (new 2026-05-16, this codebase): bar-anchor navigation, freeze/freeze-q/reseed/reroll ops, FX_ROBOTIZE↔ROBOLOOP toggle via GP16, scale-aware robotize.
- **Track-specific Root and Scale parameter layers** (V4.092): per-step root/scale overrides.
- **Per-track xorshift32 PRNG** for robotize: caller-supplied state, deterministic, restorable.
- **Conditional triggers** — *not yet implemented*, on user roadmap.
- **Turing machine generator** — parked on `feature/tm-generator` branch; needs runtime-hook redesign.

### Live performance
- Mute / solo (track, parameter layer); synced mute (measure-aligned) with FAST override.
- BLM mute/solo (Extra Row buttons 3/4); ALT clears all.
- CC-based mute (first CC + 15).
- Echo Fx: repeats, delay, damp, feedback, vel feedback, note increment, dotted delays.
- DUPL Fx (V4.079): forward-all / alt / alt-on-taps / random; CC/PB/Pres/PC forward flags; channel count and behaviour configurable; First Channel honoured in alt/random (V4.097).
- Humanize, Robotize, Limit, Loop Fx.
- Morph mode: blend parameter values via CC#1; offset step 1–128; GP6 store-into-original (V4.097).
- Pattern remix (V4.058, Midilab; see `pattern_remix_manual.txt`): preview / 2-press select / mixer-coupling / clip integration (partial).
- Jam page: play notes via GP buttons; auto track selection on incoming channel (V4.097); MIDI bus config for transpose/arp/live.
- Shadow Out (V4.094): forward selected track MIDI to external port/channel.
- Live recording with quantization percent; step recording note length; FTS/FX flags on record.

### Editing UI
- **Edit Views** (V4.0beta34): Step / Trigger / Layer (up to 14 par layers, good for chord builds) / 303 (octave+note+vel) / Step Select.
- ALL button: hold-and-move identical, unselected-encoder ramps (Cirklon-style), 16-step selection pattern, auto-disable on page change.
- Datawheel multi-purpose modes (SELECT+GP7/8): scroll cursor / scroll view / change value / select layer.
- Note/Chord non-immediate edit (hold EDIT).
- FTS edit display: forced keys lower line, original upper right.
- Note display: transposed / non-transposed (option), names / numbers.
- FX submenu pages: Scale, Echo, LFO, Limit, Loop, Humanize, Robotize (FX_ROBOTIZE), DUPL, **Robo** entry now opens FX_ROBOTIZE↔ROBOLOOP toggle.
- Drum kit: editable instrument labels (16 per track), parameter layer assignments, configurable partitioning.
- Scale management: global selection on FX→Scale, root/scale per-step parameter layers, FTS in live mode (skipped for drum tracks, V4.084), FTS in BLM16x16+X grid (V4.084), transpose-page scale-based selection (V4.091).
- Chord management: 3 chord sets (Chord1=32, Chord2=32, Chord3=108), root override global or per-step, Layer view for custom chords, chord transpose from loopback (V4.070).

### MIDI / Sync / CV / Clock
- MIDI ports: Default + IN/OUT 2..4 (STM32), USB1..4, up to 8 IIC MIDI OUT.
- MIDI router: 16 nodes (V4.055), SysEx forwarding, destinations include "Track", "Sel.Trk", and OSC.
- Ext.Ctrl page (V4.070): CC assignment for Morph, Scale, Song, Phrase, Pattern, Bank, All Notes Off, mute, mixer dump, etc.
- Parameter change → MIDI port (V4.077) for DAW recording.
- MIDI Section Control with keyboard forwarding (V4.0beta16).
- MIDI File import / export / playback (`/midi`); quantization 16/32/64; loop on/off; auto-sync to measure if running.
- MIDI monitor page; filter clock/active sense by default.
- Clock: 384 ppqn internal; slave / master / auto modes; tap tempo; song position pointer; optional dedicated BPM encoder; BPM LED digits; per-port output delay (experimental V4.089).
- CV: V/Oct, Hz/V, Inverse curves; slew 0–255 mV; pitch range; gate polarity; per-voltage calibration with interpolation (V4+); per-channel notestack; AOUT module type select.
- Up to 32 CV outputs (4 AOUT modules, V4.096).
- 8 DIN sync clock outputs with individual divider/pulsewidth, optional Start/Stop, Suskey/portamento (V4.091).
- CV clock rates 0.063 / 0.125 / 0.25 / 0.5 PPQN + Stop/Start + pulse fns (V4.097).
- NRPN parameter control (`mbseqv4_cc_implementation.txt`); NRPN channel 127 = current track (V4.084).

### File / Storage / SysEx
- SD card FAT32 required.
- Root files: `MBSEQ_GC.V4` (global config — shared across sessions), `MBSEQ_HW.V4` (hardware config).
- `/SESSIONS/<name>/` (≤8 chars): `MBSEQ_B1..B4.V4` (banks), `MBSEQ_S.V4` (songs), `MBSEQ_G.V4` (grooves), `MBSEQ_C.V4` (session config), `MBSEQ_M.V4` (mixer maps), `MBSEQ_BM.V4` (bookmarks). Plus two **sentinel-bank** files outside the `B1..B4` navigation: `MBSEQ_PH.V4` (phrase snapshot library, bank `0xfd`) and `MBSEQ_AN.V4` (CHECKPOINT anchor, bank `0xfe`).
- `/PRESETS`, `/SYSEX/<device>/`, `/MIDI`, `/TRACKS` directories.
- Auto-load DEFAULT session on startup.
- Backup terminal command (experimental, `.tar`).
- SysEx device pages with per-device port/delay; multi-dump inter-message delay (V4.085).
- MIDI Filebrowser support (V4.067, V4.079 auto-reload).

### Hardware / Frontpanels
- Frontpanels: Wilba BLM8x8, BLM4x16, BLM16x16+X, Antilog (V4.094), midiphy LH/RH (V4.095), Wilba TPD.
- BLM modes: keyboard, 303, drum, grid edit with FTS, OSC variants.
- Encoder modes: DETENTED3 default (V4.060), DETENTED4/5 (V4.088), FAST2 push.
- LED functions: status beat, mute, layer, MIDI event, BPM digits, step digits, measure flash, BLM colour mapping.
- Direct track-selection buttons (16); track-selection DOUT (2 SRs).
- Bookmark function (V4.0beta40+): 16 buttons; `MBSEQ_BM.V4` file; short press recall, long press store.
- Footswitch: record / delete.
- OSC: 4 ports (req MBHP_ETH); MIDI / text msg / SysEx modes; Lemur iPad.
- USB host mode on STM32F4 (V4.084).

---

## 6. Open TODOs / FIXMEs / XXX (65 items)

Located by full-tree grep; grouped by file. Verbatim comment text in quotes.

> Priority triage of these items lives in [MBSEQV4_TODO_TRIAGE.md](MBSEQV4_TODO_TRIAGE.md).

### Core sequencer engine

- [seq_core.c:839](../core/seq_core.c#L839) — TODO — "dirty code, we should handle this in SEQ_CV, because only there it's known that clk_divider 0 and 0xfffd/e/f are used for special functions"
- [seq_pattern.c:135](../core/seq_pattern.c#L135) — TODO — "stall here if previous pattern change hasn't been finished yet!" — **RETIRED 2026-06-12 (not built); see §3.** Auto-writeback makes a per-group overwrite lose only an intermediate target, never the writeback decision.
- [seq_song.c:244](../core/seq_song.c#L244) — TODO — "take bpm_start into account!"
- [seq_song.c:413](../core/seq_song.c#L413) — TODO — "implement prefetching until end of step!"

### MIDI port & router

- [seq_midi_port.c:415](../core/seq_midi_port.c#L415), [:436](../core/seq_midi_port.c#L436), [:453](../core/seq_midi_port.c#L453) — TODO — "check for ethernet connection here" (×3)
- [seq_midi_router.c:164](../core/seq_midi_router.c#L164) — TODO — "optimize this by collecting up to 3 data words and put it into package"
- [seq_midi_router.c:275](../core/seq_midi_router.c#L275) — TODO — "special check for OSC, since MIOS32_MIDI_CheckAvailable() won't work here"

### MIDI input / recording

- [seq_record.c:454](../core/seq_record.c#L454), [:588](../core/seq_record.c#L588) — TODO — "handle this correctly if track is played backwards"
- [seq_record.c:580](../core/seq_record.c#L580) — TODO — "we could vary the tolerance depending on the BPM rate: than slower the clock, than lower the tolerance"

### LCD / UI display

- [seq_lcd.c:383](../core/seq_lcd.c#L383) — TODO — "tmp!!! Provide a streamed COM method later!"
- [seq_lcd.c:701](../core/seq_lcd.c#L701) — TODO — "enhanced messages"
- [seq_lcd.c:716](../core/seq_lcd.c#L716), [:836](../core/seq_lcd.c#L836) — TODO — "tmp. solution to print chord velocity correctly"

### File management

- [seq_file_m.c:410](../core/seq_file_m.c#L410), [seq_file_b.c:690](../core/seq_file_b.c#L690), [seq_file_s.c:404](../core/seq_file_s.c#L404) — TODO — "before writing into <map/pattern/song> slot, we should check if it already exists, and then…" (×3 — overwrite confirmation)
- [seq_file_gc.c:282](../core/seq_file_gc.c#L282), [:294](../core/seq_file_gc.c#L294) — TODO — "improve code here - allow get_dec with 64bit?"
- [seq_file.h:18](../core/seq_file.h#L18) — TODO — "change" (orphaned marker)

### MIDI SysEx & sync

- [seq_midi_sysex.c:192](../core/seq_midi_sysex.c#L192) — TODO — "here we could send an error notification, that multiple devices are trying to access the device"
- [seq_midi_sysex.c:193](../core/seq_midi_sysex.c#L193) — TODO — "support for independent streams of MBSEQ Remote and remaining stuff"

### Layer & event

- [seq_layer.c:404](../core/seq_layer.c#L404) — TODO — "optionally different channel taken from const D"
- [seq_layer.c:479](../core/seq_layer.c#L479), [:840](../core/seq_layer.c#L840) — TODO — "we could do this in seq_core, maybe cleaner" (Quick&Dirty CC forwarding)
- [seq_layer.c:1084](../core/seq_layer.c#L1084) — TODO — "good? Or should we record on the selected par layer?"
- [seq_morph.c:144](../core/seq_morph.c#L144) — TODO — "check if re-using the MSB is useful"

### Arpeggiator / transposer

- [seq_ui_trkevnt.c:956](../core/seq_ui_trkevnt.c#L956) — TODO — "optionally allow to use a 'local' channel, edit parameter right of VelA"
- [seq_ui_trkevnt.c:1529](../core/seq_ui_trkevnt.c#L1529) — TODO — "copy preset for all selected tracks!"

### BLM

- [seq_blm.c:226](../core/seq_blm.c#L226) — TODO — "how about using red LEDs for accent?"
- [seq_blm.c:440](../core/seq_blm.c#L440) — TODO — "consider ui_selected_trg_layer?"
- [seq_blm.c:505](../core/seq_blm.c#L505) — TODO — "should we cycle the layer that is used if no free layer has been found?"
- [seq_blm.c:1291](../core/seq_blm.c#L1291) — TODO — "dirty - actually we should differ between mode_selections_8rows/16rows"
- [seq_blm.c:1734](../core/seq_blm.c#L1734) — TODO — (empty/placeholder)

### Pattern UI

- [seq_ui_pattern_remix.c:50](../core/seq_ui_pattern_remix.c#L50) — TODO — "put this inside options page to be selected by the user"
- [seq_ui_pattern_remix.c:154](../core/seq_ui_pattern_remix.c#L154) — TODO — "finish the incrementer helper for gp encoders"
- [seq_ui_pattern_remix.c:311](../core/seq_ui_pattern_remix.c#L311) — TODO — "implements a ableton remotescript to the clip control functionality"
- [seq_ui_pattern_remix.c:705](../core/seq_ui_pattern_remix.c#L705) — TODO — "sync this with the pattern change handled by SEQ_PATTERN_Handler()"
- [seq_ui_song.c:61](../core/seq_ui_song.c#L61), [:62](../core/seq_ui_song.c#L62) — TODO — "has to be aligned with SEQ_UI_Button_DirectTrack() function in seq_ui.c"

### Disk / bank validation

- [seq_ui_disk.c:692](../core/seq_ui_disk.c#L692), [seq_ui_pattern.c:140](../core/seq_ui_pattern.c#L140) — TODO — "print error message if bank not valid (max_patterns = 0)"
- [seq_ui_bpm_presets.c:78](../core/seq_ui_bpm_presets.c#L78) — TODO — (empty/placeholder)

### CC / control

- [seq_cc.c:212](../core/seq_cc.c#L212), [:414](../core/seq_cc.c#L414) — TODO — (empty inline at `CC_CHANGE_STEP` case, getter and setter)
- [seq_ui_opt.c:1344](../core/seq_ui_opt.c#L1344) — debug — `TODO#%3d` printed for unknown fader function

### MIDI import / export

- [seq_midimp.c:214](../core/seq_midimp.c#L214) — TODO — "currently no dedicated track can be imported"
- [seq_midimp.c:379](../core/seq_midimp.c#L379), [:542](../core/seq_midimp.c#L542) — TODO — "select dedicated track" (note event, meta event)

### App-level

- [app.c:595](../core/app.c#L595) — TODO — "use proper mutex handling here"
- [app.c:801](../core/app.c#L801) — TODO — "should we load the patterns when SD Card has been detected?"
- [seq_random.c:51](../core/seq_random.c#L51) — TODO — "combine with timer values for even more randomness"
- [seq_groove.c:5](../core/seq_groove.c#L5) — TODO — "customized groove templates stored on SD Card" (file header)

### Stub / reserved UI pages

- [seq_ui_pages.c:44](../core/seq_ui_pages.c#L44), [:54-60](../core/seq_ui_pages.c#L54) — references to `SEQ_UI_TODO_Init()`; backed by [seq_ui_todo.c](../core/seq_ui_todo.c) stub. Slots reserved for future pages.

---

## 7. Deferred / wishlist features from CHANGELOG

### Explicit "future" / "planned" markers

- `pattern_remix_manual.txt:177` — "Live API is not fully implemented yet, needs a remote script and some coding to handle in a inteligent and transparent way the audio and midi tracks from ableton live"
- `pattern_remix_manual.txt:240` — "A integration between midibox and ableton live are planned. but need more study about what to control and how"
- `pattern_remix_manual.txt:258-261` — Remix-page reload on GP press; SaveAs option; Ableton API completion.
- `CHANGELOG.txt:423` (V4.090) — encoder inversion: "Additional (internal) functions can be added in future"
- `CHANGELOG.txt:2091-2095` (V4.0beta16) — BLM4x16 and BLM16x16 section selection.

### V4 wishlist (CHANGELOG.txt lines ~2384+)

1. SysEx librarian (record + dump for external gear)
2. Remote access for controlling multiple MBSEQ instances (partial)
3. Track sections A/B/C/D like MB808 (preloading without audition)
4. SysEx dump of patterns/songs
5. Apply grooves to DIN sync output
6. ALL Length change only on steps with gates ON
7. SysEx file handling improvements (non-blocking, "Sending…" indicator)
8. Loopback track HOLD/RESTART for arpeggiator
9. Tempo button shortcut for live BPM fades
10. Humanize trigger layers (accent/glide/roll, percentage)
11. Mixer relative CC change
12. Global loop quantization to measure
13. **Track output → track input routing** (this is the closest match to what generic bounce-in-place addresses)
14. AOUT control via NRPN
15. Remote control on dedicated MIDI port without remote-key requirement

### V4+ wishlist (CHANGELOG.txt lines ~2735-2787, requires STM32F4)

1. Expression layer (per-step expression/dynamics)
2. Audio metronome (click track output)
3. Directory sorting for SD optimisation
4. Delays by port AND channel
5. MIDI filter for incoming events
6. Multi-channel recording (16 tracks simultaneously)
7. Pitch bend & CC interpolation
8. **Save Robotizer settings** — done by us (v2 ext block, 2026-05-16)
9. Synthesizer name mapping (friendly routing)
10. Bipolar LFO amount independent per destination
11. Sample & Hold LFO trigger layer; "SINEtrg" triggered waveforms
12. Multi-record on all tracks simultaneously
13. Fx Duplicate fully assignable port/channel (needs CCs ≥128 storage — the ext block we built now enables this)
14. Multi record/live for all tracks in parallel

---

## End

Update this document whenever a feature lands, a CC moves, an invariant changes, or a TODO is resolved. Memory should stay slim and point here.
