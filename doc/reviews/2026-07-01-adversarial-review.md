# Deep Adversarial Review — MIOS32 platform + MIDIbox SEQ V4

> **Archive.** The still-open tail (#2 · #54 · #55, HELD #35 · #46) is tracked on
> [`doc/OPEN_ITEMS.md`](../OPEN_ITEMS.md) (2026-07-11 docs consolidation); this file
> keeps the full derivations. Held-finding fix plans:
> [`doc/plans/2026-07-02-held-findings-35-46.md`](../plans/2026-07-02-held-findings-35-46.md).

_Generated 2026-07-01. Scope: all MIDIbox-authored code + vendor integration seams, deepest on the real-time hot path and the newest fork features. **Follow-up 2026-07-01** — 7 of 8 P1s fixed + pushed to main (commits ff2505c5, 905f93a3). **Follow-up 2026-07-01 (input-hardening pass)** — the Story-2 sweep landed as one pass (commit 4b4fbd71): #4 #9 #10 #11 #12 #18 #21 #25 #32 #33 #34 #56 #57 #67 fixed at the SD-load / MIDI-in boundary (TrackInit is the geometry source-kill). Build clean, flashed, HIL 241/241 green. **Follow-up 2026-07-01 (story-1 concurrency sweep)** — commit 417cd864: #0 #1 #15 #16 #17 #19 #20 #22 #23 #24 #42 #51 #52 #62 #68 #70 fixed (short critical sections at the shared-state touch, the pattern #6/#53 modeled; plus RenderSuppressSync so bulk CC replays don't regress). #2 HELD (guard can't span SEQ_CORE_Reset; #1 closes the clobber mechanism). Build clean, flashed, HIL 241/241 green + by-ear GO. **Follow-up 2026-07-01 (CAPTURE fidelity)** — #13/#14 fixed context-sensitively (fb0f76e6): exact-boundary gates tie into an onset / across the loop seam, terminate only into in-window silence; re-sim sink restores multi-step chains. Flashed, HIL 241/241 green (glide + multi-step pins held); exact-into-silence terminate branch is by-ear (no foreign-clkdiv fixture yet). **Follow-up 2026-07-02 (P2/P3 quick-win sweep)** — commit e0bb8769: the remaining tail closed (25 rows) — #3 #7/#64 #8 #26 #29 #36 #37 #38 #39/#40 #43 #44 #45 #47 #48 #49 #50 #59 #60 #61 #65 #66 #69 #71. Each finding was independently re-derived against current source (a 25-agent derive pass; line refs had drifted and two of this review's own suggested-fixes had already proven misdirected), each fix spec adversarially verified, then a 5-lens diff review over the applied result caught + fixed 2 minor regressions (#65 note-0 metronome-slot corner, printf count==0 va_end). **#35 and #46 HELD** (re-derived, real, but both need on-hardware validation the USB HIL cannot provide — full fix plans in doc/plans/2026-07-02-held-findings-35-46.md). Build clean (+~2KB flash, +272B main SRAM for the RX buffers); **flashed, HIL 241/241 green + by-ear GO (2026-07-02).** #61 (roll2 flam clkdiv scaling) and #60 (synch-to-measure nth-trigger) — the two behavior-changing fixes HIL cannot judge — confirmed good by ear on the instrument._

## Executive summary

An adversarial sweep of ~130K lines (MIOS32 HAL + SEQ V4 app + fork) surfaced **72 verified defects**. **No memory-corruption bug is reachable through the shipping control surface today** — the refutation gate demoted every P0 candidate to P2 once a skeptic found the upstream clamp or critical section that currently guards it. But that safety is *borrowed*: it lives in a caller, a clamp, or a convention elsewhere, not at the vulnerable site, so a future edit, a corrupt/foreign SD file, or an unexpected MIDI message re-opens it. **Eight findings are P1** (audible or user-reachable now); **7 have been fixed and merged** (commit ff2505c5 + 905f93a3). The corpus collapses into **two structural stories** best fixed as patterns rather than one at a time.

### Story 1 — the +4 emission task shares mutable state with the +2/+3 tasks without locks (16 findings)
The 1 ms emission task races the UI/button, MIDI-in, and hooks tasks over the render double-buffer, the generator pool, the undo journal, the live-play bitmaps, and the clock counter. The fork's newest surfaces (melodic play, generators, capture-to-track) *widened* these windows.
- **#6 (P1, FIXED):** pressing BOUNCE (GP8) on the pitch-gen page *while playing* runs a track render in the UI task that races the emission task's double-buffer flip → torn output mirror → wrong live pitch/gate on the very next step. Fixed: wrapped render+flip in `portENTER_CRITICAL` inside `SEQ_CORE_CaptureTrackOutput` and the `journal_restore` twin (seq_core.c). Build 241/241 HIL.
- **#19 / #16 / #15 (P2):** the undo-journal snapshot, generator ROLL, and ENGAGE all read/mutate buffers the emission task is concurrently writing → an undo can restore a pattern that never coherently existed; a fresh ENGAGE landing on a bar line can emit a muted/garbage measure. `journal_restore` already uses the fix (`portENTER_CRITICAL`); `journal_snap` omits it.
- **#53 (P1, FIXED):** a synched pattern change does a blocking multi-sector SD read *inside* `portENTER_CRITICAL` + `MUTEX_MIDIOUT` on the +4 task, stalling the MIDI drain. **This is the mechanism behind the fork's long-running freeze/hiccup history**, now pinned to specific lines. Fixed: Tier-1 (dropped outer critical section, drift-gated writeback; seq_pattern.c). Depends on #6. Build 241/241 HIL.
- **#35 (P3←P1, HELD — re-derived 2026-07-02):** the "NonBlocking" UART send busy-waits when the 64-byte DIN FIFO fills. Re-derivation confirmed the mechanism (`MIOS32_UART_MIDI_PackageSend_NonBlocking` calls the *blocking* `MIOS32_UART_TxBufferPutMore`, spinning the +4 task at DIN line rate under `MUTEX_MIDIOUT`) and that the original suggested fix was wrong twice (relocated the spin + corrupted running-status state). A safe fix needs three coupled layers and DIN-rig by-ear validation the USB HIL can't provide — still HELD; full plan in doc/plans/2026-07-02-held-findings-35-46.md.

### Story 2 — values from SD files and external MIDI index fixed arrays without validation (14 findings)
Persistence and MIDI-in trust their inputs; the bound is assumed, not checked.
- **#28 (P1, FIXED):** a pattern *name* is passed as a `printf` **format string** (`sprintf(dst, name)`) — a name containing `%s` dereferences wild stack args. (Verification note: %n is inert in the fork — no %n compiled — but %s wild-deref is real.) Fixed: all 6 name-copy sites → bounded `memcpy(,,20)+NUL` (seq_ui_pattern_remix.c).
- **#30 (P1, FIXED):** track-preset Par/Trg address offset wild write. Fixed: gate write on `addr_offset+16<=MAX` (seq_file_t.c).
- **#31 (P1, FIXED):** `song_size < 20` underflow overruns `seq_song_steps[128]`. Fixed: reject bank if `song_size < header`, clamp `num_entries` (seq_file_s.c).
- **#27 (P1, FIXED):** BLM keyboard mode indexes 16-element arrays with un-clamped button_column (0..63). Fixed: clamp `button_column < BLM_SCALAR_MASTER_NUM_ROWS` at function entry (seq_blm.c).
- **#9 (P2, FIXED 4b4fbd71):** drum track claiming >16 instruments overruns `layer_events[16]` — killed at the source: `SEQ_*_TrackInit` now rejects >16 instruments/layers + zero factors with u32 size math, plus local clamps in the drum event loop.

### Two more that will bite in ordinary use
- **#63 (P1, FIXED):** a positive per-port MIDI-clock delay wraps the `0xffffffff` sentinel used for Note-Off scheduling → sustained/stretched notes are cut off instantly. Fixed: sentinel exempt from delay add; positive delays saturate instead of wrap (seq_midi_out.c).
- **#13 / #14 (P2, FIXED fb0f76e6):** CAPTURE writes exact-integer-step notes as an *unterminated* Glide (both the live-tape and stopped re-sim paths), so a captured note over-holds across the following rest — the exact fidelity bug CAPTURE's own comments say the code exists to prevent. Fixed context-sensitively (tie into an onset / terminate into silence); multi-step chains restored in the re-sim sink.

### Where to start
The remediation plan below sequences all 72 into waves. Highest leverage:
1. **One input-hardening pass** that clamps track / instrument / scale / geometry indices and masks note/CC bytes to 7 bits at every SD-load and MIDI-in entry point kills most of Story 2 at the source (#9, #27, #30, #31, #11, #18, #21, #56, #10, #32). **DONE 4b4fbd71** — shipped as one sweep, also covering #4, #12, #25, #33, #34, #57, #67.
2. **Two small self-contained user-facing fixes:** the `sprintf(dst, name)` format string (#28) and the sentinel-timestamp wrap (#63).
3. **The Story-1 races** want the treatment `journal_restore` already models — a short `portENTER_CRITICAL` around the buffer read, or deferring the UI-task render to the tick when `SEQ_BPM_IsRunning()` (#6, #19, #15, #16), plus moving the blocking SD/UART work off the +4 critical path (#53, #35).

### Honesty caveat
These are static verdicts — source-read and triple-refuted, but **not reproduced on hardware**. The RT-timing magnitudes (#53/#35 stall length, render cost) are asserted, not measured; confirm them with the existing `CMD_*_PERF` bench probes before and after any fix. See *Coverage & honest limitations* at the end.

---

## How this review was run

- **26 finders** — 20 subsystem clusters (clock/BPM, core tick+emit, render stack, MIDI-out scheduler, layer/transforms, CAPTURE, gen+RNG, GRAVITY, phrases/morph, save/undo, play/record, MIDI-in/route, HIL SysEx, UI core+BLM, UI pages, persistence, UART, USB, SPI/DMA/SD, MIDI codec, timers/sys/IIC/bootloader, vendor seams) + **4 cross-cutting lens sweeps** (concurrency, RT-timing, memory-safety, resource-exhaustion).
- **288 agents, ~10.3M tokens.** Each candidate finding passed through a **3-skeptic adversarial refutation gate** — one skeptic re-checked the source anchor, one checked reachability/guards, one checked whether the failure scenario actually holds. A finding survived only if ≥2 of 3 kept it **and** ≥1 confirmed the code against source. A **second-look pass** re-ran the six hottest clusters seeded with round-1 titles to hunt what the first pass missed.
- **Result: 72 findings survived** (59 first pass + 13 second look). Severity after the gate: **P1** 8 · **P2** 22 · **P3** 42.
- **Severity is the *verified* severity** (post-gate). Where the finder's proposed tier differed, both are shown — the gate frequently *demoted* findings (e.g. a P0 that turned out to be latent-not-yet-reachable became P2), which is the point of an adversarial pass.

| Tier | Meaning |
|---|---|
| **P0** | Crash / memory corruption reachable now |
| **P1** | Audible failure — dropped/stuck/wrong live note, missed tick, perceptible stall |
| **P2** | Latent / robustness — race not yet triggered, missing bound on a currently-safe path, data loss on power-fail or corrupt/foreign file |
| **P3** | Correctness edge — off-by-one in a rare mode, wrong-but-recoverable |

> **Read this right:** almost every P2/P3 here is *guarded today* — safe because of an upstream clamp, a critical section, or a caller convention. They are reported because the safety rests on a **non-local invariant** (an assumption elsewhere) rather than a check at the site, so a future edit, a corrupt/foreign SD file, or a new caller re-opens them. That is exactly the class of latent trap an adversarial pass exists to surface.

## Findings at a glance (ranked)

| # | Sev | Class | Cluster | Location | Finding |
|---|---|---|---|---|---|
| 53 | ✓ P1 | rt-timing | LENS rt-timing | `seq_pattern.c` — SEQ_PATTERN_Handler :1440 | **[FIXED 905f93a3]** Synched pattern change does blocking SD read inside portENTER_CRITICAL + MUTEX_MIDIOUT on the +4 task, stalling MIDI drain. Fixed: Tier-1 (dropped critical section, drift-gated writeback). Depends on #6. |
| 6 | ✓ P1 | concurrency | render stack | `seq_core.c` — SEQ_CORE_CaptureTrackOutput / SEQ_CORE_ProcessorBounce :1922 | **[FIXED ff2505c5]** Unguarded SEQ_CORE_RenderTrack in capture/bounce races the emission task's double-buffer flip → torn output mirror = wrong live pitch/gate. Fixed: wrapped render+flip in portENTER_CRITICAL. |
| 27 | ✓ P1←P0 | memory-safety | UI core+BLM | `seq_blm.c` — SEQ_BLM_BUTTON_GP_KeyboardMode :925 | **[FIXED ff2505c5]** BLM keyboard mode indexes 16-element arrays with un-clamped button_column (0..63) from external MIDI. Fixed: clamp to BLM_SCALAR_MASTER_NUM_ROWS at function entry. |
| 63 | ✓ P1 | logic | MIDI OUT sched | `seq_midi_out.c` — SEQ_MIDI_OUT_Send :331 | **[FIXED ff2505c5]** Positive per-port MClk delay wraps the 0xffffffff sentinel Note-Off timestamp → sustained notes cut off instantly. Fixed: sentinel exempt from delay; saturate instead of wrap. |
| 28 | ✓ P1←P0 | memory-safety | UI pages | `seq_ui_pattern_remix.c` — Encoder_Handler/Button_Handler (sprintf name copies) :459 | **[FIXED ff2505c5]** Pattern-name copies use the name as printf format string — %s dereferences wild stack args. [Correction: %n is inert; %s real.] Fixed: all 6 sites → bounded memcpy+NUL. |
| 30 | ✓ P1←P0 | memory-safety | persistence | `seq_file_t.c` — SEQ_FILE_T_Read :190 | **[FIXED ff2505c5]** Track-preset Par/Trg address offset causes wild write past layer row. Fixed: gate write on addr_offset+16<=MAX. |
| 31 | ✓ P1 | memory-safety | persistence | `seq_file_s.c` — SEQ_FILE_S_SongRead :369 | **[FIXED ff2505c5]** Corrupt song bank with song_size < 20 underflows num_entries and overruns seq_song_steps[128]. Fixed: reject if song_size<header, clamp num_entries. |
| 46 | P2←P1 | persistence | timers/sys/iic/bsl | `bsl_sysex.c` — BSL_SYSEX_WriteMem :574 | **[HELD 2026-07-02]** (re-derived 2026-07-02: real (off-base erase + no verify), but the BSL-resident copy runs during uploads (app does not link bsl_sysex.c); deploying a fix means a live sector-0 reflash unprovable without hardware. Full plan in doc/plans/2026-07-02-held-findings-35-46.md.) STM32F4 flash write only erases on exact sector-base match and never verifies -> silent corrupt program on non-base write |
| 51 | ✓ P2 | concurrency | LENS concurrency | `seq_live.c` — SEQ_LIVE_PlayEvent :226 | **[FIXED 417cd864]** Unguarded read-modify-write on seq_live_played_notes[]/live_keyboard_*[] widened to a +2/+3 cross-task race by the new play surface _(dup of #21)_. Fixed: atomic bitmap RMW + triple snapshot/store; sends outside the critical section. |
| 9 | ✓ P2←P0 | memory-safety | layer/transforms | `seq_layer.c` — SEQ_LAYER_GetEvents (drum-mode note loop) :422 | **[FIXED 4b4fbd71]** Drum-mode event loop overruns layer_events[16] when a track reports >16 instruments. Fixed: TrackInit rejects >16 instruments (source-kill) + local clamps and 83-guard re-entry fix in the drum loops. |
| 35 | P3←P1 | rt-timing | UART MIDI | `mios32_uart_midi.c` — MIOS32_UART_MIDI_PackageSend_NonBlocking :306 | **[HELD 2026-07-02]** (re-derived 2026-07-02: real busy-wait (MIOS32_UART_TxBufferPutMore spins on the +4 task); safe fix needs a 3-layer driver/Handler/flush restructure with DIN-rig by-ear validation the USB HIL cannot provide. Full plan in doc/plans/2026-07-02-held-findings-35-46.md.) "NonBlocking" UART send busy-waits inside the +4 emission task when the DIN TX FIFO fills. [Downgraded: verification found proposed fix named wrong function (HELD pending re-derivation).] |
| 21 | ✓ P2 | memory-safety | play/record | `seq_ui_inssel.c` — SEQ_UI_INSSEL_DrumTrigger :109 | **[FIXED 4b4fbd71]** Drum-pad preview note is an unmasked u8 from lay_const -> OOB write in SEQ_LIVE_PlayEvent. Fixed: note masked to 7 bits at dispatch (+ #56 guard in PlayEvent). |
| 62 | ✓ P2 | logic | render stack | `seq_core.c` — SEQ_CORE_RenderTouched / SEQ_CORE_RenderTrack :934 | **[FIXED 417cd864]** Stopped-edit synchronous render takes the SWEEP path, refreshing only a 4-step window instead of the full buffer — stale output mirror for GRIP/GRAVITY/transpose/limit edits while transport is stopped. Fixed: sweep regime is play-only; bulk CC replays suppress per-call renders (RenderSuppressSync) and keep their single end-of-load flush. |
| 65 | ✓ P2←P1 | logic | CAPTURE | `seq_core.c` — SEQ_CORE_CaptureTapeTap / metronome emit in SEQ_CORE_Tick :4741 | **[FIXED e0bb8769]** (metronome click filtered by exact port+chn+note identity (nonzero-slot guarded) on the capture tee.) Metronome click notes are captured into the pattern when the recording track is track 16 (index 15) |
| 13 | ✓ P2 | logic | CAPTURE | `seq_core.c` — SEQ_CORE_CaptureMaterializeNote :345 | **[FIXED fb0f76e6]** Captured note of an exact-integer-step duration (gate % tps == 0) is written as an unterminated Glide and over-holds into the following rests. Fixed context-sensitively (the review's blanket terminate would break legato: the tie mechanism produces exact gates ending AT the next onset): boundary onset/loop-seam → tie; in-window silence → terminate at 94. Tape pre-gates onsets; helper clamps to terminating lengths. |
| 14 | ✓ P2 | logic | CAPTURE | `seq_core.c` — SEQ_CORE_CapSpanSink :2260 | **[FIXED fb0f76e6]** Stopped re-sim capture collapses every multi-step note to a single Glide start step, losing duration and over-holding. Fixed: off back-fill routes through the materialize chain; exact-boundary offs deferred past their boundary (same-tick tie onset drains after the off); chains hand off legato at gated steps. |
| 15 | ✓ P2 | concurrency | gen+RNG | `seq_generator.c` — SEQ_GENERATOR_Engage :408 | **[FIXED 417cd864]** Engage marks slot in_use/engaged before seed, loop[], anchor[] and mult[] are initialized — emission Tick can process a half-built slot. Fixed: publish moved last, under a critical publish barrier. |
| 16 | ✓ P2 | concurrency | gen+RNG | `seq_generator.c` — mutate_loop / roll_loop / SEQ_GENERATOR_Snap :173 | **[FIXED 417cd864]** UI-thread gestures mutate a pool slot with no lock while the +4 emission Tick can preempt and mutate the same slot. Fixed: ROLL/SNAP/ANCHOR/ForceMutate/re-ENGAGE fenced per-slot. |
| 19 | ✓ P2←P1 | concurrency | save/undo | `seq_core.c` — journal_snap :3443 | **[FIXED 417cd864]** Journal snapshot memcpys seq_par/trg_layer_value with no tick exclusion — torn read vs the +4 generator write. Fixed: capture under portENTER_CRITICAL, mirroring journal_restore. |
| 20 | ✓ P2 | logic | save/undo | `seq_core.c` — SEQ_CORE_JournalUndo :3613 | **[FIXED 417cd864]** REDO silently drops generators past PERSIST_SLOTS(4) — the 'after' snapshot has no engaged-count guard. Fixed: JournalArm's truncation guard applied to both lazy captures; overflow withholds the toggle arm. |
| 45 | ✓ P2←P1 | logic | timers/sys/iic/bsl | `mios32_iic_midi.c` — _MIOS32_IIC_MIDI_PackageReceive :457 | **[FIXED e0bb8769]** (IIC MIDI rx bytes copied into the package before the callback fires.) Rx callback fed with stale/uninitialized package before received bytes are copied in |
| 38 | ✓ P2 | persistence | USB MIDI | `mios32_usb.c` — MIOS32_USB_MIDI_SIZ_CONFIG_DESC_SINGLE_USB / MIOS32_USB_ConfigDescriptor_SingleUSB :94 | **[FIXED e0bb8769]** (SingleUSB config-desc size now uses the single-port class-desc size (F4 + F10x); array/wTotalLength/GetCfgDesc agree.) SingleUSB config-descriptor size uses the 4-port class-desc size -> 96 trailing zero bytes reported to host, breaking enumeration in ForceSingleUSB mode |
| 44 | ✓ P2 | memory-safety | MIDI codec | `mios32_midi.c` — MIOS32_MIDI_SendDebugMessage :958 | **[FIXED e0bb8769]** (real bounded vsnprintf threaded through the printf-stdarg engine end-pointer; SendDebugMessage uses it.) vsprintf into fixed 128-byte stack buffer with only a format-length guard |
| 55 | P2 | rt-timing | LENS rt-timing | `seq_core.c` — SEQ_CORE_CaptureTapeTap :402 | Every note-off on the CAPTURE recording track does an unbounded-to-768 linear ring back-walk inside the MIDI drain under MUTEX_MIDIOUT |
| 54 | P2 | rt-timing | LENS rt-timing | `seq_core.c` — SEQ_CORE_Handler :4357 | Catch-up/prefetch loop can run ~120 SEQ_CORE_Tick iterations (each rendering 16 tracks) in one 1ms emission slot while holding MUTEX_MIDIOUT |
| 68 | ✓ P2 | concurrency | MIDI in/route | `seq_midi_in.c` — SEQ_MIDI_IN_ArpNoteGet :1261 | **[FIXED 417cd864]** Non-HOLD arp read latches num_notes from live ARP_SORTED.len, then indexes items across a possible concurrent NOTESTACK_Pop. Fixed writer-side: IRQ guards on the Push/Pop + hold-copy clusters (+4 reader preempts the +3 writer, so reader-side locking can't help). |
| 70 | ✓ P3←P2 | concurrency | HIL sysex | `seq_testctrl.c` — cmd_reset_state :493 | **[FIXED 417cd864]** Unprotected read-modify-write of the shared seq_core_state bitfield word can clobber +4-task transport/trigger flags. Fixed: atomic clear, matching cmd_freeze_set. |
| 4 | ✓ P2 | memory-safety | core tick+emit | `seq_core.c` — SEQ_CORE_Tick (glide_notes bitmask) :5481 | **[FIXED 4b4fbd71]** Glide/stretch note bitmask indexed by note/32 into 4-word arrays with an 8-bit note field and no defensive bound. Fixed: word index masked to 0x7f at all 4 tick sites + the ReSchedule filter (seq_midi_out.c). |
| 12 | ✓ P2 | memory-safety | layer/transforms | `seq_layer.c` — SEQ_LAYER_GetEvents (Combined mode) :708 | **[FIXED 4b4fbd71]** Combined event-mode note/chord layer reads seq_cc_trk/par arrays at track+1/track+2 without bounding track to a group base. Fixed: note/chord layers refuse group offsets 6/7. |
| 57 | ✓ P3←P2 | persistence | LENS memory | `seq_par.c` — SEQ_PAR_TrackInit :182 | **[FIXED 4b4fbd71]** Signed-int overflow in TrackInit geometry check can accept pathological layer size from a corrupt bank file. Fixed: u32 math + factor bounds (≤16 layers/instruments, no zero factors) in both SEQ_PAR_ and SEQ_TRG_TrackInit. |
| 59 | ✓ P3←P2 | resource-exhaustion | core tick+emit | `seq_core.c` — SEQ_CORE_Echo :6249 | **[FIXED e0bb8769]** (echo_repeats &= 0x3f at the consumption point; bit-7 no longer escapes the disable mask.) echo_repeats bit-7 escapes the disable mask -> 128-191 echo events per note flood the MIDI-out pool |
| 0 | ✓ P2←P1 | resource-exhaustion | clock/BPM | `seq_bpm.c` — bpm_req_clk_ctr / SEQ_BPM_Timer_Master / SEQ_BPM_ChkReqClk :105 | **[FIXED 417cd864]** u8 pending-clock counter silently wraps and drops ~256 ticks when the emission task is stalled. Fixed: widened to u32. |
| 7 | ✓ P3←P2 | logic | MIDI OUT sched | `seq_midi_out.c` — SEQ_MIDI_OUT_Send :426 | **[FIXED e0bb8769]** (OnOff len>0xffff split now emits a real OffEvent (vel 0) at the pre-delay timestamp.) OnOff with len>0xffff truncates len into u16 and re-sends as OnOff (not Off) with velocity intact -> stuck note |
| 8 | ✓ P3 | resource-exhaustion | MIDI OUT sched | `seq_core.c` — SEQ_CORE_ScheduleEvent :4030 | **[FIXED e0bb8769]** (paired sentinel Off suppressed when the On Send was dropped (per-branch on_status gate).) When an On event is dropped by pool exhaustion, its paired sentinel Off is still queued, consuming a slot for a note that never sounded |
| 60 | ✓ P3 | logic | core tick+emit | `seq_core.c` — SEQ_CORE_Tick :4600 | **[FIXED e0bb8769]** (reset_trkpos_done mask dedups the second same-tick ResetTrkPos/++bar on SYNCH_TO_MEASURE recall.) Quantized phrase-recall rephase and synch-to-measure both bump t->bar in the same measure tick -> nth-trigger phase skips a bar |
| 61 | ✓ P3 | logic | core tick+emit | `seq_core.c` — SEQ_CORE_Tick :5526 | **[FIXED e0bb8769]** (roll2 inner gatelength scaled by step_length/96 (1-tick floor); outer echo value untouched. BY-EAR: changes non-16th roll2 feel.) Roll2 flam trigger spacing/gate ignore clock divider (inner gatelength shadows the outer, never scaled by step_length) |
| 66 | ✓ P3 | logic | CAPTURE | `seq_core.c` — SEQ_CORE_CaptureDstLoopSteps / CaptureSpanTape :568 | **[FIXED e0bb8769]** (fractional-bar foreign-clkdiv SYNCH_TO_MEASURE grab refused (CaptureDstLoopSteps returns 0).) Foreign-clkdiv SYNCH_TO_MEASURE track loses the last steps of the captured bar when tps does not divide gspm*96 |
| 67 | ✓ P3 | logic | MIDI in/route | `seq_midi_in.c` — SEQ_MIDI_IN_ArpNoteGet :1264 | **[FIXED 4b4fbd71]** HOLD-mode arp num_notes counts to 0 when MIDI note 0 (C-2) is held → modulo-by-zero. Fixed: empty-stack guard returns 0x80 before the modulo (behavior-identical on this hardware, UB removed). |
| 10 | ✓ P3←P2 | resource-exhaustion | layer/transforms | `seq_groove.c` — SEQ_GROOVE_DelayGet / SEQ_GROOVE_Event :269 | **[FIXED 4b4fbd71]** Modulo-by-zero on groove num_steps loaded from SD as a multiple of 256. Fixed: clamp 1..16 at load (seq_file_g.c) + 0/>16 guards before both emission-path modulos. |
| 32 | ✓ P3←P1 | memory-safety | persistence | `seq_groove.c` — SEQ_GROOVE_DelayGet / SEQ_GROOVE_Event :269 | **[FIXED 4b4fbd71]** Groove template num_steps > 16 from MBSEQ_G.V4 causes OOB read of add_step_* arrays on the emission path _(dup of #10)_ |
| 18 | ✓ P3 | memory-safety | GRAVITY | `seq_scale.c` — SEQ_SCALE_NoteValueGet :297 | **[FIXED 4b4fbd71]** TensionBandMask feeds seq_core_global_scale to NoteValueGet, which indexes seq_scale_table with no bounds check _(dup of #11)_ |
| 11 | ✓ P3 | memory-safety | layer/transforms | `seq_scale.c` — SEQ_SCALE_NoteValueGet :297 | **[FIXED 4b4fbd71]** Out-of-bounds read of seq_scale_table when a per-track scale par-layer byte exceeds the table size. Fixed: out-of-table scale = pass-through in NoteValueGet/NextNoteInScale (covers Prev/Walk/all FTS callers); root normalized to 0..11. |
| 47 | ✓ P3←P2 | concurrency | timers/sys/iic/bsl | `mios32_iic.c` — MIOS32_IIC_Init :245 | **[FIXED e0bb8769]** (I2C2 error-IRQ install guarded by the correct MIOS32_IIC2_ENABLED macro.) I2C2 (port 0) error IRQ install guarded by wrong macro (MIOS32_IIC1_ENABLED) - copy/paste typo |
| 49 | ✓ P2 | persistence | vendor seams | `diskio.c` — disk_read / disk_write :98 | **[FIXED e0bb8769]** (diskio disk_read/disk_write treat any nonzero SectorRead/Write status as RES_ERROR.) FatFS shim tests only `< 0`, so a positive SD R1 error code is accepted as success (stale read / silently dropped write) |
| 3 | ✓ P3←P2 | logic | core tick+emit | `seq_core.c` — SEQ_CORE_Tick (gen_off_events) :5335 | **[FIXED e0bb8769]** (gen_off_events widened u8->u16 (also fixes the 256-multiple note-hang).) u8 gen_off_events truncates the scaled stretched-glide gatelength on slow clock dividers, cutting the note short |
| 5 | P3 | logic | core tick+emit | `seq_core.c` — SEQ_CORE_Tick (glide dedup vs humanize order) :5481 | Glide-note tracking stores the post-HUMANIZE note but dedups against the pre-HUMANIZE note, breaking glide continuity when humanize is active _(dup of #3)_ |
| 1 | ✓ P3←P2 | concurrency | clock/BPM | `seq_bpm.c` — SEQ_BPM_ChkReqClk :691 | **[FIXED 417cd864]** SEQ_BPM_TickSet does not clear bpm_req_clk_ctr; a stale request count can clobber a freshly-set tick position. Fixed: TickSet clears the request counter under IRQ-disable. |
| 2 | P3 | concurrency | clock/BPM | `seq_song.c` — SEQ_SONG_Fwd / SEQ_SONG_Rew :518 | **[HELD 2026-07-01]** Read-modify-write of bpm_tick in Fwd/Rew races the master timer ISR while running. Held: the prescribed IRQ guard would span SEQ_CORE_Reset (heavy, lock-taking); with #1's request-clear the clobber mechanism is closed — residual is sub-tick jump-target drift. |
| 17 | ✓ P3←P2 | rt-timing | GRAVITY | `seq_cc.c` — SEQ_CC_Set (case SEQ_CC_TENSION_GRIP) :510 | **[FIXED 417cd864]** GRIP write while stopped runs a full track render with interrupts masked. Fixed: all processor-slot syncs deferred to after SEQ_CC_Set's critical section (LinkUpdate idiom). |
| 23 | ✓ P3 | rt-timing | play/record | `seq_ui_inssel.c` — SEQ_UI_INSSEL_RecordChord :193 | **[FIXED 417cd864]** Chord-record target-step read of t->step / timestamp_next_step_ref races the emission task. Fixed: step sampled and written under MUTEX_MIDIOUT (taken at function top). |
| 52 | ✓ P3 | concurrency | LENS concurrency | `seq_ui_inssel.c` — SEQ_UI_INSSEL_RecordChord :191 | **[FIXED 417cd864]** Target step for atomic chord record is read from engine-owned t->step / timestamp_next_step_ref outside the mutex _(dup of #23)_ |
| 22 | ✓ P3 | concurrency | play/record | `seq_ui_inssel.c` — SEQ_UI_INSSEL_KbdNote :286 | **[FIXED 417cd864]** Held-note tracking arrays are not reentrancy-safe across the physical vs MIDI-remote button paths. Fixed: per-key state drained/stored atomically; sends work from the snapshot. |
| 24 | ✓ P3←P2 | concurrency | MIDI in/route | `seq_midi_in.c` — SEQ_MIDI_IN_ResetSingleTransArpStacks / SEQ_MIDI_IN_ResetChangerStacks :321 | **[FIXED 417cd864]** UI-task stack reset is unsynchronized vs the higher-priority MIDI-in mutator → torn notestack / lost or corrupted transposer/arp note. Fixed: both resets atomic under IRQ-disable. |
| 25 | ✓ P3 | memory-safety | MIDI in/route | `seq_midi_in.c` — SEQ_MIDI_IN_TransposerNoteGet / SEQ_MIDI_IN_ArpNoteGet :1199 | **[FIXED 4b4fbd71]** Bus getters index bus_notestack[bus] with no bus-range guard (unlike the sibling PC-set/lowest-note getters). Fixed: guard added to both, matching the siblings. |
| 56 | ✓ P3←P2 | memory-safety | LENS memory | `seq_live.c` — SEQ_LIVE_PlayEvent :221 | **[FIXED 4b4fbd71]** Note >127 in SEQ_LIVE_PlayEvent overruns seq_live_played_notes[4] and live_keyboard_*[128] _(dup of #21)_. Fixed: note ≥128 rejected at the top of the NoteOn branch. |
| 36 | ✓ P3←P2 | resource-exhaustion | UART MIDI | `mios32_uart.c` — USART2_IRQHandler / MIOS32_UART_RxBufferPut :775 | **[FIXED e0bb8769]** (RX buffer 64->128B + per-UART overflow counters (ORE already cleared by SR/DR read order).) RX FIFO overflow silently drops MIDI bytes, corrupting the in-flight parser event |
| 37 | ✓ P3 | concurrency | UART MIDI | `mios32_uart_midi.c` — MIOS32_UART_MIDI_Periodic_mS / MIOS32_UART_MIDI_PackageSend_NonBlocking :243 | **[FIXED e0bb8769]** (rs_expire_ctr/rs_last touch wrapped in a short MIOS32_IRQ_Disable window.) Cross-priority race on rs_expire_ctr/rs_last between the +3 periodic task and +4 emission task; the 'atomic not required' comment is inaccurate |
| 42 | ✓ P3 | concurrency | SPI/DMA/SD | `mios32_spi.c` — spi_callback :147 | **[FIXED 417cd864]** spi_callback[] is written in task context and read in the DMA ISR without volatile/barrier. Fixed: declared volatile. |
| 43 | ✓ P3 | logic | SPI/DMA/SD | `mios32_sdcard.c` — MIOS32_SDCARD_CheckAvailable :345 | **[FIXED e0bb8769]** (not-was_available fast path returns after PowerOn(); no double CS-deassert / mutex-give.) CheckAvailable re-deactivates CS and gives an already-given mutex on the not-was_available success path |
| 39 | ✓ P3 | logic | USB MIDI | `mios32_usb_midi.c` — USBH_InterfaceInit :465 | **[FIXED e0bb8769]** (USB-host endpoint loop bounded by bNumEndpoints (IN/OUT pair gate).) USB-host MIDI enumeration reads Ep_Desc[i][1] unconditionally without checking bNumEndpoints, opening a channel from stale endpoint data if the interface has only one endpoint |
| 40 | ✓ P3 | logic | USB MIDI | `mios32_usb_midi.c` — USBH_InterfaceInit :496 | **[FIXED e0bb8769]** (DeviceNotSupported() now called when the interface is NOT available (condition un-inverted).) DeviceNotSupported() is invoked when the MIDI interface IS available (inverted condition) on the USB-host path |
| 41 | P3 | logic | SPI/DMA/SD | `mios32_sdcard.c` — MIOS32_SDCARD_CIDRead :648 | CIDRead start-token timeout falls through into a spurious 16-byte DMA read on a disconnected card _(dup of #50)_ |
| 50 | ✓ P3 | logic | vendor seams | `mios32_sdcard.c` — MIOS32_SDCARD_CIDRead :648 | **[FIXED e0bb8769]** (CIDRead goto error on start-token timeout (matches CSDRead).) Missing `goto error` on start-token timeout: DMA-reads and parses garbage into the CID struct |
| 58 | P3 | resource-exhaustion | LENS exhaustion | `seq_midi_out.c` — SEQ_MIDI_OUT_Send :426 | OnOff note with len>0xffff can drop its Off when pool is near-full, leaving a stuck note _(dup of #7)_ |
| 64 | ✓ P3 | logic | MIDI OUT sched | `seq_midi_out.c` — SEQ_MIDI_OUT_Send :427 | **[FIXED e0bb8769]** (split Off scheduled from the pre-delay timestamp so port delay is applied once.) len>0xffff OnOff split schedules the Off using the already-delayed timestamp, so port delay is applied twice to the Off |
| 26 | ✓ P3 | logic | HIL sysex | `seq_testctrl.c` — cmd_page_set :526 | **[FIXED e0bb8769]** (page id >= SEQ_UI_PAGES rejected at the SysEx boundary (status 0x03).) CMD_PAGE_SET stores an unvalidated page id into the global ui_page, leaving the UI on a non-existent page with all NULL callbacks |
| 29 | ✓ P3 | logic | UI pages | `seq_ui_trkrnd.c` — RandomGenerator :518 | **[FIXED e0bb8769]** (target drum index kept local; ui_selected_instrument no longer clobbered.) RandomGenerator leaves ui_selected_instrument set to a drum index and uses a stale instrument for parameter-layer randomization |
| 33 | ✓ P3←P2 | memory-safety | persistence | `seq_file_t.c` — SEQ_FILE_T_Read :94 | **[FIXED 4b4fbd71]** Off-by-one track guard (`>` instead of `>=`) admits track==SEQ_CORE_NUM_TRACKS. Fixed in both Read and Write_Hlp. |
| 34 | ✓ P3 | logic | persistence | `seq_file_b.c` — SEQ_FILE_B_PatternRead :762 | **[FIXED 4b4fbd71]** Unchecked SEQ_PAR_TrackInit/SEQ_TRG_TrackInit return leaves stale geometry on oversized file layer config. Fixed: rejected geometry → default partition + section skim (PatternRead + TrackRead); seq_file_t EventMode re-partition also return-checked. |
| 48 | ✓ P3 | logic | timers/sys/iic/bsl | `mios32_iic_midi.c` — MIOS32_IIC_MIDI_ScanInterfaces :211 | **[FIXED e0bb8769]** (shadowed error removed in ScanInterfaces retry loop.) Shadowed 'error' in scan retry loop makes retry condition/availability logic ineffective |
| 69 | ✓ P3 | logic | MIDI in/route | `seq_midi_sysex.c` — SEQ_MIDI_SYSEX_Cmd_Remote :453 | **[FIXED e0bb8769]** (REFRESH gate compares REMOTE_CMD == SYSEX_REMOTE_CMD_REFRESH.) Remote refresh detection compares REMOTE_CMD to REMOTE_CMD_COMPLETE and &&'s a nonzero constant, so the 'is REFRESH' gate is wrong |
| 71 | ✓ P3 | logic | HIL sysex | `seq_testctrl.c` — SEQ_TESTCTRL_Parser :2721 | **[FIXED e0bb8769]** (F0 mid-payload resyncs the TESTCTRL parser to IDLE instead of being stored as data.) A new F0 arriving mid-payload is stored as a data byte instead of resyncing the parser, swallowing the next command |

_Severity shown as `verified←proposed` when the refutation gate changed the finder's tier._

## Cross-cutting themes

These are the structural patterns the individual findings share — fixing the *pattern* is usually cheaper and more durable than fixing each finding.

### Unlocked ISR/task shared-state races

State shared across the +4 emission task/ISR, +3 MIDI-in/SysEx tasks, and +2 UI task is read-modify-written without IRQ masking or the correct mutex. The new play surface and generator pool widened classic single-writer assumptions into real cross-priority races: torn bpm_tick RMW, half-built/concurrently-mutated generator slots, torn journal snapshots, torn live-note bitmaps (stuck notes), unsynchronized notestack reset, unguarded chord-record step reads, and an unprotected state-bitfield RMW.

_Findings: #0, #1, #2, #3, #15, #16, #19, #20, #22, #23, #24, #42, #51, #52, #68, #70_

**Status: theme largely CLOSED (2026-07-01, commit 417cd864).** All findings fixed except **#2 HELD** (the prescribed IRQ guard would span SEQ_CORE_Reset — heavy, lock-taking; with #1's TickSet request-clear the stale-count clobber is closed, residual is sub-tick jump-target drift). (#3, listed here as a cross-reference, is a clock-divider logic bug not a race — FIXED e0bb8769; see the clock-divider theme.)

### Render double-buffer single-writer contract broken while playing

The output-mirror double-buffer (active/inactive halves + XOR flip) assumes a single writer and that the tick never sees a half-rendered buffer. Bounce-while-playing, stopped-edit sweep-window rendering, and the interrupts-masked GRIP synchronous render all break that contract, producing torn par/trg reads (wrong live pitch/gate) or stale output mirrors for LCD/audition.

_Findings: #6, #17, #62_

**Status: theme CLOSED (2026-07-01).** #6 fixed in ff2505c5; #17 (slot syncs deferred out of SEQ_CC_Set's critical section) and #62 (stopped flush = full render, sweep regime play-only, + RenderSuppressSync for bulk CC replays) fixed in 417cd864.

### Unbounded index / geometry from SD files and SysEx

Loaded pattern/song/groove/track-preset files and SysEx-set CC bytes are trusted without adequate bounds or geometry validation, driving wild writes, OOB reads, and modulo-by-zero on the emission path: track-preset addr offset, song_size underflow, groove num_steps truncation/overrun, drum-instrument-count overrun, scale-table index, combined-mode cross-track reads, off-by-one track guard, signed-overflow geometry, and unmasked note bytes overrunning live-play arrays.

_Findings: #4, #9, #10, #11, #12, #21, #30, #31, #32, #33, #34, #56, #57, #67_

**Status: theme CLOSED (2026-07-01).** #30/#31 fixed in ff2505c5 (P1 wave); the rest landed as the input-hardening pass 4b4fbd71. The durable pattern: `SEQ_PAR_TrackInit`/`SEQ_TRG_TrackInit` are now the geometry gate (u32 math, ≤16 layers/instruments, no zero factors) and loaders honor the reject; scale/groove/note bytes are bounded at their own entry points.

### MIDI-out scheduler sentinel/len/delay corners -> stuck or cut notes

The sorted MIDI-out queue mishandles the 0xffffffff parked-Off sentinel and the OnOff split path: positive per-port MClk delay wraps the sentinel and fires sustained note-offs instantly; len>0xffff truncates or re-sends as On (stuck note) and double-applies delay; pool-exhaustion paths queue orphan sentinel Offs or drop the paired Off. Result: stuck or prematurely-cut live notes.

_Findings: #7, #8, #58, #63, #64, #59_

**Status: theme CLOSED (2026-07-02, commit e0bb8769).** #63 fixed in the P1 wave (ff2505c5); #7/#58/#64 (OnOff len>0xffff split now emits a real vel-0 OffEvent at the pre-delay timestamp — no stuck note, no double delay, no premature real-Off from the u16 residue), #8 (paired sentinel Off suppressed when the On Send was dropped, per-branch on_status gate, never-stuck direction preserved), and #59 (echo_repeats &= 0x3f at the consumption point) landed in the quick-win sweep. Latent for #7/#8 (no in-fork caller passes len>0xffff or hits sustained pool exhaustion today) — not HIL-exercisable without a synthetic caller.

### CAPTURE fidelity: durations, dropped tail steps, phantom events

Capture/materialize paths corrupt the deliverable: exact-integer-step and multi-step notes are written as unterminated Glides that over-hold across rests (both tape and re-sim sinks); foreign-clkdiv SYNCH_TO_MEASURE tracks silently drop the last steps of each bar; metronome clicks on track 16 get captured as real notes; and the note-off back-walk is an unbounded 768-entry linear scan on the hot MIDI drain.

_Findings: #13, #14, #55, #65, #66_

**Status: theme largely CLOSED (2026-07-02).** #13/#14 fixed in fb0f76e6; #65 (metronome click filtered off the capture tee by exact port+chn+note identity, nonzero-slot guarded) and #66 (fractional-bar foreign-clkdiv SYNCH_TO_MEASURE grab refused — CaptureDstLoopSteps returns 0, all four callers already treat 0 as a clean pre-mutation refusal) landed in e0bb8769. **#55 remains open** — the note-off back-walk is a hot-path cost that wants measurement before restructuring (paired with #54; see the blocking-work theme).

### Blocking/unbounded work on the real-time emission path

The +4 emission task performs blocking or unbounded work while holding MUTEX_MIDIOUT or inside portENTER_CRITICAL: synched pattern change does multi-sector blocking SD reads with interrupts masked, the catch-up loop runs ~120 full renders in one slot, the 'NonBlocking' UART send busy-waits, and RX FIFO overflow drops MIDI bytes. These starve the MIDI drain, causing late-note bursts and timing glitches.

_Findings: #35, #36, #53, #54_

**Status: theme partially closed.** #53 fixed in 905f93a3 (Tier-1: dropped outer critical section, drift-gated writeback). #36 fixed in e0bb8769 (RX buffer 64→128B via the app config override + per-UART overflow counters; ~41ms of headroom against +3 starvation, ORE already cleared by the SR-then-DR read order). **#35 HELD** — re-derived 2026-07-02 and confirmed real (MIOS32_UART_MIDI_PackageSend_NonBlocking calls the *blocking* MIOS32_UART_TxBufferPutMore, spinning the +4 task at DIN line rate while holding MUTEX_MIDIOUT; the review's original one-line fix was wrong twice — it relocated the spin and corrupted running-status state). A safe fix needs three coupled layers (driver free-space check before RS mutation, Handler defer-on-−2 never-drop, FlushQueue off-preservation) spanning shared platform + module code with DIN-rig by-ear validation the USB HIL cannot provide; full plan in doc/plans/2026-07-02-held-findings-35-46.md. **#54 open** (catch-up prefetch — measure-first, paired with #55).

### Unbounded printf-family format strings and buffers

User- and format-controlled data reaches printf-family functions without output bounding: a user-set pattern name is passed as the format string to sprintf into a 21-byte buffer (%n/%s corruption), and SendDebugMessage vsprintfs into a 128-byte stack buffer guarded only by format length.

_Findings: #28, #44_

**Status: theme CLOSED (2026-07-02).** #28 fixed in ff2505c5 (name copies → bounded memcpy). #44 fixed in e0bb8769: the platform links printf-stdarg.c (which overrides the printf family and provides no real vsnprintf), so the naive newlib swap would silently link a duplicate format engine — instead a `char *end` bound was threaded through the engine's single write primitive (end==0 keeps printf/vprintf/sprintf/vsprintf bit-identical), a real bounded vsnprintf added, snprintf delegated to it, and SendDebugMessage now vsnprintf's into its 128-byte buffer.

### Platform/driver correctness (USB, bootloader, IIC, SPI, FatFS/SD)

MIOS32 platform-layer defects independent of the sequencer: SingleUSB config descriptor over-reports 96 zero bytes breaking enumeration; USB-host MIDI enum reads stale endpoint desc and inverts DeviceNotSupported; bootloader flash writes without sector-aligned erase or verify (silent bricking); IIC error-IRQ guarded by the wrong macro plus a shadowed retry error; SPI callback lacks volatile/barrier; SD CIDRead misses a goto error; and the FatFS/SD shim accepts positive R1 error codes as success (silent stale read / dropped write).

_Findings: #38, #39, #40, #41, #43, #45, #46, #47, #48, #49, #50_

**Status: theme largely CLOSED (2026-07-02, commit e0bb8769).** #38 (SingleUSB config-desc size uses the single-port class-desc size — F4 + F10x), #39/#40 (USB-host endpoint loop bounded by bNumEndpoints + un-inverted DeviceNotSupported), #43 (CheckAvailable fast-path double mutex-give / CS-deassert), #45 (IIC MIDI rx bytes copied before the callback), #47 (I2C2 error-IRQ guarded by the correct MIOS32_IIC2_ENABLED macro), #48 (shadowed `error` in ScanInterfaces), #49 (diskio treats nonzero SectorRead/Write status as RES_ERROR), and #50/#41 (CIDRead goto-error on start-token timeout, the two are duplicates) all fixed. **#46 HELD** — re-derived real (off-base erase + no verify in BSL_SYSEX_WriteMem), but the copy that runs during a MIOS Studio upload is the sector-0-resident bootloader binary (the app does not link bsl_sysex.c), so an app-side edit is inert and deploying a real fix means a live sector-0 reflash that cannot be proven safe without hardware; full plan in doc/plans/2026-07-02-held-findings-35-46.md. IIC files (#45/#47/#48) are platform hygiene — reachability depends on the IIC build config. USB-host path (#39/#40) is live in this rig.

### Clock-divider scaling and phase-bookkeeping logic errors

Tick-time computations mishandle non-16th clock dividers and phase state: stretched-glide gatelength truncates into a u8 (cutting notes short), roll2 flam spacing ignores step_length via a shadowed inner variable, and combined phrase-recall rephase plus synch-to-measure double-increment t->bar, skipping an nth-trigger bar.

_Findings: #3, #60, #61_

**Status: theme CLOSED (2026-07-02, commit e0bb8769).** #3 (gen_off_events widened u8→u16 — also fixes the exact-256-multiple case that had *hung* the note by skipping the reschedule block), #60 (a function-local mask records which tracks block-A reset this tick so the synch-to-measure block skips the second same-tick ResetTrkPos/++bar), and #61 (roll2 inner gatelength scaled by step_length/96 with a 1-tick floor; outer echo-facing value untouched) all fixed. **By-ear flags:** #61 changes audible roll2 feel on non-16th-divider tracks (toward the intended scaling — 16th tracks are bit-identical); #60 leaves the LFO's same-tick advance intact on synch tracks (correct single-reset semantics).

### SysEx / test-control parser and remote framing defects

TESTCTRL/remote SysEx paths mishandle framing and validation: an unvalidated page id parks the UI on a wild page with NULL callbacks (dead control surface), a mid-payload F0 is swallowed as data instead of resyncing (next command lost), and the remote refresh gate compares the wrong constants so the refresh echo fires on the wrong criterion.

_Findings: #26, #69, #71_

**Status: theme CLOSED (2026-07-02, commit e0bb8769).** #26 (CMD_PAGE_SET rejects page ≥ SEQ_UI_PAGES at the SysEx boundary with status 0x03, leaving UI state untouched), #69 (REFRESH gate now compares REMOTE_CMD == SYSEX_REMOTE_CMD_REFRESH), and #71 (a mid-payload F0 resyncs the TESTCTRL parser to IDLE instead of being stored as data) all fixed. #26/#71 are TESTCTRL surface (default build, `make TESTCTRL=0` compiles them out); #71 directly improves HIL harness reliability.

## Remediation plan (sequenced)

Waves are the lead-reviewer's ordering across the *whole* set — a wave label reflects blast-radius and reachability, which can differ from a single finding's tier. Within a wave, cheap point-fixes precede structural ones.

### Wave: P1 corruption/crash — user-reachable now

These are P1 memory-safety/logic defects reachable through normal use or plausible external MIDI/files, causing wild writes, bricking, or corruption. Quick wins first: #28 (printf-as-format) and #30/#31 (add bounds/erase checks on file load) are small, localized patches. #27 (clamp BLM button_column) is a one-line clamp against external MIDI. #46 (bootloader sector-aligned erase + verify) is contained but load-bearing — silent bricking. #6 (bounce-while-playing render race) is structural and closes the wave: it needs the render double-buffer locking decision, not a one-liner.

- **#28 [P1]** Pattern-name copies use the name as a printf format string (sprintf(dst, src)) — %-specifiers in a user-set name deref wild stack args  
  ↳ _`seq_ui_pattern_remix.c` — Encoder_Handler/Button_Handler (sprintf name copies) :459_ — Replace every sprintf(dst, src) name copy with strncpy(dst, src, 20); dst[20]=0; (or sprintf(dst, "%.20s", src)). Same pattern also appears in seq_ui_save.c-style copies — audit all sprintf-with-nonliteral-format.
- **#27 [P1]** BLM keyboard mode indexes 16-element note arrays with un-clamped button_column (0..63) -> OOB read/write from external MIDI  
  ↳ _`seq_blm.c` — SEQ_BLM_BUTTON_GP_KeyboardMode :925_ — In SEQ_BLM_ButtonCallback GRID case, reject or fold button_x >= num_columns before dispatch (or bound button_column < 16 at the top of SEQ_BLM_BUTTON_GP_KeyboardMode, matching the num_instruments guard used in GridMode).
- **#30 [P1]** Track-preset 'Par'/'Trg' address offset causes a wild write into RAM past the layer row  
  ↳ _`seq_file_t.c` — SEQ_FILE_T_Read :190_ — Change the guard to `if( (par_layer && addr_offset+16 > SEQ_PAR_MAX_BYTES) || (!par_layer && addr_offset+16 > SEQ_TRG_MAX_BYTES) ) { DEBUG_MSG(...); continue; }` so the out-of-range line is skipped entirely and the last 16-byte group can't straddle the array end.
- **#31 [P1]** Corrupt song bank with song_size < 20 underflows num_entries and overruns seq_song_steps[128]  
  ↳ _`seq_file_s.c` — SEQ_FILE_S_SongRead :369_ — In Open, reject song_size < sizeof(seq_file_s_song_header_t); in SongRead, compute num_entries only when song_size >= sizeof(header) and additionally clamp num_entries to SEQ_SONG_NUM_STEPS before the loop.
- **#46 [P2]** STM32F4 flash write only erases on exact sector-base match and never verifies -> silent corrupt program on non-base write  
  ↳ _`bsl_sysex.c` — BSL_SYSEX_WriteMem :574_ — Erase the sector that CONTAINS addr (base<=addr<next_base) rather than requiring addr==base, and/or add the read-back verify the TODO promises before returning ACK.
- **#6 [P1]** Unguarded SEQ_CORE_RenderTrack in the capture/bounce primitive races the emission task's double-buffer flip -> torn output mirror = wrong live pitch/gate  
  ↳ _`seq_core.c` — SEQ_CORE_CaptureTrackOutput / SEQ_CORE_ProcessorBounce :1922_ — Either gate the direct render in CaptureTrackOutput behind !SEQ_BPM_IsRunning() and rely on the tick to render when playing, or wrap the render+flip in a short critical section / suspend the emission task for the duration. At minimum, ProcessorBounce/GP8 should defer to the tick when SEQ_BPM_IsRunning() rather than force a synchronous UI-task render of a live track.

### Wave: P1 audible — stuck / cut / lost live notes

P1 defects that a performer will hear: #63 (positive MClk delay wraps sentinel -> every sustained note cut) and #51 (torn live-note bitmap -> stuck MIDI-in note) are localized and high-impact — do first. #35 (UART busy-wait) and #53 (synched-switch blocking SD read under critical section) are the RT-timing walls the fork already tracks; they are structural (yield/relocate I/O off the +4 path) and belong at the end of this wave after the quick sentinel/bitmap fixes.

- **#63 [P1]** Positive per-port MClk delay wraps the 0xffffffff sentinel Note-Off timestamp → sustained/stretched notes cut off instantly  
  ↳ _`seq_midi_out.c` — SEQ_MIDI_OUT_Send :331_ — Skip the delay adjustment for the reserved sentinel timestamp (e.g. if timestamp==0xffffffff, do not add delay), or saturate: if (timestamp > 0xffffffff - delay) timestamp = 0xffffffff. Mirror the existing negative-side clamp on the positive side.
- **#51 [P2]** Unguarded read-modify-write on seq_live_played_notes[]/live_keyboard_*[] widened to a +2/+3 cross-task race by the new play surface  
  ↳ _`seq_live.c` — SEQ_LIVE_PlayEvent :226_ — Wrap the played-notes bitmap RMW + keyboard-array update (seq_live.c:226-241 and the effective_note store block) in MUTEX_MIDIOUT (recursive, so nesting the existing send-mutex is safe) or a short portENTER_CRITICAL around the bitmap word update.
- **#35 [P1]** "NonBlocking" UART send busy-waits inside the +4 emission task when the 64-byte DIN TX FIFO fills  
  ↳ _`mios32_uart_midi.c` — MIOS32_UART_MIDI_PackageSend_NonBlocking :306_ — Call MIOS32_UART_TxBufferPutMore_NonBlocking at line 306 so -2 propagates; have the emission drain treat -2 as 'try again next tick' (leave the item on midi_queue) instead of blocking. Note this busy-wait characteristic is inherited from MIOS32 mainline, so confirm the emission-side backpressure handling before changing the shared driver.
- **#53 [P1]** Synched pattern change does blocking multi-sector SD read inside portENTER_CRITICAL + MUTEX_MIDIOUT on the +4 emission task, stalling the MIDI drain for tens of ms  
  ↳ _`seq_pattern.c` — SEQ_PATTERN_Handler :1440_ — Do not call SEQ_PATTERN_Handler (which performs SD I/O) from SEQ_CORE_Handler's tick loop. Service the switch from the +2 task (as SEQ_TASK_Period1mS already does), or restructure the load so the SD read happens with interrupts ON and outside MUTEX_MIDIOUT, keeping only the RAM swap in the critical section.

### Wave: P2 robustness — reachable degradation

P2 defects that corrupt output, races, or degrade under contention but are either recoverable or need specific conditions. Quick wins first: #44 (bound vsprintf), #21/#9/#12/#57 (bounds/mask clamps on load and drum-count), #38 (fix SingleUSB descriptor length), #45 (reorder IIC callback after copy), #65 (metronome capture guard) — all localized. Then the concurrency/structural set: generator-slot and journal races (#15,#16,#19,#20), capture over-hold fidelity (#13,#14), render staleness (#62), arp num_notes race (#68), reset-state RMW (#70), and the two hot-path timing costs (#54,#55) which want measurement before restructuring.

- **#21 [P2]** Drum-pad preview note is an unmasked u8 from lay_const -> OOB write in SEQ_LIVE_PlayEvent  
  ↳ _`seq_ui_inssel.c` — SEQ_UI_INSSEL_DrumTrigger :109_ — Mask the note to 7 bits before dispatch: p.note = tcc->lay_const[0*16 + drum] & 0x7f; (and/or bound-check note_ix32 in SEQ_LIVE_PlayEvent). The record branch is already safe via SEQ_RECORD_Receive's &=0x7f.
- **#62 [P2]** Stopped-edit synchronous render takes the SWEEP path, refreshing only a 4-step window instead of the full buffer — stale output mirror for GRIP/GRAVITY/transpose/limit edits while transport is stopped  
  ↳ _`seq_core.c` — SEQ_CORE_RenderTouched / SEQ_CORE_RenderTrack :934_ — When !SEQ_BPM_IsRunning(), force the quiet full-render path (e.g. in RenderTouched clear seq_render_touched_ms before the synchronous SEQ_CORE_RenderTrack, or have RenderTrack ignore the sweep regime while stopped) so the whole [0,used) region is rebuilt and dirty is cleared. Playing behavior (sweep window follows the playhead, full catch-up after 50ms) is unaffected.
- **#65 [P2]** Metronome click notes are captured into the pattern when the recording track is track 16 (index 15)  
  ↳ _`seq_core.c` — SEQ_CORE_CaptureTapeTap / metronome emit in SEQ_CORE_Tick :4741_ — Filter the metronome out of the tape: either tap the port/event-type as well (the metronome uses seq_core_metronome_port which differs from a track's assigned port, or tag it distinctly), or in SEQ_CORE_CaptureTapeTap reject packages that don't originate from the track emission path (e.g. add a dedicated non-track sentinel cable/tag for the metronome instead of reusing 15, or gate the tap on a per-track emission flag rather than cable==15).
- **#13 [P2]** Captured note of an exact-integer-step duration (gate % tps == 0) is written as an unterminated Glide and over-holds into the following rests  
  ↳ _`seq_core.c` — SEQ_CORE_CaptureMaterializeNote :345_ — When rem==0 and full>=1, the note ends exactly on the step0+full boundary: the last carried step (step0+full-1) must NOT be Gld — write it at a sub-Gld length that terminates on the step boundary (e.g. par value <=94), or drop the final carried step to a normal max-non-tie length. Equivalently, treat gate that is an exact multiple as (full-1) carried Gld steps plus a final step whose length terminates at the step end.
- **#14 [P2]** Stopped re-sim capture collapses every multi-step note to a single Glide start step, losing duration and over-holding  
  ↳ _`seq_core.c` — SEQ_CORE_CapSpanSink :2260_ — Route the re-sim sink's precise-gate write through the same multi-step chain as SEQ_CORE_CaptureMaterializeNote (write carried Gld steps + a terminating tail), rather than a single clamped length value on the on-step. The open-note off back-fill already knows on_tick/step and gate, so the chain can be materialized when the off drains.
- **#15 [P2]** Engage marks slot in_use/engaged before seed, loop[], anchor[] and mult[] are initialized — emission Tick can process a half-built slot  
  ↳ _`seq_generator.c` — SEQ_GENERATOR_Engage :408_ — Set g->in_use = 1 (and g->engaged) LAST, after seed/seed_loop/anchor/mult are all populated (mirror the alloc_slot contract). Since pool walks key on in_use, deferring only that one store to after line 426 closes the window without needing a critical section.
- **#16 [P2]** UI-thread gestures mutate a pool slot with no lock while the +4 emission Tick can preempt and mutate the same slot  
  ↳ _`seq_generator.c` — mutate_loop / roll_loop / SEQ_GENERATOR_Snap :173_ — Serialize slot mutation: either run the UI gestures under a short taskENTER_CRITICAL / the same mutex the emission Tick would honor, or bounce the gesture into the emission task. No memory-corruption (32-bit seed store is atomic and loop[] bytes stay valid pitches), so this is a correctness/feel defect, not a crash — hence P2.
- **#19 [P2]** Journal snapshot memcpys seq_par/trg_layer_value with no tick exclusion — torn read vs the +4 generator write  
  ↳ _`seq_core.c` — journal_snap :3443_ — Wrap the par/trg (and generator) capture in journal_snap in portENTER_CRITICAL()/portEXIT_CRITICAL() — mirroring journal_restore. The bpm ISR is at MIOS32_IRQ_PRIO_HIGHEST(4) > configMAX_SYSCALL_INTERRUPT_PRIORITY(5) so the clock is unaffected; the critical section only excludes the +4 emission task's generator writes, which is exactly what is needed.
- **#20 [P2]** REDO silently drops generators past PERSIST_SLOTS(4) — the 'after' snapshot has no engaged-count guard  
  ↳ _`seq_core.c` — SEQ_CORE_JournalUndo :3613_ — Apply the same SEQ_GENERATOR_TrackEngagedCount > SEQ_GENERATOR_PERSIST_SLOTS check before the lazy after/before captures (lines 3613/3648); on overflow, either invalidate the redo arm or refuse the toggle rather than capture a truncated snapshot.
- **#45 [P2]** Rx callback fed with stale/uninitialized package before received bytes are copied in  
  ↳ _`mios32_iic_midi.c` — _MIOS32_IIC_MIDI_PackageReceive :457_ — Move the package->type/evnt0..2 assignment (lines 463-466) to immediately after the successful TransferWait and BEFORE the SendPackageToRxCallback call; pass the freshly-filled *package.
- **#38 [P2]** SingleUSB config-descriptor size uses the 4-port class-desc size -> 96 trailing zero bytes reported to host, breaking enumeration in ForceSingleUSB mode  
  ↳ _`mios32_usb.c` — MIOS32_USB_MIDI_SIZ_CONFIG_DESC_SINGLE_USB / MIOS32_USB_ConfigDescriptor_SingleUSB :94_ — Change line 94 to reference MIOS32_USB_MIDI_SIZ_CLASS_DESC_SINGLE_USB instead of MIOS32_USB_MIDI_SIZ_CLASS_DESC. This corrects both the array allocation (line 653) and the wTotalLength bytes (lines 657-658).
- **#44 [P2]** vsprintf into fixed 128-byte stack buffer with only a format-length guard  
  ↳ _`mios32_midi.c` — MIOS32_MIDI_SendDebugMessage :958_ — Use vsnprintf(str, sizeof(str), format, args) instead of vsprintf.
- **#55 [P2]** Every note-off on the CAPTURE recording track does an unbounded-to-768 linear ring back-walk inside the MIDI drain under MUTEX_MIDIOUT  
  ↳ _`seq_core.c` — SEQ_CORE_CaptureTapeTap :402_ — Bound the back-walk to a small recent window (open note-ons are recent), or key note-ons by note number so the off match is O(1); at minimum cap the scan depth well below 768.
- **#54 [P2]** Catch-up/prefetch loop can run ~120 SEQ_CORE_Tick iterations (each rendering 16 tracks) in one 1ms emission slot while holding MUTEX_MIDIOUT  
  ↳ _`seq_core.c` — SEQ_CORE_Handler :4357_ — Bound the per-service prefetch (process at most N ticks per call and carry the remainder), and/or release/re-acquire MUTEX_MIDIOUT between prefetched ticks so the drain and UI are not starved for the whole batch.
- **#68 [P2]** Non-HOLD arp read latches num_notes from live ARP_SORTED.len, then indexes items across a possible concurrent NOTESTACK_Pop  
  ↳ _`seq_midi_in.c` — SEQ_MIDI_IN_ArpNoteGet :1261_ — Snapshot len into a local and clamp the index against that snapshot, or take MUTEX_MIDIIN / a short critical section around the len-read plus index (matching the writer's locking).
- **#70 [P3]** Unprotected read-modify-write of the shared seq_core_state bitfield word can clobber +4-task transport/trigger flags  
  ↳ _`seq_testctrl.c` — cmd_reset_state :493_ — Wrap the `seq_core_state.FREEZE = 0;` at line 493 in portENTER_CRITICAL()/portEXIT_CRITICAL() (or MIOS32_IRQ_Disable/Enable), matching cmd_freeze_set.
- **#9 [P2]** Drum-mode event loop overruns layer_events[16] when a track reports >16 instruments  
  ↳ _`seq_layer.c` — SEQ_LAYER_GetEvents (drum-mode note loop) :422_ — Clamp num_instruments to the array size in the drum loop (add `if(num_events >= 16) break;` after each ++num_events, matching the melodic guard), AND bound ParInstruments/TrgInstruments to 16 at load in seq_file_t.c before SEQ_*_TrackInit.
- **#12 [P2]** Combined event-mode note/chord layer reads seq_cc_trk/par arrays at track+1/track+2 without bounding track to a group base  
  ↳ _`seq_layer.c` — SEQ_LAYER_GetEvents (Combined mode) :708_ — Guard the Combined path so the note/chord layer only proceeds when (track&7)==0 (or bound track+1/track+2 to the track's group), and reject/clamp Combined event_mode at non-base tracks on load.
- **#57 [P3]** Signed-int overflow in TrackInit geometry check can accept pathological layer size from a corrupt bank file  
  ↳ _`seq_par.c` — SEQ_PAR_TrackInit :182_ — Compute the product as u32 (or check each factor: instruments<=SEQ_CORE_NUM..., layers<=16, steps<=1024) before comparing to SEQ_PAR_MAX_BYTES; reject and leave prior geometry untouched on any out-of-range factor. Same pattern applies to SEQ_TRG_TrackInit.

### Wave: P3 robustness — latent / narrow-corner

P3 defects: latent (unreachable through current fork code but the guard is genuinely absent), narrow-corner (extreme len/BPM/config), or low-impact logic/timing slips. Batch the cheap defensive clamps and one-line fixes together (bounds guards #4,#11,#18,#25,#33,#56; div-by-zero #10,#32,#67; goto/typo/shadow fixes #41,#47,#48,#50,#69,#71; off-by-one/inverted conditions #40). Address the shared-word RMW and clock-divider logic (#0,#1,#2,#3,#60,#61) opportunistically alongside the same files touched in earlier waves. Several here are best fixed as a single hardening sweep over file/SysEx input rather than one-by-one.

- **#0 [P2]** u8 pending-clock counter silently wraps and drops ~256 ticks when the emission task is stalled  
  ↳ _`seq_bpm.c` — bpm_req_clk_ctr / SEQ_BPM_Timer_Master / SEQ_BPM_ChkReqClk :105_ — Widen bpm_req_clk_ctr to u32 (or, on drain, reconcile against bpm_tick: derive outstanding = bpm_tick - last_processed_tick instead of trusting an 8-bit request count) so a multi-hundred-ms stall replays the missed span instead of dropping it.
- **#1 [P3]** SEQ_BPM_TickSet does not clear bpm_req_clk_ctr; a stale request count can clobber a freshly-set tick position  
  ↳ _`seq_bpm.c` — SEQ_BPM_ChkReqClk :691_ — Clear bpm_req_clk_ctr (and sent_clk_ctr) inside SEQ_BPM_TickSet under IRQ-disable, or have repositioning callers do so, so a reposition starts from a clean request state.
- **#2 [P3]** Read-modify-write of bpm_tick in Fwd/Rew races the master timer ISR while running  
  ↳ _`seq_song.c` — SEQ_SONG_Fwd / SEQ_SONG_Rew :518_ — Wrap the read-compute-write in MIOS32_IRQ_Disable/Enable, matching the pattern already used in SEQ_BPM_Start/Stop/Cont.
- **#3 [P3]** u8 gen_off_events truncates the scaled stretched-glide gatelength on slow clock dividers, cutting the note short  
  ↳ _`seq_core.c` — SEQ_CORE_Tick (gen_off_events) :5335_ — Widen gen_off_events to u16 (it is used as a tick offset at lines 5341, not just a flag), and mirror the same at the line 5234 assignment. Verify no other consumer relies on it being a byte.
- **#4 [P2]** Glide/stretch note bitmask indexed by note/32 into 4-word arrays with an 8-bit note field and no defensive bound  
  ↳ _`seq_core.c` — SEQ_CORE_Tick (glide_notes bitmask) :5481_ — Mask the index (p->note & 0x7f)/32, or guard the sustained/glide store/read with if(p->note < 128). Cheapest is to clamp note to 0x7f at the point events enter this loop.
- **#5 [P3]** Glide-note tracking stores the post-HUMANIZE note but dedups against the pre-HUMANIZE note, breaking glide continuity when humanize is active  
  ↳ _`seq_core.c` — SEQ_CORE_Tick (glide dedup vs humanize order) :5481_ — Key glide tracking on the pre-FX note (snapshot before SEQ_HUMANIZE_Event, as prefx_note is already captured at line 5272 for the FTS-change path) consistently for both the compare and the store, or disable note-humanize for glided steps.
- **#7 [P3]** OnOff with len>0xffff truncates len into u16 and re-sends as OnOff (not Off) with velocity intact -> stuck note  
  ↳ _`seq_midi_out.c` — SEQ_MIDI_OUT_Send :426_ — In the len>0xffff branch, store the On item with len=0 and issue the tail as SEQ_MIDI_OUT_OffEvent with velocity=0 at timestamp+len (matching the correct Off construction in the Handler and ScheduleEvent). Currently no in-fork caller passes u32 len>0xffff to an OnOff event (seq_core gatelength is u16; only metronome/roll use OnOff, all with small len), so this is latent, not live — but it is a real correctness/stuck-note trap for any future long-OnOff caller.
- **#8 [P3]** When an On event is dropped by pool exhaustion, its paired sentinel Off is still queued, consuming a slot for a note that never sounded  
  ↳ _`seq_core.c` — SEQ_CORE_ScheduleEvent :4030_ — Only schedule the paired Off when the On actually queued (check the status return of the On Send before scheduling the sentinel Off). Low priority: the design doc explicitly accepts 'missing note, never stuck', and the reserved-2-slots policy plus FlushQueue limit the blast radius.
- **#10 [P3]** Modulo-by-zero on groove num_steps loaded from SD as a multiple of 256  
  ↳ _`seq_groove.c` — SEQ_GROOVE_DelayGet / SEQ_GROOVE_Event :269_ — Validate the value after truncation (reject if (u8)num_steps==0 or clamp to 1..16) in seq_file_g.c, and/or guard `if(!g->num_steps) return 0;` before the modulo in both SEQ_GROOVE_DelayGet and SEQ_GROOVE_Event.
- **#11 [P3]** Out-of-bounds read of seq_scale_table when a per-track scale par-layer byte exceeds the table size  
  ↳ _`seq_scale.c` — SEQ_SCALE_NoteValueGet :297_ — Clamp scale to SEQ_SCALE_NumGet()-1 at the top of SEQ_SCALE_NoteValueGet (and NextNoteInScale/PrevNoteInScale), or clamp *scale in SEQ_CORE_FTS_GetScaleAndRoot.
- **#17 [P3]** GRIP write while stopped runs a full track render with interrupts masked  
  ↳ _`seq_cc.c` — SEQ_CC_Set (case SEQ_CC_TENSION_GRIP) :510_ — Move the SlotSync/RenderTouched call outside the critical section (mirror SEQ_CC_LinkUpdate which is deliberately called after portEXIT_CRITICAL at seq_cc.c:299-302), or have the stopped-path render defer to a non-critical context. Note this is a shared idiom across SlotSync CC writes, not unique to TENSION_GRIP.
- **#18 [P3]** TensionBandMask feeds seq_core_global_scale to NoteValueGet, which indexes seq_scale_table with no bounds check  
  ↳ _`seq_scale.c` — SEQ_SCALE_NoteValueGet :297_ — Clamp scale to SEQ_SCALE_NumGet()-1 in SEQ_SCALE_NoteValueGet (the durable fix, benefits FTS too) or in TensionBandMask before the scaleMask loop. Out-of-cluster ownership but reachable from the GRAVITY field.
- **#22 [P3]** Held-note tracking arrays are not reentrancy-safe across the physical vs MIDI-remote button paths  
  ↳ _`seq_ui_inssel.c` — SEQ_UI_INSSEL_KbdNote :286_ — Guard the per-key held-note read-modify-write (clear+store, and the release drain) with MIOS32_IRQ_Disable/Enable, or serialize both button sources through one task.
- **#23 [P3]** Chord-record target-step read of t->step / timestamp_next_step_ref races the emission task  
  ↳ _`seq_ui_inssel.c` — SEQ_UI_INSSEL_RecordChord :193_ — Snapshot t->step and t->timestamp_next_step_ref together under MIOS32_IRQ_Disable/Enable (or portENTER_CRITICAL) before computing the snap, matching how the stock record path treats these under the surrounding IRQ-disabled sections.
- **#24 [P3]** UI-task stack reset is unsynchronized vs the higher-priority MIDI-in mutator → torn notestack / lost or corrupted transposer/arp note  
  ↳ _`seq_midi_in.c` — SEQ_MIDI_IN_ResetSingleTransArpStacks / SEQ_MIDI_IN_ResetChangerStacks :321_ — Wrap the notestack Init/Clear + hold-note reseed in SEQ_MIDI_IN_ResetSingleTransArpStacks (and the changer reset) in portENTER_CRITICAL()/portEXIT_CRITICAL() so it is atomic against both the +3 MIDI-in Push/Pop and +4 emission access, matching the existing atomic block at seq_midi_in.c:390-396.
- **#25 [P3]** Bus getters index bus_notestack[bus] with no bus-range guard (unlike the sibling PC-set/lowest-note getters)  
  ↳ _`seq_midi_in.c` — SEQ_MIDI_IN_TransposerNoteGet / SEQ_MIDI_IN_ArpNoteGet :1199_ — Add 'if(bus >= SEQ_MIDI_IN_NUM_BUSSES) return -1;' at the top of both SEQ_MIDI_IN_TransposerNoteGet and SEQ_MIDI_IN_ArpNoteGet to match BusPCSetGet/BusLowestNoteGet.
- **#26 [P3]** CMD_PAGE_SET stores an unvalidated page id into the global ui_page, leaving the UI on a non-existent page with all NULL callbacks  
  ↳ _`seq_testctrl.c` — cmd_page_set :526_ — In cmd_page_set, reject page >= SEQ_UI_PAGES with status 0x02 (or 0x03) before calling SEQ_UI_PageSet/CallInit; alternatively add a range clamp/guard at the top of SEQ_UI_PageSet so no caller can install a wild ui_page.
- **#29 [P3]** RandomGenerator leaves ui_selected_instrument set to a drum index and uses a stale instrument for parameter-layer randomization  
  ↳ _`seq_ui_trkrnd.c` — RandomGenerator :518_ — Pass the target instrument as a local; for par layers use instrument 0 (or the intended drum) explicitly and don't clobber the global ui_selected_instrument, or save/restore it around the loop.
- **#32 [P3]** Groove template num_steps > 16 from MBSEQ_G.V4 causes OOB read of add_step_* arrays on the emission path  
  ↳ _`seq_groove.c` — SEQ_GROOVE_DelayGet / SEQ_GROOVE_Event :269_ — In SEQ_FILE_G_Read clamp `num_steps` to 1..16 before storing; and/or bound the index in SEQ_GROOVE_* (`if(step >= 16) step %= 16;`).
- **#33 [P3]** Off-by-one track guard (`>` instead of `>=`) admits track==SEQ_CORE_NUM_TRACKS  
  ↳ _`seq_file_t.c` — SEQ_FILE_T_Read :94_ — Change to `if( track >= SEQ_CORE_NUM_TRACKS )`.
- **#34 [P3]** Unchecked SEQ_PAR_TrackInit/SEQ_TRG_TrackInit return leaves stale geometry on oversized file layer config  
  ↳ _`seq_file_b.c` — SEQ_FILE_B_PatternRead :762_ — Check the TrackInit return; on failure abort the record with SEQ_FILE_B_ERR_FORMAT (or fall back to a safe default geometry) rather than continuing to stream layer bytes.
- **#36 [P3]** RX FIFO overflow silently drops MIDI bytes, corrupting the in-flight parser event  
  ↳ _`mios32_uart.c` — USART2_IRQHandler / MIOS32_UART_RxBufferPut :775_ — At minimum count/flag RX overruns so it is observable; consider raising MIOS32_UART_RX_BUFFER_SIZE for MIDI-in ports, or draining RX in the ISR/higher-prio context. Also verify USART ORE (overrun) flag is being cleared — the ISR only tests RXNE (bit5) and TXE (bit7).
- **#37 [P3]** Cross-priority race on rs_expire_ctr/rs_last between the +3 periodic task and +4 emission task; the 'atomic not required' comment is inaccurate  
  ↳ _`mios32_uart_midi.c` — MIOS32_UART_MIDI_Periodic_mS / MIOS32_UART_MIDI_PackageSend_NonBlocking :243_ — Guard the rs_expire_ctr read/reset and rs_last write in PackageSend_NonBlocking with MIOS32_IRQ_Disable/Enable (as RS_Reset already does), or accept it and fix the misleading comment. Low musical impact; flag mainly because the comment could mask a future widening of these fields.
- **#39 [P3]** USB-host MIDI enumeration reads Ep_Desc[i][1] unconditionally without checking bNumEndpoints, opening a channel from stale endpoint data if the interface has only one endpoint  
  ↳ _`mios32_usb_midi.c` — USBH_InterfaceInit :465_ — Only process Ep_Desc[i][1] when bNumEndpoints >= 2, and require that exactly one IN and one OUT endpoint were found before calling ChangeConnectionState(1).
- **#40 [P3]** DeviceNotSupported() is invoked when the MIDI interface IS available (inverted condition) on the USB-host path  
  ↳ _`mios32_usb_midi.c` — USBH_InterfaceInit :496_ — Confirm intent against upstream MIOS32; if reporting unsupported devices, negate the condition to `if( !MIOS32_USB_MIDI_CheckAvailable(0) )`.
- **#41 [P3]** CIDRead start-token timeout falls through into a spurious 16-byte DMA read on a disconnected card  
  ↳ _`mios32_sdcard.c` — MIOS32_SDCARD_CIDRead :648_ — Add `goto error;` inside the `if( i == 65536 )` block at line 648-649 to match CSDRead.
- **#42 [P3]** spi_callback[] is written in task context and read in the DMA ISR without volatile/barrier  
  ↳ _`mios32_spi.c` — spi_callback :147_ — Declare `static void (* volatile spi_callback[3])(void);` (the surrounding volatile MMIO writes already order it in practice, but volatile documents and guarantees the ISR-visible read).
- **#43 [P3]** CheckAvailable re-deactivates CS and gives an already-given mutex on the not-was_available success path  
  ↳ _`mios32_sdcard.c` — MIOS32_SDCARD_CheckAvailable :345_ — Give the mutex exactly once per take; restructure so the not_available epilogue is only reached while the mutex is still held, or skip the second GIVE on the branch that already released at line 339.
- **#47 [P3]** I2C2 (port 0) error IRQ install guarded by wrong macro (MIOS32_IIC1_ENABLED) - copy/paste typo  
  ↳ _`mios32_iic.c` — MIOS32_IIC_Init :245_ — Change the guard on the I2C2_ER_IRQn install to '#if defined(MIOS32_IIC0_ENABLED) && MIOS32_IIC0_ENABLED > 0'.
- **#48 [P3]** Shadowed 'error' in scan retry loop makes retry condition/availability logic ineffective  
  ↳ _`mios32_iic_midi.c` — MIOS32_IIC_MIDI_ScanInterfaces :211_ — Drop the inner 's32 ' so the assignment updates the outer error; then the loop early-exits on first success.
- **#49 [P2]** FatFS shim tests only `< 0`, so a positive SD R1 error code is accepted as success (stale read / silently dropped write)  
  ↳ _`diskio.c` — disk_read / disk_write :98_ — In disk_read/disk_write treat any nonzero SectorRead/SectorWrite return as an error (`if (rc) return RES_ERROR;`), or have the driver collapse all R1-error returns to a negative code before returning.
- **#50 [P3]** Missing `goto error` on start-token timeout: DMA-reads and parses garbage into the CID struct  
  ↳ _`mios32_sdcard.c` — MIOS32_SDCARD_CIDRead :648_ — Add `goto error;` after `status = -257;` in the CIDRead start-token timeout branch, matching CSDRead.
- **#52 [P3]** Target step for atomic chord record is read from engine-owned t->step / timestamp_next_step_ref outside the mutex  
  ↳ _`seq_ui_inssel.c` — SEQ_UI_INSSEL_RecordChord :191_ — Move the step computation (191-208) inside the MUTEX_MIDIOUT critical section (take the mutex at the top of the function) so t->step/timestamp_next_step_ref are sampled and acted on atomically w.r.t. the emission task; the code comment already acknowledges the stock path races the step, and this keeps the fork's atomic replacement actually atomic.
- **#56 [P3]** Note >127 in SEQ_LIVE_PlayEvent overruns seq_live_played_notes[4] and live_keyboard_*[128]  
  ↳ _`seq_live.c` — SEQ_LIVE_PlayEvent :221_ — Add `if( p.note > 127 ) return -1;` (or clamp) at the top of the NoteOn branch before computing note_ix32, so the played-notes bitmap and live_keyboard_* indexing are always in range regardless of caller.
- **#58 [P3]** OnOff note with len>0xffff can drop its Off when pool is near-full, leaving a stuck note  
  ↳ _`seq_midi_out.c` — SEQ_MIDI_OUT_Send :426_ — In the len>0xffff branch, schedule the tail as SEQ_MIDI_OUT_OffEvent (which is exempt from the near-full failsafe), mirroring the drain-time OnOff->Off reschedule at line 691, so the Off cannot be refused after the On was accepted.
- **#59 [P3]** echo_repeats bit-7 escapes the disable mask -> 128-191 echo events per note flood the MIDI-out pool  
  ↳ _`seq_core.c` — SEQ_CORE_Echo :6249_ — Clamp the loop count to the count field only: `u8 n = echo_repeats & 0x3f; for(i=0;i<n;++i)`, or mask echo_repeats to 0x7f on entry / in SEQ_CC_Set.
- **#60 [P3]** Quantized phrase-recall rephase and synch-to-measure both bump t->bar in the same measure tick -> nth-trigger phase skips a bar  
  ↳ _`seq_core.c` — SEQ_CORE_Tick :4600_ — Guard the second bump: skip the synch-to-measure `++t->bar` (and/or ResetTrkPos) for tracks already reset in block A this tick (e.g. remember which tracks block A handled, or move the bar increment to a single measure-boundary site).
- **#61 [P3]** Roll2 flam trigger spacing/gate ignore clock divider (inner gatelength shadows the outer, never scaled by step_length)  
  ↳ _`seq_core.c` — SEQ_CORE_Tick :5526_ — Remove the inner shadow and scale like the roll1 path: compute a single gatelength and, for the divider, multiply by t->step_length/96 (e.g. `gatelength = ((4-(roll2_mode>>5))*((roll2_mode&0x1f)+1) * t->step_length)/96;`).
- **#64 [P3]** len>0xffff OnOff split schedules the Off using the already-delayed timestamp, so port delay is applied twice to the Off  
  ↳ _`seq_midi_out.c` — SEQ_MIDI_OUT_Send :427_ — Compute the Off timestamp from the pre-delay input, or subtract 'delay' back out before the recursive call, so delay is applied exactly once.
- **#66 [P3]** Foreign-clkdiv SYNCH_TO_MEASURE track loses the last steps of the captured bar when tps does not divide gspm*96  
  ↳ _`seq_core.c` — SEQ_CORE_CaptureDstLoopSteps / CaptureSpanTape :568_ — Either round dst_spm up (ceil) and cap the drive/window to dst_steps*tps consistently, or refuse the capture (return a negative status) for synch tracks whose tps does not divide gspm*96, matching how other unrepresentable configurations are refused.
- **#67 [P3]** HOLD-mode arp num_notes counts to 0 when MIDI note 0 (C-2) is held → modulo-by-zero  
  ↳ _`seq_midi_in.c` — SEQ_MIDI_IN_ArpNoteGet :1264_ — Guard the divisor ('if(!num_notes) return 0x80;' before the modulo) and/or track hold-slot occupancy with a real length field instead of a note==0 sentinel so MIDI note 0 is representable.
- **#69 [P3]** Remote refresh detection compares REMOTE_CMD to REMOTE_CMD_COMPLETE and &&'s a nonzero constant, so the 'is REFRESH' gate is wrong  
  ↳ _`seq_midi_sysex.c` — SEQ_MIDI_SYSEX_Cmd_Remote :453_ — Replace with 'if( sysex_state.remote_lcd.REMOTE_CMD_VALID && sysex_state.remote_lcd.REMOTE_CMD == SYSEX_REMOTE_CMD_REFRESH )'.
- **#71 [P3]** A new F0 arriving mid-payload is stored as a data byte instead of resyncing the parser, swallowing the next command  
  ↳ _`seq_testctrl.c` — SEQ_TESTCTRL_Parser :2721_ — In STATE_PAYLOAD, treat an incoming 0xf0 as an abort-and-restart: reset parser_state/header_ctr and re-enter STATE_HEADER (header_ctr=1) instead of buffering it.

## Detailed findings

### P1 — 8

#### #53 · Synched pattern change does blocking multi-sector SD read inside portENTER_CRITICAL + MUTEX_MIDIOUT on the +4 emission task, stalling the MIDI drain for tens of ms

**P1** · class `rt-timing` · cluster LENS rt-timing · confidence high

- **Location:** `seq_pattern.c` — SEQ_PATTERN_Handler :1440
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SYNCHED_PATTERN_CHANGE is enabled and a pattern change is requested while playing. On the switch-grid boundary, SEQ_CORE_Handler (running in SEQ_TASK_MIDI at prio +4, already holding MUTEX_MIDIOUT via app.c:845) calls SEQ_PATTERN_Handler at seq_core.c:4415. SEQ_PATTERN_Handler does MUTEX_SDCARD_TAKE then portENTER_CRITICAL() (line 1440), then SEQ_PATTERN_Load -> SEQ_FILE_B_PatternRead, which reads several KB (many 512-byte sectors) via MIOS32_SDCARD_SectorRead. Each sector busy-polls SPI (up to a 65536-iteration start-token spin, mios32_sdcard.c:475) with NO yield hook on the read path. This runs with interrupts masked at BASEPRI and the MIDIOUT mutex held. The emission task is the code that is blocked, so SEQ_MIDI_OUT_Handler cannot drain the queue for the whole ~tens-of-ms read; the poll-yield hook (vTaskDelay) is inert here because it is illegal inside portENTER_CRITICAL. The BPM timer ISR (MIOS32_IRQ_PRIO_HIGHEST=4, below the 0xa0 BASEPRI mask) keeps advancing bpm_tick, so on return the drain replays a burst of now-overdue notes/clocks in one 1ms slot -> audible late-note cluster / timing glitch on every synched switch while playing.
- **Root cause:** Blocking SD I/O is performed on the highest-priority audio task, inside a critical section that both masks interrupts and defeats the SD poll-yield hook, and under the MIDIOUT mutex that gates the note drain.
- **Suggested fix:** Do not call SEQ_PATTERN_Handler (which performs SD I/O) from SEQ_CORE_Handler's tick loop. Service the switch from the +2 task (as SEQ_TASK_Period1mS already does), or restructure the load so the SD read happens with interrupts ON and outside MUTEX_MIDIOUT, keeping only the RAM swap in the critical section.

#### #6 · Unguarded SEQ_CORE_RenderTrack in the capture/bounce primitive races the emission task's double-buffer flip -> torn output mirror = wrong live pitch/gate

**P1** · class `concurrency` · cluster render stack · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_CaptureTrackOutput / SEQ_CORE_ProcessorBounce :1922
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** Transport is PLAYING. User opens the pitch-gen page and presses GP8 (BOUNCE) on a track that has an enabled processor (seq_ui_trkpitchgen.c:263, no SEQ_BPM_IsRunning() guard). This runs in the UI task at prio tskIDLE+2 and enters SEQ_CORE_ProcessorBounce -> SEQ_CORE_CaptureTrackOutput, which UNCONDITIONALLY sets seq_render_dirty[track]=1 and calls SEQ_CORE_RenderTrack(track) at line 1922 (unlike SEQ_CORE_RenderTouched/RenderDirtySet at 934/963/972, which gate the direct render behind !SEQ_BPM_IsRunning()). RenderTrack's quiet path memcpys source into SEQ_PAR_OutputInactive(track), runs the processor stack over it, then does seq_render_active_buf[track]^=1. Meanwhile the +4 emission task runs SEQ_CORE_Tick -> SEQ_CORE_RenderTracks -> SEQ_CORE_RenderTrack(track) (dirty is set, or a live-sig change fires) for the SAME track and writes the SAME inactive half + flips the SAME byte. The two renders are not mutually excluded: (a) both memcpy/process the same inactive half concurrently (interleaved writes), and (b) the emission task can flip seq_render_active_buf to point at the half the UI task is still mid-filling, so the very next per-step SEQ_PAR_Get/SEQ_TRG_Get (seq_par.c:293, seq_trg.c:184) in the tick reads a half-rendered buffer -> torn par byte -> wrong emitted note pitch / wrong gate length on a live note, or a double-flip that republishes stale/clobbered data. The single-byte XOR is atomic but the render+flip sequence is not, and there is no mutex/critical section around it (design comment at line 1909-1911 explicitly assumes single-writer: 'needs no mutex ... the tick never sees a half-rendered buffer' — false once bounce runs while playing).
- **Root cause:** The double-buffer coherency invariant assumes exactly one caller of SEQ_CORE_RenderTrack while the transport runs (the +4 emission-task RenderTracks). SEQ_CORE_RenderTouched/RenderDirtySet honor this by gating their direct RenderTrack call behind !SEQ_BPM_IsRunning(), but SEQ_CORE_CaptureTrackOutput (line 1920-1922) and SEQ_CORE_ProcessorBounce (line 1973) call RenderTrack directly with no such gate and no mutex, and ProcessorBounce is reachable from a UI button while playing.
- **Suggested fix:** Either gate the direct render in CaptureTrackOutput behind !SEQ_BPM_IsRunning() and rely on the tick to render when playing, or wrap the render+flip in a short critical section / suspend the emission task for the duration. At minimum, ProcessorBounce/GP8 should defer to the tick when SEQ_BPM_IsRunning() rather than force a synchronous UI-task render of a live track.

#### #27 · BLM keyboard mode indexes 16-element note arrays with un-clamped button_column (0..63) -> OOB read/write from external MIDI

**P1** (finder proposed P0) · class `memory-safety` · cluster UI core+BLM · confidence high

- **Location:** `seq_blm.c` — SEQ_BLM_BUTTON_GP_KeyboardMode :925
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** An external BLM_SCALAR (or any device/spoofed MIDI on the BLM SysEx port) sends a GRID note event with note value in 0x10..0x3f while blm_mode==BLM_MODE_KEYBOARD. In blm_scalar_master.c:509 any note<0x40 is forwarded verbatim as button_x (0..63) to the callback. SEQ_BLM_ButtonCallback (seq_blm.c:1448) passes button_x through as button_column with NO clamp to num_columns. SEQ_BLM_BUTTON_GP_KeyboardMode then does blm_keyboard_velocity[button_column], and on press writes blm_keyboard_port/chn/note/velocity[button_column] (lines 1010-1013) and reads at 986-996/1022-1024. Those four static arrays are sized BLM_SCALAR_MASTER_NUM_ROWS = 16 (declared seq_blm.c:105-108). button_column of 16..63 writes/reads up to 47 bytes past each array into adjacent statics (blm_shift_active, blm_mode, etc.), corrupting driver state and emitting notes with garbage note/port -> stuck/wrong live notes and possible fault.
- **Root cause:** button_x from the wire (0..63 for a '16x16' grid, i.e. 16 cols x 4 step-views) is never clamped to num_columns (<=16) before being used as the column index into the 16-wide blm_keyboard_* arrays; the LED update loop (seq_blm.c:788,821) correctly iterates only BLM_SCALAR_MASTER_NUM_COLUMNS=16, exposing the intent that only 16 columns are valid.
- **Suggested fix:** In SEQ_BLM_ButtonCallback GRID case, reject or fold button_x >= num_columns before dispatch (or bound button_column < 16 at the top of SEQ_BLM_BUTTON_GP_KeyboardMode, matching the num_instruments guard used in GridMode).

#### #63 · Positive per-port MClk delay wraps the 0xffffffff sentinel Note-Off timestamp → sustained/stretched notes cut off instantly

**P1** · class `logic` · cluster MIDI OUT sched · confidence high

- **Location:** `seq_midi_out.c` — SEQ_MIDI_OUT_Send :331
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** User sets a positive 'MClk Delay' on an output port (e.g. +2 ms). SEQ_MIDI_PORT_ClkDelayUpdate() calls SEQ_MIDI_OUT_DelaySet(port, +ppqn) with ppqn in 1..127 (seq_midi_port.c:673-675). The core queues a sustained/stretched Note-Off as a parked sentinel via SEQ_MIDI_OUT_Send(port, pkg, SEQ_MIDI_OUT_OffEvent, 0xffffffff, 0) (seq_core.c:4032/4056/4096/4113/4138). In SEQ_MIDI_OUT_Send the SUPPORT_DELAY block runs for every event type gated only on port: with delay>0 it takes the else branch 'timestamp += delay', so 0xffffffff + delay wraps to (delay-1) — a tiny tick value (e.g. 1). The item is then inserted near the front of the sorted queue. On the very next SEQ_MIDI_OUT_Handler pass, item->timestamp (≈1) <= callback_bpm_tick_get() (a large running tick) is immediately true, so the sentinel Note-Off fires at once instead of staying parked until SEQ_MIDI_OUT_ReSchedule moves it. Result: on any port carrying notes that also has a positive MClk delay configured, every sustained/glide/stretched note is silenced immediately after its Note-On.
- **Root cause:** The ppqn delay is added unconditionally (timestamp += delay) with no overflow guard, but the 0xffffffff value is used as a 'park forever' sentinel by the Off/OnOff sustain mechanism. A positive delay pushes the sentinel past u32 max and wraps it into the near-future window the Handler drains, defeating the sentinel. The negative-delay branch is guarded (clamps to 0) but the positive branch is not, and no branch special-cases the 0xffffffff sentinel.
- **Suggested fix:** Skip the delay adjustment for the reserved sentinel timestamp (e.g. if timestamp==0xffffffff, do not add delay), or saturate: if (timestamp > 0xffffffff - delay) timestamp = 0xffffffff. Mirror the existing negative-side clamp on the positive side.

#### #28 · Pattern-name copies use the name as a printf format string (sprintf(dst, src)) — %-specifiers in a user-set name deref wild stack args

**P1** (finder proposed P0) · class `memory-safety` · cluster UI pages · confidence high

- **Location:** `seq_ui_pattern_remix.c` — Encoder_Handler/Button_Handler (sprintf name copies) :459
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** On the Pattern-Remix name-edit page the keypad's GP11 encoder path (seq_ui.c:5509 SEQ_UI_Var8_Inc(...,32,127,...)) lets the user set ANY ASCII byte 32-127 into seq_pattern_name[0], including '%' (37). Name a pattern e.g. "%s%s%n". Then any of the copies sprintf(seq_pattern_name[group], seq_pattern_name[0]) (line 216), sprintf(pattern_name, seq_pattern_name[0]) (220/351), sprintf(pattern_name_copypaste, seq_pattern_name[0]) (438/440), sprintf(seq_pattern_name[0], pattern_name_copypaste) (459) passes the tainted string as the *format* argument to sprintf. '%s' dereferences an arbitrary stack word as a char* (wild read -> crash / garbage into a 21-byte buffer), '%n' writes through a stack word (memory corruption); even benign '%d' produces a long expansion that overflows the fixed [21] destination. This corrupts pattern-name state / faults during a live Copy/Paste/rename.
- **Root cause:** sprintf(dst, src) is used to copy strings where src is runtime/user/SD-sourced data that can legally contain printf conversion specifiers; the copy should be strncpy/memcpy or sprintf(dst, "%s", src).
- **Suggested fix:** Replace every sprintf(dst, src) name copy with strncpy(dst, src, 20); dst[20]=0; (or sprintf(dst, "%.20s", src)). Same pattern also appears in seq_ui_save.c-style copies — audit all sprintf-with-nonliteral-format.

#### #30 · Track-preset 'Par'/'Trg' address offset causes a wild write into RAM past the layer row

**P1** (finder proposed P0) · class `memory-safety` · cluster persistence · confidence high

- **Location:** `seq_file_t.c` — SEQ_FILE_T_Read :190
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** User loads a track preset from /PRESETS/*.V4T (seq_ui_pattern.c:499 with import_flags.ALL=0xff, or seq_ui_trkevnt.c:1683). The file contains a line like `Par 0x3F8 <16 values>` (addr_offset=1016) or `Par 0x400 <16 values>` (addr_offset=1024). The bounds guard at lines 165-170 only checks `addr_offset >= SEQ_PAR_MAX_BYTES` (1024) and — critically — only prints a DEBUG_MSG with no break/continue, so control falls through. Lines 187-194 then execute `seq_par_layer_value[track][addr_offset + i] = values[i]` for i=0..15 regardless: `Par 0x3F8` writes indices 1016..1031 (8 bytes past the 1024-byte AHB row, corrupting the next track's par data), and `Par 0x400` writes 1024..1039 (a fully out-of-row wild write). Same for `Trg` into seq_trg_layer_value.
- **Root cause:** The guard (a) validates only the base offset, not base+16, and (b) has no control-flow escape — it logs and then the write block runs unconditionally. A crafted or corrupt track-preset file loaded via the UI drives the index.
- **Suggested fix:** Change the guard to `if( (par_layer && addr_offset+16 > SEQ_PAR_MAX_BYTES) || (!par_layer && addr_offset+16 > SEQ_TRG_MAX_BYTES) ) { DEBUG_MSG(...); continue; }` so the out-of-range line is skipped entirely and the last 16-byte group can't straddle the array end.

#### #31 · Corrupt song bank with song_size < 20 underflows num_entries and overruns seq_song_steps[128]

**P1** · class `memory-safety` · cluster persistence · confidence high

- **Location:** `seq_file_s.c` — SEQ_FILE_S_SongRead :369
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A foreign/corrupt/truncated MBSEQ_S.V4 whose header field song_size is < sizeof(seq_file_s_song_header_t) (20) — e.g. 0 — passes SEQ_FILE_S_Open (which reads song_size at seq_file_s.c:296 with NO lower-bound check and sets info->valid=1). On song select/session load, SongRead clamps only the upper bound (line 363), then computes `num_entries = (song_size - sizeof(seq_file_s_song_header_t)) / sizeof(seq_song_step_t)`. With song_size=0 the u16 promotes to unsigned and 0-20 wraps to ~4.29e9, /8 ≈ 5.36e8 entries. The loop at 370-373 writes s->ALL_L/ALL_H sequentially from &seq_song_steps[0], overrunning the 128-entry array by hundreds of megabytes -> guaranteed corruption / hard fault.
- **Root cause:** song_size is read from the file and used to derive the loop count without validating song_size >= sizeof(song_header); the subtraction underflows in unsigned arithmetic.
- **Suggested fix:** In Open, reject song_size < sizeof(seq_file_s_song_header_t); in SongRead, compute num_entries only when song_size >= sizeof(header) and additionally clamp num_entries to SEQ_SONG_NUM_STEPS before the loop.

#### #35 · "NonBlocking" UART send busy-waits inside the +4 emission task when the 64-byte DIN TX FIFO fills

**P1** · class `rt-timing` · cluster UART MIDI · confidence high

- **Location:** `mios32_uart_midi.c` — MIOS32_UART_MIDI_PackageSend_NonBlocking :306
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** During dense DIN MIDI output (e.g. several tracks routed to a UART port, or a chord + CC burst landing on the same tick), the 64-byte tx_buffer for that UART fills. SEQ_MIDI_OUT_Handler (running in TASK_MIDI at tskIDLE+4) calls callback_midi_send_package -> MIOS32_MIDI_SendPackage[_NonBlocking] -> MIOS32_UART_MIDI_PackageSend_NonBlocking, which at line 306 calls the BLOCKING MIOS32_UART_TxBufferPutMore (mios32_uart.c:716), whose while((error=..._NonBlocking(...))==-2); spins until the RXNE/TXE ISR drains bytes. At 31250 baud each byte drains in ~320us, so freeing room for a 3-byte package can busy-wait ~1ms with interrupts enabled. The +4 emission task thus stalls, and the next 1ms SEQ_CORE_Tick is delayed/missed -> audible timing glitch / late notes exactly when the queue is busiest.
- **Root cause:** The function documents itself as non-blocking (header lines 256-257 '-2: caller should retry', and the switch at 306-309 has a 'case -2: return -2' path that is meant to bubble a retry request to the caller) but line 306 invokes the BLOCKING wrapper MIOS32_UART_TxBufferPutMore instead of MIOS32_UART_TxBufferPutMore_NonBlocking (mios32_uart.c:665). The -2 return path is therefore dead code and the non-blocking contract is violated. SEQ_MIDI_OUT_Handler (modules/sequencer/seq_midi_out.c:649) calls the send fire-and-forget with no retry/backpressure handling, so it cannot yield around a full FIFO.
- **Suggested fix:** Call MIOS32_UART_TxBufferPutMore_NonBlocking at line 306 so -2 propagates; have the emission drain treat -2 as 'try again next tick' (leave the item on midi_queue) instead of blocking. Note this busy-wait characteristic is inherited from MIOS32 mainline, so confirm the emission-side backpressure handling before changing the shared driver.

### P2 — 22

#### #46 · STM32F4 flash write only erases on exact sector-base match and never verifies -> silent corrupt program on non-base write

**P2** (finder proposed P1) · class `persistence` · cluster timers/sys/iic/bsl · confidence medium

- **Location:** `bsl_sysex.c` — BSL_SYSEX_WriteMem :574
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A host sends a Write-Mem whose sysex_addr is inside a sector but not exactly equal to a flash_sector_map[][0] base (e.g. 0x08004010, or any resumed/partial upload that does not restart on a sector boundary). The erase loop at line 573-604 finds no addr==base match, so NO FLASH_EraseSector runs, then FLASH_ProgramWord (line 615) writes into an un-erased region. NAND-flash programming can only clear bits, so the resulting words are (old AND new); FLASH_ProgramWord frequently still returns FLASH_COMPLETE, so no error is reported (line 627 verify is a TODO/no-op). The device ACKs success but the flashed firmware image is corrupt -> bricked boot until re-flashed.
- **Root cause:** Erase is keyed on an exact sector-base address equality instead of 'address falls within sector', and there is no read-back verify after programming. Relies entirely on a trusted host always starting writes at sector bases; comment at line 466 only claims 16-byte alignment, not sector alignment.
- **Suggested fix:** Erase the sector that CONTAINS addr (base<=addr<next_base) rather than requiring addr==base, and/or add the read-back verify the TODO promises before returning ACK.

#### #51 · Unguarded read-modify-write on seq_live_played_notes[]/live_keyboard_*[] widened to a +2/+3 cross-task race by the new play surface

**P2** · class `concurrency` · cluster LENS concurrency · confidence medium

- **Location:** `seq_live.c` — SEQ_LIVE_PlayEvent :226
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #21
- **Failure scenario:** seq_live_played_notes[note>>5] is read (226), then |= mask (241) or &= ~mask (239) WITHOUT holding MUTEX_MIDIOUT across the RMW (the mutex only brackets the MIDI send at 228-235). The fork's play surface now calls SEQ_LIVE_PlayEvent from SEQ_UI_INSSEL_Button_Handler (seq_ui_inssel.c:115,156) which runs in the UI task at tskIDLE+2 (PRIORITY_TASK_PERIOD1MS), while MIDI-in notes reach the same function via APP_MIDI_NotifyPackage -> SEQ_MIDI_IN_Receive -> SEQ_LIVE_PlayEvent in TASK_MIDI_Hooks at tskIDLE+3 (programming_models/traditional/main.c:41,257). Concrete break: while the +2 GP-button handler is between reading and writing seq_live_played_notes[1] for note 40, an incoming external note 45 (same word, bit 45%32) preempts at +3 and does its own |= ; the +2 task then writes back its stale word, dropping bit 45 -> that MIDI-in note's off is never sent (its active bit was clobbered) -> stuck/hung note. Same tear applies to live_keyboard_note[p.note] when a keyboard press and a MIDI-in note share a pitch, sending the wrong stored output note on release.
- **Root cause:** seq_live_played_notes / live_keyboard_* are non-atomic multi-slot state RMW'd without a lock; mainline reached PlayEvent from +3 (MIDI-in) and +2 (JAM page) already, and the fork's INSSEL play surface adds another +2 entry, keeping the race live on a hot performance path.
- **Suggested fix:** Wrap the played-notes bitmap RMW + keyboard-array update (seq_live.c:226-241 and the effective_note store block) in MUTEX_MIDIOUT (recursive, so nesting the existing send-mutex is safe) or a short portENTER_CRITICAL around the bitmap word update.

#### #9 · Drum-mode event loop overruns layer_events[16] when a track reports >16 instruments

**P2** (finder proposed P0) · class `memory-safety` · cluster layer/transforms · confidence high

- **Location:** `seq_layer.c` — SEQ_LAYER_GetEvents (drum-mode note loop) :422
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** Load a pattern (SD MBSEQ_T.V4 via SEQ_FILE_T, or a crafted/corrupt file) with EventMode=Drum (3), ParInstruments=64, ParLayers=1, ParSteps=16. seq_file_t.c only rejects par_instruments<1 (line 213) and SEQ_PAR_TrackInit only rejects instruments*layers*steps > SEQ_PAR_MAX_BYTES (1024); 64*1*16==1024 passes, so par_layer_num_instruments[track]=64. On the next step, SEQ_CORE_Tick (1ms emission task, prio tskIDLE+4) declares seq_layer_evnt_t layer_events[16] on its stack (seq_core.c:5089) and calls SEQ_LAYER_GetEvents. The drum path loops for(drum=0; drum<num_instruments(=64); ++drum) writing &layer_events[num_events] at line 422 and ++num_events at 434 with NO num_events>=16 bound check (unlike the melodic path at line 980 and the chord sub-loop at 777). Every drum with note&&velocity (or unconditionally when insert_empty_notes=1, e.g. the UI PARSEL/TRGSEL VU path via SEQ_LAYER_GetEvntOfLayer at line 329) writes past index 15, smashing the emission-task stack -> live crash / HardFault / silent RAM corruption during performance.
- **Root cause:** The non-MBSEQV4P drum note-emission loop has no capacity guard on num_events against the fixed 16-entry layer_events array, and neither the SD pattern loader (seq_file_t.c:199/213) nor SEQ_PAR/TRG_TrackInit clamp instrument count to <=16.
- **Suggested fix:** Clamp num_instruments to the array size in the drum loop (add `if(num_events >= 16) break;` after each ++num_events, matching the melodic guard), AND bound ParInstruments/TrgInstruments to 16 at load in seq_file_t.c before SEQ_*_TrackInit.

#### #21 · Drum-pad preview note is an unmasked u8 from lay_const -> OOB write in SEQ_LIVE_PlayEvent

**P2** · class `memory-safety` · cluster play/record · confidence high

- **Location:** `seq_ui_inssel.c` — SEQ_UI_INSSEL_DrumTrigger :109
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Also independently found as:** #56, #51
- **Failure scenario:** A pattern loaded from SD (seq_file_b.c:759 SEQ_CC_Set(track,cc,cc_buffer[cc]) writes raw file bytes unmasked; SEQ_CC_Set stores lay_const[cc]=value with no 7-bit mask, seq_cc.c:322) or a SysEx CC-set puts a drum-instrument note byte >127 into tcc->lay_const[0..15]. On a Drum track with INSSEL_DRUM_TRIGGER enabled, tapping that pad while REC is NOT armed sets p.note = tcc->lay_const[drum] (128..255) and calls SEQ_LIVE_PlayEvent. There, note_ix32 = p.note/32 = 4..7 and seq_live_played_notes[note_ix32] |= mask writes 1-3 words PAST the 4-word seq_live_played_notes[4] array (seq_live.c:241/226/239), and live_keyboard_port/chn/note[p.note] index past their 128-entry arrays (seq_live.c:230,303-305). This corrupts adjacent .bss/.data globals in the live-play/emission context.
- **Root cause:** The new preview path sources the note from a full u8 config field (lay_const) instead of a 7-bit MIDI byte, and SEQ_LIVE_PlayEvent (unlike SEQ_RECORD_Receive at seq_record.c:391 which does note &= 0x7f) never masks p.note before using it to index the 4-word played-notes and 128-entry keyboard-tracking arrays. Stock MIDI-in cannot reach this because MIDI note bytes are inherently <=127; the drum-trigger surface is the new exposure.
- **Suggested fix:** Mask the note to 7 bits before dispatch: p.note = tcc->lay_const[0*16 + drum] & 0x7f; (and/or bound-check note_ix32 in SEQ_LIVE_PlayEvent). The record branch is already safe via SEQ_RECORD_Receive's &=0x7f.

#### #62 · Stopped-edit synchronous render takes the SWEEP path, refreshing only a 4-step window instead of the full buffer — stale output mirror for GRIP/GRAVITY/transpose/limit edits while transport is stopped

**P2** · class `logic` · cluster render stack · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_RenderTouched / SEQ_CORE_RenderTrack :934
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** Transport is STOPPED. User turns the GRAVITY encoder (SEQ_CORE_TensionGravitySet -> SEQ_CORE_RenderTouched, line 1810) or writes GRIP / ChordMask-strength / transpose / limit via a page/CC (all SlotSync fns end in SEQ_CORE_RenderTouched: lines 1664,1671,1694,1701,1736,1743,1770,1777). RenderTouched (line 930-934) sets seq_render_touched_ms[track]=now, dirty=1, and because BPM is not running calls SEQ_CORE_RenderTrack synchronously. RenderTrack (line 1585) calls SEQ_CORE_RenderSweeping(track), which returns 1 (delay = now-now = 0 < SEQ_RENDER_SWEEP_MS=50), so it dispatches sweep_window_render (line 1588) and returns WITHOUT clearing dirty and WITHOUT a full render. sweep_window_render (line 1486) only recopies+reprocesses steps [seq_core_trk[track].step % num_p_steps, +SEQ_RENDER_SWEEP_LOOKAHEAD=4) into the ACTIVE half. Every step outside that 4-step window keeps its PRE-EDIT value in the output mirror. SEQ_PAR_Get reads OutputActive (seq_par.c:293), so the LCD step view and any stopped audition of those steps show/play the OLD (ungripped / untransposed) pitches. Because there is no tick while stopped and each further edit re-touches (delay resets to ~0), the catch-up quiet render NEVER fires while stopped — the staleness persists and even leaks into playback: on PLAY, dirty is still 1 but if <50ms have elapsed the first RenderTracks pass sweeps again (window only), so the opening ~50ms / few steps of the pattern emit stale pitches until a full quiet render finally runs.
- **Root cause:** RenderSweeping is time-based only and returns true immediately after any touch, so the stopped-path synchronous render inherits the sweep (partial-window) regime that was designed for the live/playing knob-motion case. The comments at lines 958-961 (RenderDirtySet) and 1794-1796 (TensionGravitySet) explicitly promise a synchronous FULL render so stopped edits are 'immediately visible on the LCD' / 'the LCD/audition reflect it' — RenderTouched violates that contract by routing through sweep_window_render.
- **Suggested fix:** When !SEQ_BPM_IsRunning(), force the quiet full-render path (e.g. in RenderTouched clear seq_render_touched_ms before the synchronous SEQ_CORE_RenderTrack, or have RenderTrack ignore the sweep regime while stopped) so the whole [0,used) region is rebuilt and dirty is cleared. Playing behavior (sweep window follows the playhead, full catch-up after 50ms) is unaffected.

#### #65 · Metronome click notes are captured into the pattern when the recording track is track 16 (index 15)

**P2** (finder proposed P1) · class `logic` · cluster CAPTURE · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_CaptureTapeTap / metronome emit in SEQ_CORE_Tick :4741
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** With the metronome enabled (seq_core_state.METRONOME, seq_core_metronome_chn set) and the visible/recording track = track 16 (index 15), the metronome emits a NoteOn every beat with p.cable=15, p.type=p.event=NoteOn(0x9), p.note=seq_core_metronome_note_b/_m, velocity 96/127, via SEQ_MIDI_OUT_Send (seq_core.c:4740-4755). SEQ_MIDI_OUT_Handler drains it and calls the passive tap callback_midi_tap = SEQ_CORE_CaptureTapeTap (seq_midi_out.c:653). In the tap, seq_core_cap_ring_track == 15, so the guard `p.cable != seq_core_cap_ring_track` (seq_core.c:390) does NOT reject it, and `p.event != 0x9` (line 391) does NOT reject it (event==0x9). The metronome note-on is appended to seq_core_cap_tape[] like a real performed note, and its len=20 running-status note-off back-fills a ~20-tick gate. A while-PLAYING N-bar CAPTURE grab (SEQ_CORE_CaptureSpanTape) then materializes a phantom metronome note on every beat downbeat of the captured pattern (bpm_tick%96==0 && ref_step%4==0), corrupting the deliverable with audible click notes that were never part of the musical line. The re-sim (STOPPED) path is unaffected because the metronome block is inside `export_track == -1` (line 4703) and re-sim drives with export_track=src.
- **Root cause:** The metronome borrows track #16's cable tag (p.cable=15) as documented at line 4741, colliding with the CAPTURE tape's per-track cable filter which uses the same cable field to identify the recording track. The tap has no way to distinguish an engine metronome NoteOn on cable 15 from a genuine track-15 performance note.
- **Suggested fix:** Filter the metronome out of the tape: either tap the port/event-type as well (the metronome uses seq_core_metronome_port which differs from a track's assigned port, or tag it distinctly), or in SEQ_CORE_CaptureTapeTap reject packages that don't originate from the track emission path (e.g. add a dedicated non-track sentinel cable/tag for the metronome instead of reusing 15, or gate the tap on a per-track emission flag rather than cable==15).

#### #13 · Captured note of an exact-integer-step duration (gate % tps == 0) is written as an unterminated Glide and over-holds into the following rests

**P2** · class `logic` · cluster CAPTURE · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_CaptureMaterializeNote :345
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** While playing, a live-tape grab captures a note whose measured gate is an exact multiple of tps and >= tps — e.g. a held keyboard/drum note that sounded for exactly one step (gate == tps) or two steps (gate == 2*tps). Then full = gate/tps >= 1 and rem = gate % tps == 0. Line 322-328 writes the start step at length par 95 (SEQ_PAR_LengthGet maps 95 -> e->len 96 = Glide, confirmed seq_par.c:365), lines 336-343 carry each additional full step at par 95 (also Glide), and because rem == 0 the terminating partial-tail block at line 344-353 is skipped entirely. Result: the LAST covered step is a Glide (e->len==96) with no sub-Gld terminator, and its following step is a rest. At playback SEQ_CORE_Tick treats e->len>=96 as gen_sustained_events (seq_core.c:5327) and defers the note-off; a rest emits no event, so the note keeps ringing across the rest(s) and only ends at the NEXT note-on (or loop restart). The captured note audibly sustains far longer than it did live — the exact over-hold the function's own comment ("a final partial step ... whose sub-step length terminates the sustain") says it exists to prevent.
- **Root cause:** The multi-step length-chain encoder only emits a terminating (<96) tail step when rem>0. When the note length is an exact integer number of steps (rem==0) the final covered step is left at Gld/95 with nothing after it to break the tie, so it glides instead of ending. The single-step exact case (full==1, rem==0) is not treated specially either — it falls through the full>=1 branch and never early-returns.
- **Suggested fix:** When rem==0 and full>=1, the note ends exactly on the step0+full boundary: the last carried step (step0+full-1) must NOT be Gld — write it at a sub-Gld length that terminates on the step boundary (e.g. par value <=94), or drop the final carried step to a normal max-non-tie length. Equivalently, treat gate that is an exact multiple as (full-1) carried Gld steps plus a final step whose length terminates at the step end.

#### #14 · Stopped re-sim capture collapses every multi-step note to a single Glide start step, losing duration and over-holding

**P2** · class `logic` · cluster CAPTURE · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_CapSpanSink :2260
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A STOPPED grab drives SEQ_CORE_CaptureSpanReSim, which materializes via SEQ_CORE_CapSpanSink (NOT SEQ_CORE_CaptureMaterializeNote). For a re-simulated note spanning >1 dst step (on at step0, off at on_tick + 2*tps), the note-off back-fill at line 2240-2242 computes gate=2*tps and calls SEQ_CORE_CaptureGateToParLen(2*tps, tps) which yields len clamped to 96 -> par value 95 (Glide) written onto step0 only. No carried/terminating steps are written (the sink writes exactly one step per note-on). At playback that Gld step (e->len==96, seq_core.c:5327 -> gen_sustained_events) ties forward until the next note-on instead of ending after 2 steps: the note over-holds and any intervening rest is swallowed. Unlike the tape path, the re-sim sink has no multi-step length-chain at all, so ANY captured note longer than one step (not just exact multiples) loses its true duration.
- **Root cause:** The re-sim quantizing sink writes a single length-layer value per step and clamps gate>tps to 95 (Gld). It was never given the multi-step tie-chain that SEQ_CORE_CaptureMaterializeNote applies on the tape path, so notes longer than one step degrade to an unterminated glide on their first step.
- **Suggested fix:** Route the re-sim sink's precise-gate write through the same multi-step chain as SEQ_CORE_CaptureMaterializeNote (write carried Gld steps + a terminating tail), rather than a single clamped length value on the on-step. The open-note off back-fill already knows on_tick/step and gate, so the chain can be materialized when the off drains.

#### #15 · Engage marks slot in_use/engaged before seed, loop[], anchor[] and mult[] are initialized — emission Tick can process a half-built slot

**P2** · class `concurrency` · cluster gen+RNG · confidence high

- **Location:** `seq_generator.c` — SEQ_GENERATOR_Engage :408
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** User presses ENGAGE (GP1, runs in the +2 Period1mS/button task). Engage sets g->engaged=1 (line 407) and g->in_use=1 (line 408), making the slot visible to the pool walk, but does NOT set g->seed until line 415, seed_loop() (loop[]) until 418, and memset(g->mult, 0x22) until 426. If the +4 emission task's SEQ_GENERATOR_Tick fires a measure-wrap on this track in that window, mutate_loop() runs on the slot while g->mult is still 0x00 (=MULT_MUTE for every step → mult_threshold returns 0 → whole loop frozen) and, if it preempts before line 415/418, g->seed is 0 (self-heals to the shared 0xdeadbabe default, not the intended minted stream) and g->loop is all zeros. write_loop_to_source then transcribes zero/garbage pitches into the source par-layer for that one measure. Result: an audible bar of muted or wrong-pitch notes at the exact tick a fresh ENGAGE lands on a measure boundary, plus a one-measure break of the 'deterministic/seekable per-slot stream' contract.
- **Root cause:** The commit at alloc_slot (lines 273-283) documents the invariant 'caller initializes the slot fields and THEN sets in_use ... keeping the slot in a sane state by the time anyone could read it', but Engage violates it: in_use=1 is set at line 408, ahead of the seed/loop/anchor/mult initialization (415-426) and the pool_index publish (428). in_use — not pool_index — is the field the emission-task pool walk (SEQ_GENERATOR_Tick line 701, ForceRewrite, Roll) tests, so the slot is reachable before it is fully built.
- **Suggested fix:** Set g->in_use = 1 (and g->engaged) LAST, after seed/seed_loop/anchor/mult are all populated (mirror the alloc_slot contract). Since pool walks key on in_use, deferring only that one store to after line 426 closes the window without needing a critical section.

#### #16 · UI-thread gestures mutate a pool slot with no lock while the +4 emission Tick can preempt and mutate the same slot

**P2** · class `concurrency` · cluster gen+RNG · confidence medium

- **Location:** `seq_generator.c` — mutate_loop / roll_loop / SEQ_GENERATOR_Snap :173
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A generator is engaged on the visible track. The user presses ROLL (GP1, +2 task → SEQ_GENERATOR_Roll → roll_loop) or ForceMutate/Snap. roll_loop iterates g->loop[0..63], each iteration reading/advancing g->seed and writing g->loop[i], then write_loop_to_source() streams the loop into the par buffer via SEQ_PAR_Set. Because the emission task is priority +4 and this runs at +2, a measure-wrap tick can preempt mid-loop and run mutate_loop()+write_loop_to_source() on the SAME slot g: it advances g->seed and overwrites g->loop[] entries the UI half-wrote, and both threads independently call SEQ_PAR_Set on the same (track,par_layer). The transcribed source ends up an interleaving of two passes — the seed stream is consumed out of order (breaking the deterministic/seekable per-slot contract the keystone promises) and the audible loop for that measure is a scrambled mix rather than either intended result.
- **Root cause:** The pool is shared mutable state between the +2 UI/button task and the +4 emission task, but none of the UI gesture entry points (Roll/Snap/ForceMutate/MultCycle/LockToggle/Engage) nor SEQ_GENERATOR_Tick take a mutex or critical section around the slot read-modify-write. The design's per-slot xorshift keystone assumes a single writer per slot; two tasks at different priorities both write g->seed and g->loop[].
- **Suggested fix:** Serialize slot mutation: either run the UI gestures under a short taskENTER_CRITICAL / the same mutex the emission Tick would honor, or bounce the gesture into the emission task. No memory-corruption (32-bit seed store is atomic and loop[] bytes stay valid pitches), so this is a correctness/feel defect, not a crash — hence P2.

#### #19 · Journal snapshot memcpys seq_par/trg_layer_value with no tick exclusion — torn read vs the +4 generator write

**P2** (finder proposed P1) · class `concurrency` · cluster save/undo · confidence high

- **Location:** `seq_core.c` — journal_snap :3443
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** Track T has 1–4 engaged, auto-mutating generators (JournalArm's guard only refuses >4, so 1–4 is allowed to arm). A deliberate gesture (pull/utility/ENGAGE/capture-to-track) calls SEQ_CORE_JournalArm(T) from the UI/hooks task (prio +2/+3). journal_snap runs `memcpy(u->par, seq_par_layer_value[T], 1024)` / `memcpy(u->trg, ...,256)` with NO portENTER_CRITICAL. During those memcpys the +4 emission task (SEQ_CORE_Tick -> SEQ_GENERATOR_Tick -> write_loop_to_source -> SEQ_PAR_Set at seq_par.c:31) preempts on a measure wrap and rewrites seq_par_layer_value[T][step_ix] for the SAME track. The snapshot captures a mix of pre- and post-mutate bytes — a par/trg buffer that never existed as a coherent pattern. A later SEQ_CORE_JournalUndo restores that Frankenstein buffer: wrong pitches/velocities/gates on the mutated steps. The identical exposure exists at line 3613 (journal_snap(&after) in JournalUndo) which snaps a LIVE generator track.
- **Root cause:** Asymmetric protection: journal_restore (line 3466) explicitly wraps its bulk memcpys in portENTER_CRITICAL precisely because 'a tick between... would render/emit torn state' (comment line 3464), but the snapshot side (journal_snap, and its callers JournalArm/JournalUndo/JournalRedo at 3569/3613/3648) omits the symmetric critical section even though the +4 generator write mutates the same seq_par_layer_value array asynchronously.
- **Suggested fix:** Wrap the par/trg (and generator) capture in journal_snap in portENTER_CRITICAL()/portEXIT_CRITICAL() — mirroring journal_restore. The bpm ISR is at MIOS32_IRQ_PRIO_HIGHEST(4) > configMAX_SYSCALL_INTERRUPT_PRIORITY(5) so the clock is unaffected; the critical section only excludes the +4 emission task's generator writes, which is exactly what is needed.

#### #20 · REDO silently drops generators past PERSIST_SLOTS(4) — the 'after' snapshot has no engaged-count guard

**P2** · class `logic` · cluster save/undo · confidence medium

- **Location:** `seq_core.c` — SEQ_CORE_JournalUndo :3613
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SEQ_CORE_JournalArm refuses to arm a track with >4 engaged generators (guard at line 3564, so REDO can't silently delete overflow). But when UNDO captures the post-gesture state — journal_snap(&action_journal.after, ...) at line 3613 — it calls SEQ_GENERATOR_TrackSnapshot with max=PERSIST_SLOTS(4) and NO TrackEngagedCount guard. If the armed gesture itself pushed the live generator count from <=4 up to >4 on a drum track (up to SEQ_GENERATOR_INSTRUMENTS=16 possible), TrackSnapshot copies only the first 4 (ascending by instrument) and after.gen_count caps at 4. A subsequent REDO (journal_restore -> SEQ_GENERATOR_TrackRestore) clears and re-adds only those 4 — the 5th+ generator engaged by the gesture is silently lost, and the redone track no longer mutates as it did live.
- **Root cause:** The overflow guard is applied only on the ARM path (before), not on the lazily-captured after/before snapshots taken inside JournalUndo/JournalRedo. TrackSnapshot truncates silently at max rather than signalling truncation.
- **Suggested fix:** Apply the same SEQ_GENERATOR_TrackEngagedCount > SEQ_GENERATOR_PERSIST_SLOTS check before the lazy after/before captures (lines 3613/3648); on overflow, either invalidate the redo arm or refuse the toggle rather than capture a truncated snapshot.

#### #45 · Rx callback fed with stale/uninitialized package before received bytes are copied in

**P2** (finder proposed P1) · class `logic` · cluster timers/sys/iic/bsl · confidence high

- **Location:** `mios32_iic_midi.c` — _MIOS32_IIC_MIDI_PackageReceive :457
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** An MBHP_IIC_MIDI module signals a byte (RI_N active), the 4-byte IIC read succeeds into local buffer[], and at line 457 MIOS32_MIDI_SendPackageToRxCallback(IIC0+iic_port, *package) is invoked BEFORE package->type/evnt0..2 are populated (that copy only happens at lines 463-466). The callback therefore receives whatever garbage/stale value the caller's package struct held. Concretely a Note-On arriving on an IIC MIDI IN port delivers wrong bytes to any registered Rx callback (MIDI monitor, filter, router) — wrong pitch/CC forwarded or a filter mis-fires; if the callback returns non-zero (filtered) at line 458 the function returns 0/-4 and the just-read real bytes are discarded, dropping the note entirely.
- **Root cause:** Order-of-operations bug: the Rx-callback dispatch was placed before the buffer[]->*package copy. Dormant in SEQ V4 (IIC MIDI not enabled in mios32_config.h) but a real platform defect on any build with MIOS32_IIC_MIDI_NUM>0.
- **Suggested fix:** Move the package->type/evnt0..2 assignment (lines 463-466) to immediately after the successful TransferWait and BEFORE the SendPackageToRxCallback call; pass the freshly-filled *package.

#### #38 · SingleUSB config-descriptor size uses the 4-port class-desc size -> 96 trailing zero bytes reported to host, breaking enumeration in ForceSingleUSB mode

**P2** · class `persistence` · cluster USB MIDI · confidence high

- **Location:** `mios32_usb.c` — MIOS32_USB_MIDI_SIZ_CONFIG_DESC_SINGLE_USB / MIOS32_USB_ConfigDescriptor_SingleUSB :94
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** User enables the single-USB Windows-compatibility fallback (MIOS32_SYS_ADDR_SINGLE_USB set, ForceSingleUSB()==1). MIOS32_USB_CLASS_GetCfgDesc (line 1562-1564) returns MIOS32_USB_ConfigDescriptor_SingleUSB and reports *length = sizeof(that array). With NUM_PORTS=4 (this fork, mios32_config.h:35) the array is declared [MIOS32_USB_SIZ_CONFIG_DESC_SINGLE_USB] = 197 bytes, and its own wTotalLength field (bytes 2-3, lines 657-658) is 197. But the actual initialized descriptor content ends at line 790 and is only 101 bytes; the remaining 96 bytes are static zero-fill. The host reads a 197-byte config descriptor whose trailing 96 bytes are 0x00 -> a descriptor with bLength=0 mid-parse, which stalls/aborts many host descriptor parsers -> device fails to enumerate in exactly the fallback mode the user turned on to recover a flaky Windows host.
- **Root cause:** Line 94 defines MIOS32_USB_MIDI_SIZ_CONFIG_DESC_SINGLE_USB using MIOS32_USB_MIDI_SIZ_CLASS_DESC (the full NUM_PORTS=4 class-descriptor length, =161) instead of MIOS32_USB_MIDI_SIZ_CLASS_DESC_SINGLE_USB (the 1-port length, =65, defined one line above at 93). Verified arithmetically: correct single-USB config size = 101 bytes, buggy macro = 197 bytes; array size (line 116) and reported wTotalLength (line 657) both inherit the wrong 197.
- **Suggested fix:** Change line 94 to reference MIOS32_USB_MIDI_SIZ_CLASS_DESC_SINGLE_USB instead of MIOS32_USB_MIDI_SIZ_CLASS_DESC. This corrects both the array allocation (line 653) and the wTotalLength bytes (lines 657-658).

#### #44 · vsprintf into fixed 128-byte stack buffer with only a format-length guard

**P2** · class `memory-safety` · cluster MIDI codec · confidence high

- **Location:** `mios32_midi.c` — MIOS32_MIDI_SendDebugMessage :958
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A caller passes a format containing %s (or many %d) whose expanded result exceeds 128 bytes, e.g. MIOS32_MIDI_SendDebugMessage("%s", long_str) with long_str > 127 chars, or several wide integer/float conversions. The only guard (line 952) checks strlen(format) > 100 — it does NOT bound the *expanded* output. vsprintf(str, format, args) at line 958 then writes past str[128] on the stack of TASK_MIDI_Hooks, smashing the return address / adjacent locals -> HardFault or silent corruption. The code comment at 949-951 admits this is 'a weak protection.'
- **Root cause:** vsprintf has no size bound; the length check is on the format string, not the rendered output. Current in-tree callers only pass short literal %s args, so it is latent rather than live.
- **Suggested fix:** Use vsnprintf(str, sizeof(str), format, args) instead of vsprintf.

#### #55 · Every note-off on the CAPTURE recording track does an unbounded-to-768 linear ring back-walk inside the MIDI drain under MUTEX_MIDIOUT

**P2** · class `rt-timing` · cluster LENS rt-timing · confidence medium

- **Location:** `seq_core.c` — SEQ_CORE_CaptureTapeTap :402
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** With CAPTURE armed on the visible track (seq_core_cap_ring_track < NUM_TRACKS) and the tape filled after ~16 bars (seq_core_cap_tape_count == 768), every drained note-off of the recording track invokes the passive tap SEQ_CORE_CaptureTapeTap from inside SEQ_MIDI_OUT_Handler (seq_midi_out.c:653, in the +4 task holding MUTEX_MIDIOUT). The note-off branch (line 402) walks back up to seq_core_cap_tape_count (768) ring entries with two modulo ops each to find the matching open note-on. If the matching on has scrolled out of the ring (held-long note, or note-off with no on), the loop runs the full 768 iterations and finds nothing. A chord release drains several note-offs in one tick, multiplying the cost (e.g. 6 offs x 768 = ~4600 modulo iterations) in a single 1ms emission slot while the note drain and UI are blocked on the mutex.
- **Root cause:** Linear LIFO search over a 768-entry ring on the hot drain path, worst case (no match) always scanning the entire tape, with no early-out bound.
- **Suggested fix:** Bound the back-walk to a small recent window (open note-ons are recent), or key note-ons by note number so the off match is O(1); at minimum cap the scan depth well below 768.

#### #54 · Catch-up/prefetch loop can run ~120 SEQ_CORE_Tick iterations (each rendering 16 tracks) in one 1ms emission slot while holding MUTEX_MIDIOUT

**P2** · class `rt-timing` · cluster LENS rt-timing · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_Handler :4357
- **Verification:** 2/3 skeptics kept · 2/3 confirmed against source
- **Failure scenario:** On a switch request, SEQ_CORE_AddForwardDelay sets bpm_tick_prefetch_req up to SwitchMarginMs()-worth of ticks ahead (margin capped at 250ms). At high BPM (e.g. 300 BPM, 384 ppqn) 250ms is ~120 ticks. The for(; bpm_tick<=bpm_tick_target; ++bpm_tick) loop at seq_core.c:4357 then runs SEQ_CORE_Tick ~120 times in a single SEQ_TASK_MIDI service, each iteration re-rendering up to 16 tracks and scheduling notes into the O(n) sorted SEQ_MIDI_OUT_Send queue, all inside the single MUTEX_MIDIOUT hold (app.c:845-858). The same window can be re-entered if the emission task was previously starved (TASK_MIDI resets xLastExecutionTime after a >5-tick gap, tasks.c:120). Result: a multi-ms MIDIOUT-mutex hold that starves the +2 UI and, combined with the burst render, risks a visible/audible hitch at the switch instant even without the SD read.
- **Root cause:** The forward-delay prefetch batches many ticks of full 16-track render+schedule work into one emission service under a single mutex hold, with worst-case size scaling with tempo.
- **Suggested fix:** Bound the per-service prefetch (process at most N ticks per call and carry the remainder), and/or release/re-acquire MUTEX_MIDIOUT between prefetched ticks so the drain and UI are not starved for the whole batch.

#### #68 · Non-HOLD arp read latches num_notes from live ARP_SORTED.len, then indexes items across a possible concurrent NOTESTACK_Pop

**P2** · class `concurrency` · cluster MIDI in/route · confidence medium

- **Location:** `seq_midi_in.c` — SEQ_MIDI_IN_ArpNoteGet :1261
- **Verification:** 2/3 skeptics kept · 2/3 confirmed against source
- **Failure scenario:** A track in Arpeggiator playmode with HOLD off is rendered from a task at priority +2 (render/UI) while the MIDI-in task (+3) processes a note-off on the same bus. Reader reads num_notes = bus_notestack[bus][ARP_SORTED].len (line 1261) = 3. MIDI-in preempts; NOTESTACK_Pop shifts note_items and drops len to 1. Reader resumes and evaluates note_ptr[key_num % 3] (line 1264): note_ptr (base captured at line 1225) is still valid but indices 1..2 now hold shifted/zeroed items, so the arp emits a stale or 0 (disabled) pitch for that step.
- **Root cause:** The getter reads notestack.len and dereferences notestack.note_items in separate unsynchronized statements; NOTESTACK_Pop (run under MUTEX_MIDIIN in the MIDI-in task) is not atomic w.r.t. these lock-free reads and the reader holds no mutex/critical section. Distinct from the reported UI-stack-reset race: the corruption here is a Pop-induced shift during an ordinary note-off and torn-reads len-vs-items, not a full stack reset.
- **Suggested fix:** Snapshot len into a local and clamp the index against that snapshot, or take MUTEX_MIDIIN / a short critical section around the len-read plus index (matching the writer's locking).

#### #4 · Glide/stretch note bitmask indexed by note/32 into 4-word arrays with an 8-bit note field and no defensive bound

**P2** · class `memory-safety` · cluster core tick+emit · confidence medium

- **Location:** `seq_core.c` — SEQ_CORE_Tick (glide_notes bitmask) :5481
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** The glide/stretched-note bookkeeping arrays glide_notes[4], prev_glide_notes[4] (line 5048) and next_glide_notes[4] (line 5049) are 128-bit (4x u32). They are written/read as glide_notes[p->note / 32] (line 5481), prev_glide_notes[p->note / 32] (5462), and prev/next_glide_notes[p->note>>5] (5219/5223/5441). mios32_midi_package_t.note is a full 8-bit field (mios32_midi.h:171 'u8 note:8'), so p->note in 128..255 yields index 4..7 and writes past glide_notes[3] into adjacent seq_core_trk_t fields (corrupting track state). Today every production pitch path clamps to 0..127 (SEQ_CORE_Transpose -> SEQ_CORE_TrimNote(...,0,127) at line 6016; render-stack pitch chain TrimNote at lines 1200/1211), so this is not currently triggerable, but a note>=128 arriving from a loaded/SysEx-set par buffer, a future processor slot, or the drum lay_const note path (tcc->lay_const[0*16+drum]) would overrun.
- **Root cause:** The bitmask index math assumes a 7-bit note (0..127 -> word 0..3) but the note storage is 8 bits and the write site has no bounds guard; safety currently rests entirely on upstream clamps rather than a local invariant.
- **Suggested fix:** Mask the index (p->note & 0x7f)/32, or guard the sustained/glide store/read with if(p->note < 128). Cheapest is to clamp note to 0x7f at the point events enter this loop.

#### #12 · Combined event-mode note/chord layer reads seq_cc_trk/par arrays at track+1/track+2 without bounding track to a group base

**P2** · class `memory-safety` · cluster layer/transforms · confidence low

- **Location:** `seq_layer.c` — SEQ_LAYER_GetEvents (Combined mode) :708
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** If a track with (track&7)==7 (i.e. track 7 or 15) is set to SEQ_EVENT_MODE_Combined (event_mode is loadable from SD / settable outside the normal V4L base-of-group convention), the Note/Chord cases execute SEQ_PAR_Get(track+1,...) and SEQ_PAR_Get(track+2,...) at lines 708/710/751/753. For track 15 that is SEQ_PAR_Get(16,...) and (17,...); SEQ_PAR_Get (seq_par.c:271) indexes par_layer_num_layers[16], par_layer_num_instruments[17] and seq_par_layer_value[16/17] with no track bound -> out-of-bounds reads past SEQ_CORE_NUM_TRACKS(16). For track 7 it silently reads track 8/9's data (cross-group logic corruption). The guard only special-cases (track&7)==1||2, assuming Combined always sits at a group base.
- **Root cause:** The Combined-mode velocity/length fetch assumes the note layer is at offset 0 within an 8-track group and only rejects offsets 1/2, so a Combined note track at group offset >=6 indexes neighbouring or out-of-array tracks.
- **Suggested fix:** Guard the Combined path so the note/chord layer only proceeds when (track&7)==0 (or bound track+1/track+2 to the track's group), and reject/clamp Combined event_mode at non-base tracks on load.

#### #0 · u8 pending-clock counter silently wraps and drops ~256 ticks when the emission task is stalled

**P2** (finder proposed P1) · class `resource-exhaustion` · cluster clock/BPM · confidence high

- **Location:** `seq_bpm.c` — bpm_req_clk_ctr / SEQ_BPM_Timer_Master / SEQ_BPM_ChkReqClk :105
- **Verification:** 2/3 skeptics kept · 2/3 confirmed against source
- **Failure scenario:** bpm_req_clk_ctr is a u8 (line 105). In master mode SEQ_BPM_Timer_Master does ++bpm_req_clk_ctr every timer ISR (~1 per ms at 140BPM/384ppqn, line 375) with no upper clamp. It is drained only by SEQ_BPM_ChkReqClk (one decrement per call, line 697), invoked from SEQ_CORE_Handler in TASK_MIDI, whose again-loop is capped at 10 iterations per 1ms wake (seq_core.c:4421). SEQ_CORE_Handler runs while holding MUTEX_MIDIOUT (app.c:845). A lower-prio task that grabs MUTEX_MIDIOUT across a long SD operation (the fork has documented ~640ms MIDIOUT/SDCARD-contention hangs) blocks TASK_MIDI for that whole window. During a ~640ms stall the ISR does ~570 increments; the u8 wraps past 255 (twice) while bpm_tick (u32) keeps counting correctly. On unblock, bpm_req_clk_ctr holds only a small residual, so ~512 pending clocks are permanently lost: the sequencer processes far fewer ticks than actually elapsed, skips every step that should have fired during the stall, and stays offset from bpm_tick / the echoed MIDI clock for the rest of the run.
- **Root cause:** Pending-clock accumulator sized u8 with no saturation; the design assumes the +4 emission task always drains within one tick, but a MUTEX_MIDIOUT priority-inversion stall violates that and the wrap is silent (no clamp, no catch-up-to-bpm_tick reconciliation).
- **Suggested fix:** Widen bpm_req_clk_ctr to u32 (or, on drain, reconcile against bpm_tick: derive outstanding = bpm_tick - last_processed_tick instead of trusting an 8-bit request count) so a multi-hundred-ms stall replays the missed span instead of dropping it.

#### #49 · FatFS shim tests only `< 0`, so a positive SD R1 error code is accepted as success (stale read / silently dropped write)

**P2** · class `persistence` · cluster vendor seams · confidence high

- **Location:** `diskio.c` — disk_read / disk_write :98
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** MIOS32_SDCARD_SectorRead/SectorWrite return the card's R1 status bits as a POSITIVE value on a non-timeout command error (mios32_sdcard.c:470 and :546 keep `status` positive when it is >0; a positive R1 also short-circuits before the DMA data phase via `goto error`). disk_read (line 98) and disk_write (line 128) only test `< 0`. So if a marginal/aging card (the memory notes describe hot-swap + a disposable aging test SD) answers READ_SINGLE_BLOCK / WRITE_SINGLE_BLOCK with e.g. R1=0x04 (illegal cmd) or 0x20 (address error), SectorRead returns e.g. 0x04 without ever running the 512-byte DMA, disk_read sees `0x04 < 0`==false, returns RES_OK, and FatFS consumes whatever stale bytes were already in `buff` as a valid sector -> a phrase/pattern/config file is read wrong; on write the sector is never programmed yet FatFS believes the save succeeded -> silent loss of a just-saved set.
- **Root cause:** Sign-convention mismatch at the vendor seam: the driver documents/returns R1 error bits as positive values, but the FatFS diskio adapter only treats negative returns as failure. Positive-but-nonzero is a real error yet is mapped to RES_OK.
- **Suggested fix:** In disk_read/disk_write treat any nonzero SectorRead/SectorWrite return as an error (`if (rc) return RES_ERROR;`), or have the driver collapse all R1-error returns to a negative code before returning.

### P3 — 42

#### #70 · Unprotected read-modify-write of the shared seq_core_state bitfield word can clobber +4-task transport/trigger flags

**P3** (finder proposed P2) · class `concurrency` · cluster HIL sysex · confidence high

- **Location:** `seq_testctrl.c` — cmd_reset_state :493
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** CMD_RESET_STATE arrives on the +3 SysEx RX task. Line 493 executes `seq_core_state.FREEZE = 0;`, which the compiler emits as a read-modify-write of the whole u16 bitfield word holding FREEZE plus FIRST_CLK, FORCE_REF_STEP_RESET, METRONOME, MANUAL_TRIGGER_STOP_REQ, MANUAL_TRIGGER_STEP_REQ, EXT_RESTART_REQ, LOOP, FOLLOW. If a RESET is issued without the stop bit (flags omitting 0x01) or with a transport-start/manual-trigger request in flight, the +4 emission task (SEQ_CORE_Handler/SEQ_CORE_Tick) can preempt between the RX task's load and store and set one of those sibling request bits; the RX task's store then overwrites it. Result: a lost MANUAL_TRIGGER_STEP_REQ (a dropped/missed live step) or a lost FIRST_CLK/EXT_RESTART_REQ (a mishandled clock start). The sibling `cmd_freeze_set` (line 2621) wraps the identical `seq_core_state.FREEZE=` write in portENTER_CRITICAL/portEXIT_CRITICAL, proving this word requires protection — the reset path omits it.
- **Root cause:** Bitfield assignment to one member of a multi-bit word is a non-atomic RMW; the reset path writes FREEZE without the critical section the codebase uses everywhere else for this word, while sibling bits are written concurrently by the higher-priority emission task.
- **Suggested fix:** Wrap the `seq_core_state.FREEZE = 0;` at line 493 in portENTER_CRITICAL()/portEXIT_CRITICAL() (or MIOS32_IRQ_Disable/Enable), matching cmd_freeze_set.

#### #57 · Signed-int overflow in TrackInit geometry check can accept pathological layer size from a corrupt bank file

**P3** (finder proposed P2) · class `persistence` · cluster LENS memory · confidence medium

- **Location:** `seq_par.c` — SEQ_PAR_TrackInit :182
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SEQ_FILE_B slot_track load (seq_file_b.c:1038) calls SEQ_PAR_TrackInit(dst, p_layer_size, num_p_layers, num_p_instruments) with p_layer_size read as a raw 16-bit value from the .V4 bank file and num_p_layers/instruments as raw bytes. TrackInit's guard `(instruments * par_layers * steps) > SEQ_PAR_MAX_BYTES` computes in signed int; with pathological values (e.g. instruments=255, layers=255, steps=65535) the product ~4.26e9 overflows INT_MAX (UB), can wrap negative, and the `> 1024` test passes -> oversized par_layer_num_steps/layers/instruments are committed. The bulk read itself is separately clamped to SEQ_PAR_MAX_BYTES (seq_file_b.c:1045), so the load doesn't overflow — but the retained oversized geometry then feeds sweep_window_render (seq_core.c:1507) which computes base = instr*layers*steps and memcpy(&par_buf[base+step_lo], ..., step_count) with base far beyond the 1024-byte buffer, corrupting CCM/adjacent tracks on the next render.
- **Root cause:** The overflow-check multiplication is performed in signed int without promoting to a wide unsigned type or range-checking each factor first, and TrackInit commits geometry that later hot-path memcpys trust to be <= MAX_BYTES.
- **Suggested fix:** Compute the product as u32 (or check each factor: instruments<=SEQ_CORE_NUM..., layers<=16, steps<=1024) before comparing to SEQ_PAR_MAX_BYTES; reject and leave prior geometry untouched on any out-of-range factor. Same pattern applies to SEQ_TRG_TrackInit.

#### #59 · echo_repeats bit-7 escapes the disable mask -> 128-191 echo events per note flood the MIDI-out pool

**P3** (finder proposed P2) · class `resource-exhaustion` · cluster core tick+emit · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_Echo :6249
- **Verification:** 2/3 skeptics kept · 2/3 confirmed against source
- **Failure scenario:** A stored/loaded or SysEx-set echo_repeats byte in 0x81..0xBF for any track: the tick caller's guard at seq_core.c:5584 tests only `(tcc->echo_repeats & 0x3f)` (0x81&0x3f=1, nonzero -> Echo is called), and inside Echo the disable test at 6244 is `echo_repeats & 0x40` (0x81&0x40=0 -> not disabled). The loop `for(i=0; i<echo_repeats; ++i)` at 6249 then runs 129 times, each iteration calling SEQ_CORE_ScheduleEvent -> up to a Note-On/Note-Off pair. One such step on one track queues ~258 events; across several tracks this overruns SEQ_MIDI_OUT_MAX_EVENTS (256) and drops scheduled notes/stuck-note-offs during live play.
- **Root cause:** The disable/normalize logic only strips/masks bits 0x40 and (in the robotize path) 0x0f; bit 0x80 of the raw u8 echo_repeats is never masked out of the loop count. SEQ_CC_Set (seq_cc.c:444) writes echo_repeats = value with no clamp, so config-file restore and SysEx CC-set can carry bit7 even though the UI (seq_ui_fx_echo.c) masks writes to 0x3f.
- **Suggested fix:** Clamp the loop count to the count field only: `u8 n = echo_repeats & 0x3f; for(i=0;i<n;++i)`, or mask echo_repeats to 0x7f on entry / in SEQ_CC_Set.

#### #7 · OnOff with len>0xffff truncates len into u16 and re-sends as OnOff (not Off) with velocity intact -> stuck note

**P3** (finder proposed P2) · class `logic` · cluster MIDI OUT sched · confidence high

- **Location:** `seq_midi_out.c` — SEQ_MIDI_OUT_Send :426
- **Verification:** 2/3 skeptics kept · 2/3 confirmed against source
- **Also independently found as:** #58
- **Failure scenario:** A caller schedules event_type=SEQ_MIDI_OUT_OnOffEvent with a u32 len>0xffff (e.g. len=0x10000, a ~682-quarter-note drone). Line 345 stores len into the u16 new_item->len, truncating 0x10000 to 0. The queued item is therefore an OnOff with len==0: in SEQ_MIDI_OUT_Handler line 658 `item->len` is false, so it sends the raw Note-On package and frees it with NO Off. Separately, line 426-427 recurses with event_type STILL == OnOffEvent (not OffEvent) and len=0 at timestamp+len, and the package's velocity is NOT zeroed (unlike the correct Off path which sets velocity=0). That second item is also an OnOff-len-0 -> another Note On, again no Off. Net: two Note-On messages, zero Note-Offs = a permanently stuck note. The comment says 'schedule off event now' but the code passes the On event_type and non-zero velocity.
- **Root cause:** len parameter is u32 but the queue item field is u16; the overflow branch reuses `event_type` (OnOff) and the un-zeroed midi_package instead of emitting a genuine OffEvent, and the primary item's len is silently truncated by the u16 store.
- **Suggested fix:** In the len>0xffff branch, store the On item with len=0 and issue the tail as SEQ_MIDI_OUT_OffEvent with velocity=0 at timestamp+len (matching the correct Off construction in the Handler and ScheduleEvent). Currently no in-fork caller passes u32 len>0xffff to an OnOff event (seq_core gatelength is u16; only metronome/roll use OnOff, all with small len), so this is latent, not live — but it is a real correctness/stuck-note trap for any future long-OnOff caller.

#### #8 · When an On event is dropped by pool exhaustion, its paired sentinel Off is still queued, consuming a slot for a note that never sounded

**P3** · class `resource-exhaustion` · cluster MIDI OUT sched · confidence medium

- **Location:** `seq_core.c` — SEQ_CORE_ScheduleEvent :4030
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** At line 4028 the On is sent; if the pool is at MAX_EVENTS-2 the failsafe in SEQ_MIDI_OUT_Send (seq_midi_out.c:316) drops the On and returns -1. But line 4030 gates only on `event_type == SEQ_MIDI_OUT_OnEvent` (independent of the On's return status), so the Off is still scheduled at 0xffffffff. Under sustained pool pressure this deposits sentinel Off items for notes that were never turned on: each consumes one of the reserved slots and later emits a spurious velocity-0 Note-Off. Harmless on most synths (Off for a silent note) but wastes scarce pool slots exactly when the pool is exhausted, and can emit stray Note-Offs that retrigger envelope/gate on some hardware.
- **Root cause:** The On/Off pairing does not propagate the On's allocation-failure status; the Off is scheduled unconditionally.
- **Suggested fix:** Only schedule the paired Off when the On actually queued (check the status return of the On Send before scheduling the sentinel Off). Low priority: the design doc explicitly accepts 'missing note, never stuck', and the reserved-2-slots policy plus FlushQueue limit the blast radius.

#### #60 · Quantized phrase-recall rephase and synch-to-measure both bump t->bar in the same measure tick -> nth-trigger phase skips a bar

**P3** · class `logic` · cluster core tick+emit · confidence medium

- **Location:** `seq_core.c` — SEQ_CORE_Tick :4600
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A track has clkdiv.SYNCH_TO_MEASURE set and an nth-play/nth-mute par value, and the user does a phrase recall whose SWITCH-QUANTIZE rephase lands on a measure boundary. On that tick (bpm_tick%96==0): the rephase set seq_core_state.reset_trkpos_req=0xffff (line 4590), so block A calls SEQ_CORE_ResetTrkPos + `++t->bar` (4599-4600); then synch_to_measure_req is set (4620/4643/4646) and the per-track synch block (4832-4834) calls ResetTrkPos + `++t->bar` again for the same track. t->bar advances by 2 in one musical measure. The nth-trigger tests `t->bar % (bar+1)` (5164, 5404), so the affected track's nth-play/nth-mute/accent cycle jumps a bar -> a step that should have fired (or muted) on this bar is evaluated on the wrong bar.
- **Root cause:** The fork's SWITCH-QUANTIZE recall-rephase path reuses reset_trkpos_req (block A, which increments t->bar) while the pre-existing synch-to-measure path (block below) independently increments t->bar; nothing dedups the two ResetTrkPos+bar-bump paths within one measure-boundary tick.
- **Suggested fix:** Guard the second bump: skip the synch-to-measure `++t->bar` (and/or ResetTrkPos) for tracks already reset in block A this tick (e.g. remember which tracks block A handled, or move the bar increment to a single measure-boundary site).

#### #61 · Roll2 flam trigger spacing/gate ignore clock divider (inner gatelength shadows the outer, never scaled by step_length)

**P3** · class `logic` · cluster core tick+emit · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_Tick :5526
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A melodic/drum track set to a non-16th clock divider (clkdiv.value>0, so t->step_length != 96) with a Roll2 parameter set (roll2_mode != 0). At line 5523 the outer u16 `gatelength` is computed, then line 5526 declares a NEW `int gatelength` that shadows it for the rest of the roll2 block. The trigger-loop timestamps `bpm_tick + t->bpm_tick_delay + i*gatelength` (5534) and `half_gatelength` (5528) use this inner value, which is `(4-(roll2_mode>>5))*((roll2_mode&0x1f)+1)` with NO multiplication by t->step_length/96 -- despite the comment at 5525 claiming it scales over the clock counter. Result: on a 1/8 or slower step, the roll2 flams cluster at the wrong (16th-relative) spacing and gate instead of spreading across the actual step length; the sibling roll1 path (5547) and single-trigger path (5578) do scale by t->step_length, so the two roll modes behave inconsistently.
- **Root cause:** Variable shadowing: the second `int gatelength` masks the outer u16, and its formula omits the `* t->step_length / 96` clock-divider scaling that the comment and the parallel roll1/single paths apply.
- **Suggested fix:** Remove the inner shadow and scale like the roll1 path: compute a single gatelength and, for the divider, multiply by t->step_length/96 (e.g. `gatelength = ((4-(roll2_mode>>5))*((roll2_mode&0x1f)+1) * t->step_length)/96;`).

#### #66 · Foreign-clkdiv SYNCH_TO_MEASURE track loses the last steps of the captured bar when tps does not divide gspm*96

**P3** · class `logic` · cluster CAPTURE · confidence medium

- **Location:** `seq_core.c` — SEQ_CORE_CaptureDstLoopSteps / CaptureSpanTape :568
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** For a SYNCH_TO_MEASURE track whose step_length (tps) does not evenly divide one global measure in ticks (gspm*96), e.g. gspm=16 (1536 ticks/bar) with a non-triplet clkdiv value=2 -> tps=18, 1536/18 = 85.33. CaptureDstLoopSteps floors this to dst_spm=85, so dst_steps=k*85 and the audible loop period P=gspm*96=1536 (HEARD) or the bar-marker span (GRID) is k*1536 ticks. In the materialize loop (seq_core.c:2704-2705) step=(e->tick - win_start)/tps can reach up to k*1536/18 = k*85.33, so notes in the final ~6 ticks of each bar produce step >= dst_steps and hit `if( step >= dst_steps ) continue;` (line 2705) — they are silently dropped from the capture. The STOPPED re-sim path has the analogous truncation: drive_ticks = dst_steps*tps = k*85*18 = k*1530 < k*1536 (seq_core.c:2505), so the last 6 ticks of the measure are never driven and any note there is lost.
- **Root cause:** dst_spm is computed as floor(gspm*96 / tps) (SEQ_CORE_CaptureDstLoopSteps line 568) while the window period P for a synch track is the exact gspm*96 ticks; when tps does not divide gspm*96 the step-count allocation is short of the true tick span, so the tail of every bar quantizes past the last allocated dst step.
- **Suggested fix:** Either round dst_spm up (ceil) and cap the drive/window to dst_steps*tps consistently, or refuse the capture (return a negative status) for synch tracks whose tps does not divide gspm*96, matching how other unrepresentable configurations are refused.

#### #67 · HOLD-mode arp num_notes counts to 0 when MIDI note 0 (C-2) is held → modulo-by-zero

**P3** · class `logic` · cluster MIDI in/route · confidence high

- **Location:** `seq_midi_in.c` — SEQ_MIDI_IN_ArpNoteGet :1264
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** On an Arpeggiator-playmode track with the HOLD flag set, hold a chord that includes MIDI note number 0 (e.g. a keyboard/loopback in a low octave, or lower-note filter at 0). SEQ_MIDI_IN_Receive_Note copies the held notes into arp_{sorted,unsorted}_hold[bus] (line 834-835). In ArpNoteGet the hold branch counts notes by scanning until note_ptr[i].note==0 (line 1257-1259). Because note value 0 is a legal MIDI note but is also the array terminator sentinel, a held note 0 in slot 0 makes num_notes=0, and line 1264 evaluates key_num % 0.
- **Root cause:** num_notes is derived by treating note value 0 as 'no note' (sentinel), but 0 is a valid MIDI note, and the same expression then divides by num_notes with no guard. On the ARM EABI x%0 returns 0 (no fault; DIV_0_TRP is not enabled in this fork's CCR) and the trailing '!num_notes' term forces bit 7, so the arp note is silently disabled rather than crashing — the arp goes silent for that key instead of playing note 0.
- **Suggested fix:** Guard the divisor ('if(!num_notes) return 0x80;' before the modulo) and/or track hold-slot occupancy with a real length field instead of a note==0 sentinel so MIDI note 0 is representable.

#### #10 · Modulo-by-zero on groove num_steps loaded from SD as a multiple of 256

**P3** (finder proposed P2) · class `resource-exhaustion` · cluster layer/transforms · confidence medium

- **Location:** `seq_groove.c` — SEQ_GROOVE_DelayGet / SEQ_GROOVE_Event :269
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Also independently found as:** #32
- **Failure scenario:** MBSEQ_G.V4 is hand-edited/corrupted with `NumSteps <g> 256` (or any multiple of 256). In seq_file_g.c:194 `int num_steps = get_dec(word)` = 256 passes the `num_steps > 0` guard (line 195), but the target field seq_groove_templates[g].num_steps is u8 (seq_groove.h:41), so it truncates to 0. On the next groove-enabled step, SEQ_GROOVE_DelayGet:269 and SEQ_GROOVE_Event:302 execute `step %= g->num_steps` i.e. `step %= 0` -> integer division-by-zero, a UsageFault on Cortex-M4 when div-trap is enabled (or undefined result otherwise), inside the emission path.
- **Root cause:** The >0 validity check is applied to the pre-truncation int while the stored field is u8, so a multiple of 256 becomes a stored 0; the runtime modulo does not defend against num_steps==0.
- **Suggested fix:** Validate the value after truncation (reject if (u8)num_steps==0 or clamp to 1..16) in seq_file_g.c, and/or guard `if(!g->num_steps) return 0;` before the modulo in both SEQ_GROOVE_DelayGet and SEQ_GROOVE_Event.

#### #32 · Groove template num_steps > 16 from MBSEQ_G.V4 causes OOB read of add_step_* arrays on the emission path

**P3** (finder proposed P1) · class `memory-safety` · cluster persistence · confidence high

- **Location:** `seq_groove.c` — SEQ_GROOVE_DelayGet / SEQ_GROOVE_Event :269
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #10
- **Failure scenario:** A hand-edited/foreign MBSEQ_G.V4 with a line `NumSteps 0 20` is loaded by SEQ_FILE_G_Read (seq_file_g.c:190-197), which only validates `num_steps > 0` before storing into the u8 field seq_groove_templates[0].num_steps. The arrays add_step_delay/length/velocity are each only [16] (seq_groove.h:42-44). At emission time SEQ_GROOVE_DelayGet (seq_groove.c:269) and SEQ_GROOVE_Event (302) do `step %= g->num_steps` (0..19) then read `g->add_step_delay[step]` / `add_step_velocity[step]` / `add_step_length[step]`, reading up to 3 bytes past each 16-element array. For the last template the velocity array overrun reads past the whole struct. Result: wrong groove delay/velocity/gate (torn timing/velocity) and OOB reads on the hard-real-time per-note path.
- **Root cause:** SEQ_FILE_G_Read does not clamp num_steps to the 16-element array size; the runtime uses it directly as a modulus and array index without bounds.
- **Suggested fix:** In SEQ_FILE_G_Read clamp `num_steps` to 1..16 before storing; and/or bound the index in SEQ_GROOVE_* (`if(step >= 16) step %= 16;`).

#### #18 · TensionBandMask feeds seq_core_global_scale to NoteValueGet, which indexes seq_scale_table with no bounds check

**P3** · class `memory-safety` · cluster GRAVITY · confidence low

- **Location:** `seq_scale.c` — SEQ_SCALE_NoteValueGet :297
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #11
- **Failure scenario:** SEQ_CORE_TensionBandMask (seq_core.c:1342,1363) reads scale = seq_core_global_scale and calls SEQ_SCALE_NoteValueGet(60+pc, scale, root) in a 12-iteration loop to build scaleMask. SEQ_SCALE_NoteValueGet indexes seq_scale_table[scale] (seq_scale.c:297) WITHOUT the scale>=SEQ_SCALE_NumGet() guard that SEQ_SCALE_CtrlGet uses (seq_scale.c:272). If seq_core_global_scale ever holds a value >= the table size (corrupt config load via seq_file_c.c, or a future writer that bypasses the SHADE ladder), every gripped note on a tension track reads an out-of-table byte and snaps to a garbage pitch class. SHADE only sets valid indices (12-18), so this is latent on the current path.
- **Root cause:** NoteValueGet trusts its scale argument; TensionBandMask (like FTS) passes the global scale unvalidated. No clamp of seq_core_global_scale at the tension read site.
- **Suggested fix:** Clamp scale to SEQ_SCALE_NumGet()-1 in SEQ_SCALE_NoteValueGet (the durable fix, benefits FTS too) or in TensionBandMask before the scaleMask loop. Out-of-cluster ownership but reachable from the GRAVITY field.

#### #11 · Out-of-bounds read of seq_scale_table when a per-track scale par-layer byte exceeds the table size

**P3** · class `memory-safety` · cluster layer/transforms · confidence medium

- **Location:** `seq_scale.c` — SEQ_SCALE_NoteValueGet :297
- **Verification:** 3/3 skeptics kept · 2/3 confirmed against source
- **Also independently found as:** #18
- **Failure scenario:** A track has link_par_layer_scale assigned and a step's scale par byte is set high (e.g. 200) via UI/record/generator. SEQ_CORE_FTS_GetScaleAndRoot (seq_core.c:6040 / seq_core.c:1108) computes *scale = par_byte - 1 = 199 with no upper clamp against SEQ_SCALE_NumGet() (167 entries). SEQ_SCALE_NoteValueGet:297 then indexes seq_scale_table[199].notes[...] past the 167-entry const table, reading adjacent .rodata as scale-degree data -> a wrong (but bounded, in-flash) forced-scale pitch. No crash, recoverable when the byte is corrected.
- **Root cause:** SEQ_SCALE_NoteValueGet/NextNoteInScale trust the caller-supplied scale index and never bound it to SEQ_SCALE_NumGet(); the per-track scale par-layer path does not clamp the value like the global-scale UI setter does.
- **Suggested fix:** Clamp scale to SEQ_SCALE_NumGet()-1 at the top of SEQ_SCALE_NoteValueGet (and NextNoteInScale/PrevNoteInScale), or clamp *scale in SEQ_CORE_FTS_GetScaleAndRoot.

#### #47 · I2C2 (port 0) error IRQ install guarded by wrong macro (MIOS32_IIC1_ENABLED) - copy/paste typo

**P3** (finder proposed P2) · class `concurrency` · cluster timers/sys/iic/bsl · confidence high

- **Location:** `mios32_iic.c` — MIOS32_IIC_Init :245
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** On a build that enables IIC0 but disables IIC1 (MIOS32_IIC0_ENABLED>0, MIOS32_IIC1_ENABLED undefined/0), the block at line 245 '#if defined(MIOS32_IIC1_ENABLED) && MIOS32_IIC0_ENABLED > 0' evaluates false, so MIOS32_IRQ_Install(I2C2_ER_IRQn,...) is skipped. The port-0 error interrupt is never enabled: a slave NACK / bus error / arbitration-lost on I2C2 never runs ER_IRQHandler, so transfer_state.BUSY is never cleared and transfer_error never set by the error path. Every failed transfer (e.g. addressing an absent slave) then hangs until the 5ms MIOS32_IIC_TIMEOUT_VALUE fires + Deblock recovery, instead of an immediate NACK -> 5ms+ stall per attempt and possible bus wedge.
- **Root cause:** Guard uses MIOS32_IIC1_ENABLED where it should test MIOS32_IIC0_ENABLED (all three sibling blocks were meant to key the port-0 ER install on IIC0). Latent because STM32F4 default MIOS32_IIC_NUM=2 enables both ports.
- **Suggested fix:** Change the guard on the I2C2_ER_IRQn install to '#if defined(MIOS32_IIC0_ENABLED) && MIOS32_IIC0_ENABLED > 0'.

#### #3 · u8 gen_off_events truncates the scaled stretched-glide gatelength on slow clock dividers, cutting the note short

**P3** (finder proposed P2) · class `logic` · cluster core tick+emit · confidence high

- **Location:** `seq_core.c` — SEQ_CORE_Tick (gen_off_events) :5335
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Also independently found as:** #5
- **Failure scenario:** A melodic track in glide/stretch state (t->state.STRETCHED_GL && t->state.SUSTAINED) with a large clock divider so step_length exceeds ~258 (clkdiv.value>=43 -> step_length>=264) and a stored step length e->len<96 (e.g. 95). Line 5335 computes gen_off_events = (t->step_length * e->len)/96, e.g. 264*95/96 = 261, but gen_off_events is a u8 (line 5129) so 261 truncates to 5. At line 5341 rescheduled_tick = bpm_tick + prev_bpm_tick_delay + 5 instead of +261, so the Note Off for the held/glided note is scheduled ~256 ticks (well over a step) too early. Audible result: the stretched/glided note is cut short (or, for other e->len/divider combinations whose truncated value lands below prev_bpm_tick_delay's expectation, the off ordering vs the new on is disturbed). The same truncating expression is on the velocity-cleared continue path at line 5234.
- **Root cause:** gen_off_events is declared u8 (line 5129) but is assigned (t->step_length * e->len)/96 which, for step_length>258 and e->len near 96, exceeds 255. The variable doubles as both a boolean 'play off' flag and the remaining-gatelength tick offset added to rescheduled_tick at line 5341; the tick-offset role needs u16 range. step_length reaches (clkdiv.value+1)*6 up to 1536, so the product/96 reaches ~1520.
- **Suggested fix:** Widen gen_off_events to u16 (it is used as a tick offset at lines 5341, not just a flag), and mirror the same at the line 5234 assignment. Verify no other consumer relies on it being a byte.

#### #5 · Glide-note tracking stores the post-HUMANIZE note but dedups against the pre-HUMANIZE note, breaking glide continuity when humanize is active

**P3** · class `logic` · cluster core tick+emit · confidence low

- **Location:** `seq_core.c` — SEQ_CORE_Tick (glide dedup vs humanize order) :5481
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #3
- **Failure scenario:** In the first pass the glide dedup at line 5218-5227 tests prev_glide_notes[p->note/32] using p->note as it stands there, then SEQ_HUMANIZE_Event (line 5273) and SEQ_LFO_Event (5276) can shift p->note, and the second pass stores the held note into t->glide_notes[p->note/32] using the mutated value (line 5481). Across two adjacent glide steps of the same source note, humanize randomization can make the stored bit and the next step's compared bit refer to different note numbers, so the 'same note already glided' short-circuit (which delays the retrigger a tick / suppresses a duplicate on, lines 5462, 5218-5226) mis-fires: a glide that should tie instead retriggers, or a genuinely new note is suppressed.
- **Root cause:** The glide-continuity bookkeeping keys on note number but the note number is not stable across the Pre-FX (humanize/LFO) stage; the check and the store observe the value at different points in the pipeline.
- **Suggested fix:** Key glide tracking on the pre-FX note (snapshot before SEQ_HUMANIZE_Event, as prefx_note is already captured at line 5272 for the FTS-change path) consistently for both the compare and the store, or disable note-humanize for glided steps.

#### #1 · SEQ_BPM_TickSet does not clear bpm_req_clk_ctr; a stale request count can clobber a freshly-set tick position

**P3** (finder proposed P2) · class `concurrency` · cluster clock/BPM · confidence medium

- **Location:** `seq_bpm.c` — SEQ_BPM_ChkReqClk :691
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SEQ_BPM_TickSet (line 285-290) writes bpm_tick with no IRQ guard and leaves bpm_req_clk_ctr untouched. Callers reposition the transport via TickSet (seq_song.c:525/549 Fwd/Rew, seq_core.c:2303 capture restore, seq_core.c:4322). If a nonzero bpm_req_clk_ctr is still pending (e.g. clocks queued by the ISR before the reposition, not yet drained because the guard at line 687 was blocking on a start/stop/songpos request) and the new tick set is smaller than that count, the next SEQ_BPM_ChkReqClk hits `if(bpm_req_clk_ctr > bpm_tick) bpm_tick = bpm_req_clk_ctr` (line 691-693) and yanks bpm_tick back up to the residual request count, discarding the just-set position. The 'never negative' guard (comment line 692) is also effectively dead after the first ~256 ticks since bpm_req_clk_ctr is u8 and bpm_tick outgrows 255.
- **Root cause:** TickSet mutates one half of the (bpm_tick, bpm_req_clk_ctr) invariant without resetting the other, and the guard that was meant only for startup underflow protection can now overwrite a deliberate reposition.
- **Suggested fix:** Clear bpm_req_clk_ctr (and sent_clk_ctr) inside SEQ_BPM_TickSet under IRQ-disable, or have repositioning callers do so, so a reposition starts from a clean request state.

#### #2 · Read-modify-write of bpm_tick in Fwd/Rew races the master timer ISR while running

**P3** · class `concurrency` · cluster clock/BPM · confidence medium

- **Location:** `seq_song.c` — SEQ_SONG_Fwd / SEQ_SONG_Rew :518
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SEQ_SONG_Fwd (seq_song.c:518-525) and Rew (542-549) do bpm_tick = SEQ_BPM_TickGet(); ...compute next_bpm_tick...; SEQ_BPM_TickSet(next_bpm_tick) from the UI task (+2) with no IRQ-disable around the read/compute/write. In master mode while the sequencer is running (run_mode==Clocked), SEQ_BPM_Timer_Master (highest-prio ISR) does ++bpm_tick concurrently (line 374). Any ISR increments landing between the UI's TickGet and TickSet are overwritten, so a Fwd/Rew issued during playback lands a few ticks earlier than intended (position drift). The 32-bit store itself is atomic on Cortex-M4, so no torn word — the defect is the lost-update RMW race.
- **Root cause:** Non-atomic read-modify-write of a variable that a highest-priority ISR also mutates.
- **Suggested fix:** Wrap the read-compute-write in MIOS32_IRQ_Disable/Enable, matching the pattern already used in SEQ_BPM_Start/Stop/Cont.

#### #17 · GRIP write while stopped runs a full track render with interrupts masked

**P3** (finder proposed P2) · class `rt-timing` · cluster GRAVITY · confidence medium

- **Location:** `seq_cc.c` — SEQ_CC_Set (case SEQ_CC_TENSION_GRIP) :510
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** On the GRAVITY page with transport STOPPED, turning the GRIP encoder calls SEQ_UI_CC_Inc(SEQ_CC_TENSION_GRIP) -> SEQ_CC_Set. The whole SEQ_CC_Set body runs inside portENTER_CRITICAL()/portEXIT_CRITICAL() (seq_cc.c:319..519, vPortRaiseBASEPRI masks all syscall-priority IRQs). The TENSION_GRIP case calls SEQ_CORE_TensionSlotSync -> SEQ_CORE_RenderTouched, and because SEQ_BPM_IsRunning() is false, RenderTouched calls SEQ_CORE_RenderTrack(track) synchronously (seq_core.c:933-934). On a large track (up to 256 steps x layers x instruments) that is a full-buffer memcpy plus a 4-processor stack pass (SEQ_CORE_RenderTrack, seq_core.c:1604-1630) executed with interrupts disabled. Any BPM/USB/MIDI-IN ISR arriving during that window (e.g. an external START or incoming clock right as the user adjusts GRIP before pressing play) is delayed by the whole render time.
- **Root cause:** SEQ_CORE_RenderTouched's stopped-transport synchronous-render branch is invoked from inside SEQ_CC_Set's portENTER_CRITICAL region; the render is not bounded and runs with interrupts masked.
- **Suggested fix:** Move the SlotSync/RenderTouched call outside the critical section (mirror SEQ_CC_LinkUpdate which is deliberately called after portEXIT_CRITICAL at seq_cc.c:299-302), or have the stopped-path render defer to a non-critical context. Note this is a shared idiom across SlotSync CC writes, not unique to TENSION_GRIP.

#### #23 · Chord-record target-step read of t->step / timestamp_next_step_ref races the emission task

**P3** · class `rt-timing` · cluster play/record · confidence medium

- **Location:** `seq_ui_inssel.c` — SEQ_UI_INSSEL_RecordChord :193
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Also independently found as:** #52
- **Failure scenario:** While the sequencer is running (BPM running, not STEP_RECORD), RecordChord reads t->step and t->timestamp_next_step_ref (seq_ui_inssel.c:193,195,198) without disabling IRQs. The +4 emission task (SEQ_CORE_Tick) advances t->step and rewrites timestamp_next_step_ref concurrently. If a step boundary lands between the two reads, the forward-snap decision uses a step index and a next-step timestamp from different steps, so the atomically-written chord lands on the previous or an off-by-one step (a chord that audibly falls on the wrong beat). Not memory corruption (u8/u32 reads are individually atomic), but a wrong-placement glitch under contention.
- **Root cause:** The target-step computation samples two related live fields (t->step and t->timestamp_next_step_ref) non-atomically relative to the emission task that updates them together.
- **Suggested fix:** Snapshot t->step and t->timestamp_next_step_ref together under MIOS32_IRQ_Disable/Enable (or portENTER_CRITICAL) before computing the snap, matching how the stock record path treats these under the surrounding IRQ-disabled sections.

#### #52 · Target step for atomic chord record is read from engine-owned t->step / timestamp_next_step_ref outside the mutex

**P3** · class `concurrency` · cluster LENS concurrency · confidence medium

- **Location:** `seq_ui_inssel.c` — SEQ_UI_INSSEL_RecordChord :191
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #23
- **Failure scenario:** When recording a chord while the transport runs (SEQ_BPM_IsRunning && !STEP_RECORD), the UI task (+2) reads step=t->step (191) and t->timestamp_next_step_ref/SEQ_BPM_TickGet (193-198) BEFORE taking MUTEX_MIDIOUT at 217. The +4 emission task (SEQ_CORE_Handler under the same mutex) can advance t->step and timestamp_next_step_ref between these unlocked reads and the locked SEQ_PAR_Set writes. If the +4 tick fires a NextStep in that window, the chord is written to a step that is now one behind the playhead, so the recorded chord lands on the wrong (already-passed) step and is heard a full loop late instead of on the intended step.
- **Root cause:** The step-selection reads are single-byte (t->step u8) / single-word (u32) so not torn, but they are logically stale: they sample engine cursor state outside the mutex that serializes the write, so the read->decide->write is not atomic w.r.t. the tick's own step advance.
- **Suggested fix:** Move the step computation (191-208) inside the MUTEX_MIDIOUT critical section (take the mutex at the top of the function) so t->step/timestamp_next_step_ref are sampled and acted on atomically w.r.t. the emission task; the code comment already acknowledges the stock path races the step, and this keeps the fork's atomic replacement actually atomic.

#### #22 · Held-note tracking arrays are not reentrancy-safe across the physical vs MIDI-remote button paths

**P3** · class `concurrency` · cluster play/record · confidence medium

- **Location:** `seq_ui_inssel.c` — SEQ_UI_INSSEL_KbdNote :286
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SEQ_UI_INSSEL_Button_Handler (hence KbdNote) is driven both by physical DIN buttons and by MIDI-remote note injection (seq_ui.c:3582 SEQ_UI_Button_Handler(note+0x00,...)). If a remote GP-key event for key K preempts an in-progress physical press of the same key K (or vice-versa) between the clear loop (inssel_kbd_held[key][i]=0xff, line 286-287) and the store loop (line 288-289), the two writers interleave: the release path can read a half-populated inssel_kbd_held[key]/inssel_kbd_recorded_chord[key], leaving a note whose NoteOn was sent but whose NoteOff is never emitted -> stuck/phantom live note.
- **Root cause:** inssel_kbd_held[16][3] and inssel_kbd_recorded_chord[16] are plain statics mutated without IRQ/critical protection, on the assumption of a single serialized button task; the MIDI-remote injection path can enter the same handler at a different priority.
- **Suggested fix:** Guard the per-key held-note read-modify-write (clear+store, and the release drain) with MIOS32_IRQ_Disable/Enable, or serialize both button sources through one task.

#### #24 · UI-task stack reset is unsynchronized vs the higher-priority MIDI-in mutator → torn notestack / lost or corrupted transposer/arp note

**P3** (finder proposed P2) · class `concurrency` · cluster MIDI in/route · confidence high

- **Location:** `seq_midi_in.c` — SEQ_MIDI_IN_ResetSingleTransArpStacks / SEQ_MIDI_IN_ResetChangerStacks :321
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** The user selects MIDI page 'Reset Stacks' (seq_ui_midi.c:443), TRKJAM reset (seq_ui_trkjam.c:734), or the menu reset (seq_ui_menu.c:527). SEQ_UI_Handler runs in TASK_Period1mS at tskIDLE+2 and calls SEQ_MIDI_IN_ResetTransArpStacks → NOTESTACK_Init → NOTESTACK_Clear, which sets n->len=0 then loops clearing n->size items — WITHOUT taking MUTEX_MIDIIN. An external Note-On arriving on the bus is processed by TASK_MIDI_Hooks at tskIDLE+3 (APP_MIDI_NotifyPackage → SEQ_MIDI_IN_Receive → SEQ_MIDI_IN_BusReceive → NOTESTACK_Push), which takes MUTEX_MIDIIN. Because +3 > +2 the MIDI-in path PREEMPTS the reset mid-Clear, and the mutex it holds does not protect the reset (the reset never acquires it). The pushed note lands (len becomes 1, item[0] set); the reset then resumes its clear loop and re-zeroes item[0] and/or leaves transposer_hold_first/last_note and arp_sorted_hold/arp_unsorted_hold inconsistent with the stack. Result: a just-played note is silently wiped or the hold notes are left stale, so the +4 emission task's next transpose/arp render (SEQ_MIDI_IN_TransposerNoteGet / SEQ_MIDI_IN_ArpNoteGet) emits a wrong or missing pitch. No memory corruption (indices stay < size), but an audible dropped/wrong note during a live 'clear stacks' gesture.
- **Root cause:** The trans/arp/changer notestacks are shared between three priorities (UI reset at +2, external MIDI mutate at +3, emission read/loopback-mutate at +4). The mutate paths take MUTEX_MIDIIN but the UI reset paths do not, and a mutex cannot serialize a higher-priority preemptor against a lower-priority holder anyway — the reset must run inside portENTER_CRITICAL/IRQ-disable (as SEQ_MIDI_IN_ResetChangerStacks already does for the play_section write) or under the same lock the mutators use, applied on the reader side too.
- **Suggested fix:** Wrap the notestack Init/Clear + hold-note reseed in SEQ_MIDI_IN_ResetSingleTransArpStacks (and the changer reset) in portENTER_CRITICAL()/portEXIT_CRITICAL() so it is atomic against both the +3 MIDI-in Push/Pop and +4 emission access, matching the existing atomic block at seq_midi_in.c:390-396.

#### #25 · Bus getters index bus_notestack[bus] with no bus-range guard (unlike the sibling PC-set/lowest-note getters)

**P3** · class `memory-safety` · cluster MIDI in/route · confidence medium

- **Location:** `seq_midi_in.c` — SEQ_MIDI_IN_TransposerNoteGet / SEQ_MIDI_IN_ArpNoteGet :1199
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** SEQ_MIDI_IN_TransposerNoteGet (line 1199) and SEQ_MIDI_IN_ArpNoteGet (line 1220) read bus_notestack[bus][...] and transposer_hold_first_note[bus]/arp_*_hold[bus] with no 'if(bus >= SEQ_MIDI_IN_NUM_BUSSES) return' check, whereas SEQ_MIDI_IN_BusPCSetGet (1163) and SEQ_MIDI_IN_BusLowestNoteGet (1185) both have it. Today every caller passes tcc->busasg.bus (a 2-bit field, 0..3) or a processor slot->bus that is only ever assigned from tcc->busasg.bus / tcc->chordmask_bus (masked to &0x03) / literal 0, so bus is always 0..3 == in range. The bug is latent: any future caller, a widened slot->bus (it is a full u8 in seq_processor_slot_t, seq_core.h:267), or a persistence/version path that loads an out-of-range bus would drive an out-of-bounds read of the static notestack arrays inside the +4 emission task.
- **Root cause:** Inconsistent defensive bounds-checking across the four bus getters; the two that feed the fork's render signature (PITCH/ARP) omit the guard that the two chord-context getters have.
- **Suggested fix:** Add 'if(bus >= SEQ_MIDI_IN_NUM_BUSSES) return -1;' at the top of both SEQ_MIDI_IN_TransposerNoteGet and SEQ_MIDI_IN_ArpNoteGet to match BusPCSetGet/BusLowestNoteGet.

#### #56 · Note >127 in SEQ_LIVE_PlayEvent overruns seq_live_played_notes[4] and live_keyboard_*[128]

**P3** (finder proposed P2) · class `memory-safety` · cluster LENS memory · confidence medium

- **Location:** `seq_live.c` — SEQ_LIVE_PlayEvent :221
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #21
- **Failure scenario:** SEQ_LIVE_PlayEvent computes note_ix32 = p.note / 32 and indexes seq_live_played_notes[note_ix32] (line 226/239/241) — but that array is only 4 words (128 notes). It also indexes live_keyboard_port/chn/note[p.note] (lines 230-232, 303-305) into 128-element arrays. p.note is an 8-bit package field (0..255). If any caller passes a note > 127, note_ix32 reaches 7 (OOB write into the 4-word bitmap, clobbering adjacent .bss) and live_keyboard_[p.note] writes up to index 255 (128-past-end). The fork's own melodic surface clamps notes to 0..127 (SEQ_SCALE_WalkScale caps, chromatic path clamps at seq_ui_inssel.c), and real MIDI notes are 7-bit, so this is NOT triggerable through fork code today. It is reachable only if a drum instrument's LAY_CONST note (seq_ui_inssel.c:109 -> DrumTrigger, and seq_layer drum path) is stored > 127, which the UI/CC layer generally clamps. Latent, but the guard is absent and one bad-config path would corrupt live-play bookkeeping.
- **Root cause:** The played-notes bitmap (128 bits) and live_keyboard_* arrays (128 entries) are sized for the MIDI 7-bit note range, but SEQ_LIVE_PlayEvent never clamps p.note before indexing them; it trusts every caller to pass a valid 0..127 note. Effective-note clamping (SEQ_CORE_TrimNote at line 258) happens AFTER note_ix32 and only on the transposed value, not on the raw index into these arrays.
- **Suggested fix:** Add `if( p.note > 127 ) return -1;` (or clamp) at the top of the NoteOn branch before computing note_ix32, so the played-notes bitmap and live_keyboard_* indexing are always in range regardless of caller.

#### #36 · RX FIFO overflow silently drops MIDI bytes, corrupting the in-flight parser event

**P3** (finder proposed P2) · class `resource-exhaustion` · cluster UART MIDI · confidence high

- **Location:** `mios32_uart.c` — USART2_IRQHandler / MIOS32_UART_RxBufferPut :775
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** If TASK_MIDI_Hooks (tskIDLE+3, which runs MIOS32_MIDI_Receive_Handler -> MIOS32_UART_MIDI_PackageReceive) is preempted/starved by the +4 emission task or +3 SysEx/SD work for >~20ms during a sustained inbound stream (SysEx dump or a busy MIDI controller at 31250 baud ~= 1 byte/320us -> 64-byte buffer fills in ~20ms), MIOS32_UART_RxBufferPut returns -2. In the ISR (line 775) the DR byte was already read at line 771, so the byte is dropped with no error path ('here we could add some error handling' is empty). The running-status parser in PackageReceive then sees a truncated event: e.g. a Note On loses its velocity byte, so the next unrelated data byte is consumed as velocity -> wrong note velocity, or a status byte is lost -> subsequent data bytes misparsed until the 1s timeout resets the record.
- **Root cause:** Fixed 64-byte rx_buffer (MIOS32_UART_RX_BUFFER_SIZE) with no overflow signalling and an empty error branch; overrun is only tolerable if the reader task is never starved that long, which the +4 emission spin (finding #1) and +3 SD contention can violate.
- **Suggested fix:** At minimum count/flag RX overruns so it is observable; consider raising MIOS32_UART_RX_BUFFER_SIZE for MIDI-in ports, or draining RX in the ISR/higher-prio context. Also verify USART ORE (overrun) flag is being cleared — the ISR only tests RXNE (bit5) and TXE (bit7).

#### #37 · Cross-priority race on rs_expire_ctr/rs_last between the +3 periodic task and +4 emission task; the 'atomic not required' comment is inaccurate

**P3** · class `concurrency` · cluster UART MIDI · confidence medium

- **Location:** `mios32_uart_midi.c` — MIOS32_UART_MIDI_Periodic_mS / MIOS32_UART_MIDI_PackageSend_NonBlocking :243
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** MIOS32_UART_MIDI_Periodic_mS runs in TASK_MIDI_Hooks (tskIDLE+3) and does ++rs_expire_ctr[port] under IRQ_Disable. MIOS32_UART_MIDI_PackageSend_NonBlocking runs in the +4 emission task and does an unguarded read of rs_expire_ctr (line 273) and unguarded writes rs_expire_ctr=0 (line 295) and rs_last=evnt0 (line 303). Because +4 preempts +3, an increment in Periodic_mS can be interleaved/lost against the emission task's reset, shifting the 1-second running-status expiry by up to one tick. Worst observable effect: a status byte is re-sent one cycle early or held one cycle too long on a freshly (re)connected DIN cable.
- **Root cause:** The comment at line 243 asserts 'atomic operation not required ... due to single-byte accesses', but rs_expire_ctr is u16 and is written from two different-priority task contexts without a lock. On Cortex-M4 an aligned u16 store is atomic (so no torn value), which downgrades this from corruption to a lost-update timing skew, but the stated rationale is wrong and the access is genuinely unsynchronized.
- **Suggested fix:** Guard the rs_expire_ctr read/reset and rs_last write in PackageSend_NonBlocking with MIOS32_IRQ_Disable/Enable (as RS_Reset already does), or accept it and fix the misleading comment. Low musical impact; flag mainly because the comment could mask a future widening of these fields.

#### #42 · spi_callback[] is written in task context and read in the DMA ISR without volatile/barrier

**P3** · class `concurrency` · cluster SPI/DMA/SD · confidence medium

- **Location:** `mios32_spi.c` — spi_callback :147
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** spi_callback[spi] (line 147) is a plain (non-volatile) function-pointer array. It is stored in MIOS32_SPI_TransferBlock at line 950 from task context, then dereferenced in the DMA2_Stream2/DMA1_Stream3/DMA1_Stream2 IRQ handlers (lines 1024/1032/1040) at IRQ priority 5. In this fork the only async (non-NULL-callback) user is SRIO on SPI1, driven single-threaded from the 1ms scan, and each SD transfer is NULL-callback + serialized under the J16 mutex, so no torn/stale read is actually reachable today. The exposure is latent: the write at line 950 is not fenced against the store to the DMA enable register at line 998/1001, and the aligned 32-bit pointer store is atomic on Cortex-M4 only by luck of alignment, not by contract. A future second async client on the same SPI, or a compiler that hoists/caches the load, could invoke a stale or NULL callback (missed SRIO DOUT latch / wrong callback) after a transfer completes.
- **Root cause:** Shared-with-ISR state declared without `volatile` and set without an explicit compiler/memory barrier before the DMA is armed.
- **Suggested fix:** Declare `static void (* volatile spi_callback[3])(void);` (the surrounding volatile MMIO writes already order it in practice, but volatile documents and guarantees the ISR-visible read).

#### #43 · CheckAvailable re-deactivates CS and gives an already-given mutex on the not-was_available success path

**P3** · class `logic` · cluster SPI/DMA/SD · confidence medium

- **Location:** `mios32_sdcard.c` — MIOS32_SDCARD_CheckAvailable :345
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** With was_available==0 and CMD0 succeeding, the code at line 339 does MIOS32_SDCARD_MUTEX_GIVE, calls MIOS32_SDCARD_PowerOn() (which takes/gives the mutex itself), then falls through to the `not_available:` label at line 345 and executes MIOS32_SPI_RC_PinSet + a dummy TransferByte + a SECOND MIOS32_SDCARD_MUTEX_GIVE at line 350 — all outside any held mutex. Because the app's J16 mutex is a FreeRTOS recursive mutex (xSemaphoreGiveRecursive, tasks.h:65) and CheckAvailable runs single-threaded from the 1s poll task, the extra give is a no-op / harmless in the current build, and the CS pin write is on a bus nobody else is using at that instant. But an unbalanced give on a non-recursive or shared mutex, or a concurrent SD user, would corrupt the mutex count. It is a real take/give imbalance that only survives on the recursive-mutex + single-caller assumption.
- **Root cause:** The `else` (not-was_available) branch releases the mutex early at line 339 and then shares the common `not_available:` cleanup epilogue that assumes the mutex is still held.
- **Suggested fix:** Give the mutex exactly once per take; restructure so the not_available epilogue is only reached while the mutex is still held, or skip the second GIVE on the branch that already released at line 339.

#### #39 · USB-host MIDI enumeration reads Ep_Desc[i][1] unconditionally without checking bNumEndpoints, opening a channel from stale endpoint data if the interface has only one endpoint

**P3** · class `logic` · cluster USB MIDI · confidence medium

- **Location:** `mios32_usb_midi.c` — USBH_InterfaceInit :465
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A USB-host-connected MIDI device exposes an Audio/MIDIStreaming interface (class 1, subclass 3) with bNumEndpoints==1 (e.g. an output-only or input-only bulk device, or an odd descriptor layout). USBH_InterfaceInit reads pphost->device_prop.Ep_Desc[i][1].bEndpointAddress/wMaxPacketSize (lines 465-471) which was never populated for this interface (leftover from a prior enumeration or zero), then calls USBH_Alloc_Channel/USBH_Open_Channel with that bogus endpoint address and size. The bulk in/out channel pair is opened against a nonexistent endpoint, so subsequent USBH_BulkReceiveData/USBH_BulkSendData in USBH_Handle transfer against a wrong/zero-size endpoint -> the attached device's MIDI never works, and the transfer state machine can churn on URB_ERROR. Index is in-bounds (Ep_Desc is [2][2]) so no memory corruption; failure is functional.
- **Root cause:** No guard on pphost->device_prop.Itf_Desc[i].bNumEndpoints before dereferencing both Ep_Desc[i][0] and Ep_Desc[i][1]; the code assumes every matched MIDI interface has exactly 2 endpoints.
- **Suggested fix:** Only process Ep_Desc[i][1] when bNumEndpoints >= 2, and require that exactly one IN and one OUT endpoint were found before calling ChangeConnectionState(1).

#### #40 · DeviceNotSupported() is invoked when the MIDI interface IS available (inverted condition) on the USB-host path

**P3** · class `logic` · cluster USB MIDI · confidence low

- **Location:** `mios32_usb_midi.c` — USBH_InterfaceInit :496
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** In USB-host mode, after a supported MIDI interface is found and ChangeConnectionState(1) makes MIOS32_USB_MIDI_CheckAvailable(0) return 1, line 496 does `if( MIOS32_USB_MIDI_CheckAvailable(0) ) pphost->usr_cb->DeviceNotSupported();` -> the user callback for an UNSUPPORTED device fires precisely when a MIDI device WAS successfully attached. Depending on the user-callback implementation this can log/flag a spurious 'device not supported' or drive a wrong UI/LED state for a device that actually works. Condition appears inverted (should trigger when NOT available).
- **Root cause:** Missing negation: the intended guard is `if( !MIOS32_USB_MIDI_CheckAvailable(0) )` to report unsupported devices, but the `!` is absent.
- **Suggested fix:** Confirm intent against upstream MIOS32; if reporting unsupported devices, negate the condition to `if( !MIOS32_USB_MIDI_CheckAvailable(0) )`.

#### #41 · CIDRead start-token timeout falls through into a spurious 16-byte DMA read on a disconnected card

**P3** · class `logic` · cluster SPI/DMA/SD · confidence high

- **Location:** `mios32_sdcard.c` — MIOS32_SDCARD_CIDRead :648
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #50
- **Failure scenario:** Card is pulled (or NACKs) after CMD10 is accepted so the data-start-token never arrives. The wait loop at lines 643-647 exhausts all 65536 iterations; line 648 sets status=-257 but — unlike the sibling MIOS32_SDCARD_CSDRead which does `goto error` at line 737 — there is NO `goto error` here. Execution falls straight through to line 654, issuing MIOS32_SPI_TransferBlock(...,cid_buffer,16,NULL) plus 3 more TransferByte reads against an absent/unready card, then populates cid->* fields from the 16 bytes of garbage in cid_buffer[] before returning -257 at the error label. The caller (modules/file/file.c:1755) checks `<0` so the return code is handled correctly and the garbage struct is discarded, making this benign in outcome — but it is a clear divergence from the CSDRead pattern and clocks the SPI bus needlessly during a hot-unplug.
- **Root cause:** Missing `goto error;` after `status = -257;` on the CID data-token timeout path; CSDRead (line 735-738) has it, CIDRead does not.
- **Suggested fix:** Add `goto error;` inside the `if( i == 65536 )` block at line 648-649 to match CSDRead.

#### #50 · Missing `goto error` on start-token timeout: DMA-reads and parses garbage into the CID struct

**P3** · class `logic` · cluster vendor seams · confidence high

- **Location:** `mios32_sdcard.c` — MIOS32_SDCARD_CIDRead :648
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Also independently found as:** #41
- **Failure scenario:** In CIDRead, the start-token wait loop at lines 643-647 sets `status=-257` on timeout (line 649) but, unlike the identical loop in CSDRead (line 737 has `goto error`), CIDRead has NO `goto error`. Execution falls through to line 654 MIOS32_SPI_TransferBlock into cid_buffer (reading 16 bytes that are not a real data block because the start token never arrived) and then parses that garbage into every cid-> field before returning -257. If a caller ever ignores the negative return and uses the populated CID (e.g. serial number / product name), it gets garbage. Recoverable because status is still negative.
- **Root cause:** Copy/paste divergence from CSDRead: the timeout branch omits the `goto error` that skips the data phase.
- **Suggested fix:** Add `goto error;` after `status = -257;` in the CIDRead start-token timeout branch, matching CSDRead.

#### #58 · OnOff note with len>0xffff can drop its Off when pool is near-full, leaving a stuck note

**P3** · class `resource-exhaustion` · cluster LENS exhaustion · confidence low

- **Location:** `seq_midi_out.c` — SEQ_MIDI_OUT_Send :426
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Duplicate of:** #7
- **Failure scenario:** SEQ_MIDI_OUT_Send is called with event_type==OnOffEvent and len>0xffff while seq_midi_out_allocated == SEQ_MIDI_OUT_MAX_EVENTS-3. The failsafe at line 316 (>= MAX-2) does NOT trip, so the On item is allocated (allocated now MAX-2). Line 426-428 then recurses to schedule the split Off, but the recursion passes event_type UNCHANGED (still OnOffEvent) with len=0. That recursive call re-hits the failsafe (allocated==MAX-2 >= MAX-2, type is OnOffEvent) and returns -1. Result: the On is queued but its Off is silently dropped -> a hung note until the next FlushQueue. Only reachable with a gate length >65535 ticks (>170 quarter notes) AND a pool within 3 slots of full, so not realistically hit in performance.
- **Root cause:** The len>0xffff split recursion reuses OnOffEvent as the event_type instead of OffEvent, so the split-off half is still subject to the On/OnOff near-full failsafe that a plain Off would bypass; combined with the -2 headroom being consumed by the just-allocated On.
- **Suggested fix:** In the len>0xffff branch, schedule the tail as SEQ_MIDI_OUT_OffEvent (which is exempt from the near-full failsafe), mirroring the drain-time OnOff->Off reschedule at line 691, so the Off cannot be refused after the On was accepted.

#### #64 · len>0xffff OnOff split schedules the Off using the already-delayed timestamp, so port delay is applied twice to the Off

**P3** · class `logic` · cluster MIDI OUT sched · confidence medium

- **Location:** `seq_midi_out.c` — SEQ_MIDI_OUT_Send :427
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** On a port with a nonzero ppqn_delay, an OnOff event with len>0xffff (~>65535 ticks) is sent. By line 427 the local 'timestamp' has already had 'delay' added (line 331). The recursive call SEQ_MIDI_OUT_Send(port, ..., timestamp+len, 0) passes the delayed timestamp, and that recursive call adds 'delay' again — so the split-out Off lands at timestamp+len+2*delay instead of timestamp+len+delay. The Off is misplaced by one delay quantum (a few ms), lengthening/shortening the note. Low severity: len>0xffff is an extreme corner (very long OnOff) and the offset is small; only manifests with a per-port delay set. Related to the already-reported truncation on this same path but is a distinct timing defect.
- **Root cause:** The recursion reuses the local timestamp after it was mutated by the delay adjustment, double-counting the delay when SEQ_MIDI_OUT_Send re-applies it.
- **Suggested fix:** Compute the Off timestamp from the pre-delay input, or subtract 'delay' back out before the recursive call, so delay is applied exactly once.

#### #26 · CMD_PAGE_SET stores an unvalidated page id into the global ui_page, leaving the UI on a non-existent page with all NULL callbacks

**P3** · class `logic` · cluster HIL sysex · confidence high

- **Location:** `seq_testctrl.c` — cmd_page_set :526
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** Host sends F0 00 00 7E 4F 54 31 <page=0x7f> F7. cmd_page_set masks page to 0..127 and calls SEQ_UI_PageSet((seq_ui_page_t)0x7f). SEQ_UI_PageSet (seq_ui.c:471) does NOT bounds-check its argument: it unconditionally sets ui_page=0x7f and NULLs ui_button/encoder/led/lcd/exit/midi_in callbacks. It then calls SEQ_UI_PAGES_CallInit(0x7f), which returns -1 early (0x7f >= SEQ_UI_PAGES) WITHOUT reinstalling any callbacks. Result: the sequencer is left parked on a wild page index with every UI callback NULL — the control surface (buttons/encoders/LEDs/LCD) goes dead until the next legitimate CMD_PAGE_SET or SEQ_UI_PageSet. No OOB read occurs (ui_menu_pages[ui_page] is only touched inside SEQ_UI_PAGES_CallInit/PageNameGet, all guarded by page>=SEQ_UI_PAGES), so this is a soft UI-lockout, not memory corruption.
- **Root cause:** cmd_page_set masks the payload to 7 bits (page & 0x7f) but never validates page < SEQ_UI_PAGES before SEQ_UI_PageSet, and SEQ_UI_PageSet itself has no range guard (it trusts callers to pass a valid enum). The 0x7f mask admits values far above the real page count.
- **Suggested fix:** In cmd_page_set, reject page >= SEQ_UI_PAGES with status 0x02 (or 0x03) before calling SEQ_UI_PageSet/CallInit; alternatively add a range clamp/guard at the top of SEQ_UI_PageSet so no caller can install a wild ui_page.

#### #29 · RandomGenerator leaves ui_selected_instrument set to a drum index and uses a stale instrument for parameter-layer randomization

**P3** · class `logic` · cluster UI pages · confidence medium

- **Location:** `seq_ui_trkrnd.c` — RandomGenerator :518
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** In RandomGenerator the trigger-layer branch (drum mode) sets ui_selected_instrument = i-16 (line 529) and never restores it. When a subsequent request randomizes a parameter layer (i<16), it calls SEQ_PAR_Set(visible_track, step, layer, ui_selected_instrument, ...) (line 518) with whatever drum index was last written, not instrument 0. On a Generate-all pass the iteration order (par layers i<16 first, then triggers) means par writes use the entering ui_selected_instrument, but the persisted mutation of the global drum cursor can land par randomization on the wrong drum instrument on the next single-layer Generate, and leaves the drum cursor pointing at a high instrument after leaving the page. SEQ_PAR_Set bounds-checks (returns -1) so there is no OOB write — the failure is wrong/no randomization on the intended line, not corruption.
- **Root cause:** The routine mutates the shared global ui_selected_instrument as a scratch variable instead of passing the target instrument explicitly, so par-layer and trg-layer branches interfere across invocations.
- **Suggested fix:** Pass the target instrument as a local; for par layers use instrument 0 (or the intended drum) explicitly and don't clobber the global ui_selected_instrument, or save/restore it around the loop.

#### #33 · Off-by-one track guard (`>` instead of `>=`) admits track==SEQ_CORE_NUM_TRACKS

**P3** (finder proposed P2) · class `memory-safety` · cluster persistence · confidence high

- **Location:** `seq_file_t.c` — SEQ_FILE_T_Read :94
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** The entry guard is `if( track > SEQ_CORE_NUM_TRACKS ) return SEQ_FILE_T_ERR_TRACK;`. SEQ_CORE_NUM_TRACKS is 16 and seq_cc_trk[] has 16 elements. If any caller ever passes track==16, the check passes and line 97 does `tcc = &seq_cc_trk[16]` (one past the array), and subsequent tcc->... writes corrupt adjacent RAM. Current callers pass 0..15 (VisibleTrackGet/track), so latent today, but the guard is wrong.
- **Root cause:** Boundary comparison uses `>` where a valid index must be `< SEQ_CORE_NUM_TRACKS`, so the equal case leaks through.
- **Suggested fix:** Change to `if( track >= SEQ_CORE_NUM_TRACKS )`.

#### #34 · Unchecked SEQ_PAR_TrackInit/SEQ_TRG_TrackInit return leaves stale geometry on oversized file layer config

**P3** · class `logic` · cluster persistence · confidence medium

- **Location:** `seq_file_b.c` — SEQ_FILE_B_PatternRead :762
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** A bank pattern slot whose stored geometry gives instruments*layers*steps > SEQ_PAR_MAX_BYTES (1024) — e.g. a foreign bank, or a slot read from FatFs gap data in a sparsely-written bank (the file itself warns of this at lines 969-972). SEQ_PAR_TrackInit returns -1 without setting par_layer_num_* or zeroing the row (seq_par.c:182-183), but PatternRead ignores the return (line 762). The subsequent bulk read into seq_par_layer_value[track] is capped to SEQ_PAR_MAX_BYTES so there is no overflow, but the track keeps its previous layer count/step count while its value bytes are half-overwritten -> wrong pitches/params for that track until re-partitioned. Same for SEQ_TRG_TrackInit at line 778 and in SEQ_FILE_B_TrackRead (1038/1057).
- **Root cause:** Deserialize path trusts the file geometry and does not react to TrackInit's rejection; the array write is bounds-safe but the RAM geometry/value state becomes inconsistent.
- **Suggested fix:** Check the TrackInit return; on failure abort the record with SEQ_FILE_B_ERR_FORMAT (or fall back to a safe default geometry) rather than continuing to stream layer bytes.

#### #48 · Shadowed 'error' in scan retry loop makes retry condition/availability logic ineffective

**P3** · class `logic` · cluster timers/sys/iic/bsl · confidence high

- **Location:** `mios32_iic_midi.c` — MIOS32_IIC_MIDI_ScanInterfaces :211
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** In the while(error<0 && retries--) loop the body redeclares 's32 error' (line 211), shadowing the outer 'error' (line 208) which stays -1 forever. The loop therefore always runs the full 3 retries even after a module ACKs on the first try (wasted ~50us per present module during a once-per-second scan), and the loop exit is driven only by retries, not by success. iic_port_available is still set correctly from the inner error, so availability detection is functionally OK, but the intended early-out and the outer error state are dead.
- **Root cause:** Inner 's32 error =' declaration shadows the outer variable that the loop condition tests.
- **Suggested fix:** Drop the inner 's32 ' so the assignment updates the outer error; then the loop early-exits on first success.

#### #69 · Remote refresh detection compares REMOTE_CMD to REMOTE_CMD_COMPLETE and &&'s a nonzero constant, so the 'is REFRESH' gate is wrong

**P3** · class `logic` · cluster MIDI in/route · confidence high

- **Location:** `seq_midi_sysex.c` — SEQ_MIDI_SYSEX_Cmd_Remote :453
- **Verification:** 2/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** At SYSEX_CMD_STATE_END the intent is 'if this command was a REFRESH, echo a Client-Mode request'. The condition is 'REMOTE_CMD_VALID && REMOTE_CMD == REMOTE_CMD_COMPLETE && SYSEX_REMOTE_CMD_REFRESH'. SYSEX_REMOTE_CMD_REFRESH is the constant 0x01, so that operand is always true; and REMOTE_CMD (8-bit command id) is compared to REMOTE_CMD_COMPLETE (a 1-bit completion flag). The intended 'REMOTE_CMD == SYSEX_REMOTE_CMD_REFRESH' test is therefore never actually performed, so SendMode(CLIENT) fires/does-not-fire on the wrong criterion.
- **Root cause:** Typo: third operand should be 'REMOTE_CMD == SYSEX_REMOTE_CMD_REFRESH'. Pre-existing upstream code; reachable only in MBSEQ remote-server SysEx mode, off the live audio path, so impact is a spurious or missing Client-Mode timeout-reset echo, not corruption.
- **Suggested fix:** Replace with 'if( sysex_state.remote_lcd.REMOTE_CMD_VALID && sysex_state.remote_lcd.REMOTE_CMD == SYSEX_REMOTE_CMD_REFRESH )'.

#### #71 · A new F0 arriving mid-payload is stored as a data byte instead of resyncing the parser, swallowing the next command

**P3** · class `logic` · cluster HIL sysex · confidence medium

- **Location:** `seq_testctrl.c` — SEQ_TESTCTRL_Parser :2721
- **Verification:** 3/3 skeptics kept · 3/3 confirmed against source
- **Failure scenario:** The reset-on-status-byte guard at line 2721 explicitly exempts 0xf0 (`midi_in != 0xf0`). If a prior TESTCTRL message loses its trailing F7 (transmission glitch / dropped USB packet) the parser is stuck in STATE_PAYLOAD. When the host then sends the next well-formed message beginning with F0, that F0 is not treated as an abort/resync: at line 2754 it is not 0xf7, so it falls to line 2968 and is appended into payload_buf as an ordinary data byte, and the following header/cmd bytes are likewise consumed as payload of the stale command. The stale command then either never dispatches (still waiting for an F7) or dispatches with corrupted payload, and the intended new command is lost — the host sees a spurious timeout for a command it framed correctly.
- **Root cause:** The parser owns its own framing but does not resynchronize on a fresh SysEx-start (0xf0) while mid-payload; only the SysEx TimeOut hook recovers, so a single dropped F7 desyncs at least one following command.
- **Suggested fix:** In STATE_PAYLOAD, treat an incoming 0xf0 as an abort-and-restart: reset parser_state/header_ctr and re-enter STATE_HEADER (header_ctr=1) instead of buffering it.

## Coverage & honest limitations

**Well covered.** Concurrency between the +4 emission ISR/task and the +2/+3 UI/MIDI-in/SysEx tasks is exhaustively covered (bpm_tick, notestacks, generator pool, journal, live-play bitmaps, render double-buffer) — this is clearly the review's deepest lens and matches the fork's known freeze/timing history. File/SysEx untrusted-input validation is well covered across every persistence format (T/S/G/B/preset). The MIDI-out scheduler sentinel/len/delay family and the CAPTURE-fidelity family are both covered thoroughly with concrete musical failure modes. Platform driver review (USB device+host, bootloader, IIC, SPI, SD/FatFS) got solid breadth. Clock-divider/tick scaling logic was probed well.

**Thin or uncovered.** Many findings are explicitly flagged as latent-only (not reachable through current fork code): #4, #11, #18, #25, #33, #56 all depend on a future writer or corrupt config to trigger, so real-world exploitability is unquantified — no finder attempted to actually construct a triggering file/SysEx and confirm the fault on hardware or emulator. No performance/CPU-budget quantification: several RT-timing findings (#54 ~120 renders, #55 768-walk, #35 UART spin) assert magnitudes but none are measured against the fork's documented bench harness (CMD_SWITCH_PERF etc.). Timing-jitter findings assume worst-case BPM/ppqn without confirming those settings are used live. The MEMORY note's known walls (recall-freeze, trigger-gens) are represented (#53, #15/#16) but there is no coverage of the SEQ_FILE phrase/session (PH/AN sentinel-bank) read/write paths that dominate the fork's save model, nor of the CHECKPOINT/REVERT sentinel-bank 0xfe/0xfd logic. No review of the LFO/humanize/robotize/echo render-processor stack beyond the single echo_repeats overflow (#59). Chord-layer and arp render paths beyond the notestack races are thin.

**Recommended follow-ups.** 1) Bench-measure the four RT-timing claims (#53, #54, #55, #35) with the existing CMD_*_PERF probes to convert asserted magnitudes into real numbers and confirm which actually breach the 1ms slot at live-realistic BPM. 2) Add a single hardening pass over untrusted-input entry points: clamp track index (fix #33 >= guard), reject sub-header song_size (#31), mask note/CC bytes to 7 bits at SEQ_CC_Set/file load (kills #4,#11,#18,#21,#56 latent family at the source), and validate groove/scale/geometry on load (#10,#32,#11,#57). 3) Audit the phrase/session/CHECKPOINT SD read-write paths (PH/AN 0xfd/0xfe banks) with the same lens applied here — they are the fork's core save model and were not reviewed. 4) Decide a locking discipline for the render double-buffer while playing (bounce, stopped-edit sweep, GRIP) — findings #6/#17/#62 are three faces of one broken single-writer contract and want one structural fix, not three patches. 5) Fix the printf-as-format bug (#28) immediately — it is user-reachable memory corruption with a trivial fix.

### Method caveats
- Findings are static-analysis + source-reading verdicts. None were reproduced on hardware or emulator; the RT-timing magnitude claims (render cost, buffer-walk length, UART spin) are *asserted*, not *measured*. Confirm the P1 audible/timing items with the existing `CMD_RENDER_PERF 0x46` / `CMD_CAPTURE_PERF 0x48` / `CMD_SWITCH_PERF 0x44` bench probes before and after any fix.
- 'Duplicate' groups were merged by the synthesizer, not re-verified as identical; treat them as strong hints, not proof of sameness.
- UI pages (55 files) were sampled, not exhaustively swept — see the cluster list above for what was read in full.

