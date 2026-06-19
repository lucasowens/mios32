# Pre-build recon — gotchas + optimizations (2026-06-19)

**Provenance:** a deep multi-agent code pass (8 subsystems × scan + adversarial verify →
synthesis; 122 source-verified findings) run before committing to the six designed builds.
Full prioritized findings below; **the build-changing corrections have been folded into the
design doc** (§9 / §10(a) / §10(c) + the new §10 "Pre-build recon" subsection). This file is
the durable findings reference — keep it next to the build.

Build keys: **RNG** = per-track-rng · **SELF** = self-bus · **SET** = set-baseline ·
**UNDO** = unified undo/redo · **CAP** = retroactive capture · **TRIG** = trigger generators.
Constraints: CCM free ~9 KB (binding), main SRAM ~33 KB free, pattern SAVE ~290 ms
flash-bound, emission 1 ms-quantized, two-round tick.

---

## A. Gotchas that should change the build

**A1. Generators draw from the GLOBAL RNG and record nothing — the keystone blocker.** → RNG, CAP
`seq_generator.c:103-190` (9 `SEQ_RANDOM_Gen_Range` sites in `reroll_pitch`/`perturb_pitch`/`mutate_loop`)
all hit `jsw_rand()` via the single static `random_value` (`seq_random.c:35,113`). `seq_generator_t`
carries **no** RNG field. So a captured span with an engaged generator cannot be offline-re-simulated,
and draw order depends on track/pool iteration order. **Do:** Port every generator draw to caller-owned
state via `SEQ_RANDOM_GenRangeXorshift(u32*)` (`seq_random.c:82`, already proven by robotize). Store a
u32 seed per pool slot or per-track. **Trap:** `SEQ_RANDOM_Gen_Range` short-circuits *without advancing*
when `min==max` (`seq_random.c:99-100`) — never assume one call == one draw.

**A2. CHORD_MASK gate uses the global RNG — the load-bearing exception to per-track determinism.** → RNG, CAP
`seq_core.c:470,485`: `(u32)SEQ_RANDOM_Gen_Range(0,126) < p->strength`, per-note-per-step, sequenced
behind whatever else drew that tick. **Do:** Convert this gate to a pure `(track,instrument,step,zone)`
hash exactly like `tension_grip_hash` (`seq_core.c:742-750` — one xorshift round, zero captured state).
A naive per-track seed is *not* step-replayable unless derived per-(track,step) or itself captured.

**A3. Self-route into config dirties the group every step → ~290 ms writeback churn at each switch.** → SELF, SET
`SEQ_PATTERN_DirtySetTrack` sets `seq_pattern_dirty |= group_bit` **unconditionally** (`seq_pattern.c:242`);
the existing `seq_generator_in_automutate` gate (`seq_generator.c:51`) suppresses only `phrase_drift`
(`:249-250`), **NOT** `seq_pattern_dirty`. So self-routed CC writes at step rate force a full-group
~290 ms save per switch. **Do:** Add a NEW `seq_config_in_self_route`-style flag that suppresses
`seq_pattern_dirty` during self-route dispatch — the automutate gate is insufficient.

**A4. SET save inherits the ~290 ms-per-pattern flash wall × N dirty groups.** → SET
Pattern SAVE ≈ 290 ms (flash-PROGRAM-bound), LOAD ≈ 22 ms. A fully-dirty SET = WritebackAllDirty
(≤4 group writes) + FILE_Copy of all banks ≈ 1.2 s+. **Do:** Dispatch SetSave/SetLoad off the live
path via APP_SEQ_Task request flags under `MUTEX_SDCARD`, with LCD progress. Off-live-path is
mandatory — the flash wall cannot be margined away (`SEQ_CORE_SwitchMarginMs` caps at 250 ms).

**A5. SD/MIDIOUT mutex ordering: SD I/O must run in task context with no port-critical held.** → SET, CAP
`SEQ_CORE_LoadTrackFromSlot` takes `MUTEX_SDCARD` *before* `portENTER_CRITICAL` (`seq_core.c:1899-1903`,
comment 1889-1890: "SDCARD must be taken BEFORE entering the critical section or the take hangs"). The
documented recall-freeze class. **Do:** SET copy + any SD-touching capture path do all SD I/O in task
context, no port-critical held, mutexes in the established order.

**A6. Note-grain self-transforms MUST be render-stack processors, or they're invisible to bounce AND same-tick loopback.** → SELF, CAP, TRIG
Groove/humanize/LFO/robotize/echo are emission-time-only and reset on bounce (§3:189-198); only
render-stack processors write `OutputActive` and are bounce-faithful. **Do:** Birth self-transpose/
self-chord-mask and any trigger generator as render-stack processors reading a control layer
(`control_layer[step]`, a read — no `SEQ_PAR_Set`, no dirty), never as emission effects.

**A7. Render-stack self-bus reads see PRIOR-tick bus state (deterministic one-tick lag).** → SELF
`SEQ_CORE_RenderTracks()` runs at the tick prologue (`seq_core.c:2520`) *before* the two-round loopback
loop (`:2716`); PITCH reads the transposer bus at `:587`. A render-stack self-route can't see *this*
tick's loopback writes. Not a race (single-threaded), but a real ordering constraint. **Do:** Spec
self-route reads against the one-tick render lag; don't assume same-tick visibility.

**A8. Four render-stack fences block any naive note-read/trigger-gen: Arp, Drum, Chord, and note-0.** → SELF, TRIG, CAP
PITCH/LIMIT early-return on `playmode==Arpeggiator` (`seq_core.c:570,664`) and `event_mode==Drum`
(`:571`); chord par-layers store an **index 0..15, not pitch** (`seq_par.h:39,51,55`; engine skips
non-Note layers `:614-618`); on drums **note value 0 = lay_const kit-preset fallback, NOT rest**
(`seq_layer.c:394`, gate from `SEQ_TRG_Get`). A reader that writes "0 = silence" on a drum leaves the
kit firing phantom hits; one that reads a chord index as a note gets wrong pitch. **Do:** Every new
render-stack reader must replicate the `legacy_pitch` guard set (Arp ‖ Drum ‖ Chord1/2/3) and the
`is_note` filter; treat note-0 per mode.

**A9. Re-sim fidelity differs ~32× by build: device scheduler pool=256, host sim=8192.** → CAP, TRIG, SELF
`SEQ_MIDI_OUT_Send` silently refuses On/OnOff at headroom ≤2 (`seq_midi_out.c:296`); device
`SEQ_MIDI_OUT_MAX_EVENTS=256` (`mios32_config.h:142`) vs host `8192`. Offline re-sim on the host won't
drop where the device would. **Do:** Clamp host re-sim to 256 so capture-to-static reproduces device
drop/stall behavior. Watch peak on dense arp+echo+robotize+self-bus.

**A10. Self-route target blacklist — REFINED by the traversal pass (2026-06-19).** → SELF
Genuinely off-limits (reshape the *buffer*: realloc layers / re-run SlotSyncs mid-tick):
**EVENT_MODE** (LinkUpdate + 2 SlotSyncs, `seq_cc.c:341-346`), **MODE** (4 SlotSyncs, `:329-335`),
**par_assignment_drum** (LinkUpdate, can change `num_p_layers`, `:419-434`).
**LENGTH** is NOT a ban — it modulo-wraps the live step pointer *immediately* (`seq_cc.c:382-386`),
but **deferred to the `ref_step==0` boundary** (reuse the phrase-morph dirty-flag pattern,
`seq_core.c:2601-2626`) it lands cleanly. **Direction, clock-divider, loop, and the progression
section (forward/jmpbck/replay/repeat/skip/rs-interval) are SAFE per-step** — see the traversal
verdict in the design doc §10(c). Confirmed-safe scalars: groove, transpose, robotize dials,
humanize, echo, LFO.

**A11. Generator pool is a shared cap-64, first-come, no fairness — trigger-gens can starve it.** → TRIG, RNG
`alloc_slot` (`seq_generator.c:271-279`) linearly returns first free slot, NULL on full, LRU deferred.
A dense drum track can exhaust 64 slots and starve another track's triggers. A *separate* full-size
(180 B × 64 = 11.25 KB) trigger pool **does not fit** in 8.86 KB CCM free. **Do:** Decide pre-build —
share with a fairness/reservation rule, OR a small separate pool with a slim gate-loop struct (a 64-bit
pattern + dials ≈ 16-24 B makes a separate pool cheap). This sizing choice *is* the decision.

**A12. SET copy of a legacy-firmware bank carries forward a too-small reserved pattern_size → gen state silently dropped.** → SET
Ext-tag arbitration degrades V4→V3→none by fit against `header.pattern_size` (`seq_file_b.c:1546-1558`);
reserved size is fixed at slot create time. A byte-for-byte FILE_Copy of an old session's bank preserves
the smaller reservation, so gen state won't round-trip even on V4 firmware. **Do:** SetSave should
re-Create V4-sized destination banks and re-write records (not raw-copy), OR document that SETs made from
legacy sessions lose gen state.

**A13. Unified UNDO must define re-engage semantics — the existing gen undo arms only on FIRST engage.** → UNDO, TRIG
Generator `undo_slot` is one-deep, par-bytes only (1026 B, `seq_generator.c:61-67`); the **re-engage path
returns before snapshotting** (`:341-349`), so UNDO after a re-engage reverts to a stale first-engage
snapshot. Track undo is global one-deep (2318 B CCM). **Do:** The consolidated journal must explicitly
define re-engage and multi-step-gesture (ENGAGE+ROLL+BOUNCE) commit-as-unit semantics; gate on
`!seq_generator_in_automutate`.

---

## B. Optimizations worth doing now

**B1. Reuse robotize's proven per-track seed-ring + per-bar snapshot as the CAPTURE / RNG precedent.** → CAP, RNG
`robotize_seed_snapshots[16]` (`seq_core.h:138`, **main SRAM**), appended per measure at `seq_core.c:3591`/`3619`;
`SEQ_ROBOTIZE_PopulateAnchorsFromWindow` (`seq_robotize.c:110`) is the K-bar grab UX. `RobotizeLoopBarTick`
already re-seeds from `bar_anchors` per bar — the re-sim hook exists. Generalize this exact pattern.

**B2. `ref_step==0` block is the clean per-bar hook for the capture ring snapshot.** → CAP, TRIG
`seq_core.c:2601-2626` runs TensionResolve, PhraseMorph, and per-track robotize ticks before the emission
loop. Snapshot seed-frame + bus state here. **Note:** robotize_measure_ctr advances on the GLOBAL boundary
(polymetric tracks share one clock); **generators wrap on each track's OWN step-0** (`seq_generator.c:631-632`)
— the frame must record *both* clocks.

**B3. Reuse `SEQ_CORE_CaptureTrackOutput` as the snapshot primitive; it force-renders first.** → CAP
`seq_core.c:1306`: sets `touched_ms=0; dirty=1; RenderTrack()` *then* memcpys `OutputActive` (`:1312-1317`)
— required because sweep regime only renders a ~4-step window. Any new capture/bounce path MUST replicate
force-render-first or read `seq_par_layer_value` directly.

**B4. SEQ_PAR_Get reads the OUTPUT MIRROR (stale after a source write).** → SELF, CAP
`SEQ_PAR_Get` returns `SEQ_PAR_OutputActive(track)[ix]` (`seq_par.c:287`); `SEQ_PAR_Set` writes the source
`seq_par_layer_value` (`:259`). Write-then-read via the public API is stale until next render.
(`SEQ_CORE_BakeForceScale` is **deleted** — `seq_core.c:1395`; the live precedent is `CaptureTrackOutput`.)

**B5. ext-CC persistence range 0x80-0x9F is already widened/tagged; 0x9B-0x9F is free for self-bus bits.** → SELF, SET
V3/V4 tags persist 32 ext CCs (`seq_file_b.h:48-50`); GRIP ships at 0x9A; 0x9B-0x9F unused, persists with
no format surgery. Extending *past* 0x9F bumps the fixed 806 B stride and migrates every old pattern — avoid.

**B6. Relocation lever: ~40 KB of main-RAM CPU-only buffers are CCM-eligible if CCM tightens.** → RNG, TRIG
`ucHeap` 20 KB + `seq_par_layer_value` 16 KB (`AHB_SECTION` = no-op on STM32F4, lands in main) +
`seq_trg_layer_value` 4 KB. The `AHB_SECTION`→`CCM_SECTION` mechanism is already wired. (Doc §A5's "56 KB"
included a deleted 16 KB `seq_capture` ring — real figure ~40 KB.) Generator pool 64→32 reclaims ~5.6 KB
CCM, 64→16 ~8.4 KB — both measurement-gated, don't cut speculatively.

**B7. Capture-ring tap point: `SEQ_MIDI_IN_BusReceive`, capturing RAW NoteOn/Off (pre-push), is lossless and simpler.** → CAP
`seq_midi_in.c:699`, fully `MUTEX_MIDIIN`-held (`:716/742`). Storing raw events + replaying into a fresh
notestack is lossless. **Caveat:** the tap is post-port/channel/note-range filter (`:435,495,529`) —
per-track "from its own seat" capture, which the design explicitly wants; a true "record everything" would
need a tap upstream in `APP_MIDI_NotifyPackage` (the design does not want this).

**B8. Self-bus micro-timing can ride the existing per-step StepDelay par layer — no new plumbing.** → SELF
`t->bpm_tick_delay` is recomputed per step from `SEQ_PAR_StepDelayGet` (`seq_core.c:3185-3189`) — already
per-step and render-stack-visible. Only sub-step per-NOTE delay needs new plumbing.

---

## C. Watch-outs (lower priority)

- **C1. Processor stack order is a compile-time contract** (PITCH 0 → CHORD_MASK 1 → TENSION 2 → LIMIT 3,
  `seq_core.c:723-726`). A 5th processor needs `NUM_PROCESSOR_SLOTS++` (+96 B CCM, 16 tracks) or slot reuse.
  **Trap:** SLOT-index constants ≠ processor-ID enum numbering (`seq_core.h:255-259`). → TRIG, SELF
- **C2. Humanize / probability / random-gate / random-direction / echo all draw global RNG**
  (`seq_humanize.c:62,71,80`; `seq_core.c:2974,2983,3085,3755-3774,4141,4197`). All must fold into the
  per-track-RNG refactor for re-sim determinism. → RNG, CAP
- **C3. Pulled track keeps reading STALE bus/arp notestack until next bus note-on.** `LoadTrackFromSlot`
  never resets `bus_notestack`/arp hold stacks (`seq_core.c:1881-1952`). A self-bus notestack reader is
  stale-for-one-event after a pull. Reset on pull or document the lag. → SELF
- **C4. FatFs LFN disabled → 8.3 names, SET names ≤8 chars.** Reuse `SEQ_FILE_IsValidSessionName`
  (`seq_file.c:606`, rejects len>8) for SET dirs. → SET
- **C5. FILE_Copy is non-atomic + no FILE_Rename wrapper; f_rename REFUSES on dst-exists (FR_EXIST).**
  Build the trivial remove-then-rename wrapper; start each SetSave by removing stale `_TMP_`, treat `_TMP_`
  on boot as discard. Worst case is a clean-fail, not data loss. → SET
- **C6. PHRASE bank (MBSEQ_PH.V4) and AN sentinel are excluded from SET copy** (§10(a)); phrases re-inventory
  via `ProbePhrasesOnLoad` (`seq_pattern.c:675`). No isolation boundary for phrases — intentional. → SET
- **C7. AUTOTEST A1-A3 are writable user slots; PATTERN-hold capture can overwrite them.** SET/capture HIL
  pins must use scratch sets, never touch A1-A3 (`tests/conftest.py:29-55`; `board.py:1371`). → SET, CAP
- **C8. Drum capacity caps:** 4 par-layers per drum (`seq_layer.c:45`, OOB past `[4]`); `layer_muted` u16
  is an instrument-mask on drums but a layer-mask on normal (`seq_layer.c:397`). Self-bus/trigger readers
  must respect both axes (worst case wrong-value, not OOB — `SEQ_PAR_Get` bounds-checks). → SELF, TRIG
- **C9. OOB trg-assignment index = "no escape", not garbage** (`seq_core.c:698`). New trg-reading trigger-gens
  must bounds-check resolved indices against `SEQ_TRG_MAX_BYTES` and use the safe default. → TRIG
- **C10. Sweep-window + trg-step8 wrap truncate the lookahead** (`seq_core.c:909-914,935-937`) — harmless for
  the 4 current per-step processors; a real discontinuity only for a future window/Turing trigger-gen at the
  pattern wrap. → TRIG
- **C11. SlotSync/RenderTouched defer rendering while playing** (`seq_core.c:367-370`) — self-route effect is
  one render-cycle latent, no recursion. `RenderTracks` force-re-renders every tick for chord/tension/
  live-pitch tracks regardless of touch (`:1971-1973`) — compounds in tight re-sim loops, within live budget. → SELF, CAP
- **C12. RAM for the new features is cheap; the constraint is CCM headroom.** Per-track seed 64 B, self-bus
  fields <100 B, capture seed-frame ring ~320-500 B for K=16 — all trivial and should sit in **main SRAM**
  (the robotize ring's home), not the scarce ~9 KB CCM. Full Part-II worst-case (~88 KB) requires the
  relocation lever (B6), not feature cuts. MSP peak 592 B — not a constraint. → all

---

## Per-build impact

| Build | Top findings |
|---|---|
| **PER-TRACK-RNG** | A1 (generators on global RNG, keystone) · A2 (CHORD_MASK gate → use tension hash) · C2 (humanize/prob/dir all global, scope) |
| **SELF-BUS** | A10 (target tiers: blacklist / defer / safe-per-step) · A3 (dirty-churn gate) · A8/A7 (fences + one-tick bus lag) |
| **SET BASELINE** | A4 (290 ms × N flash wall, off-live dispatch) · A5 (mutex ordering) · A12 (legacy-bank gen-state loss) |
| **UNIFIED UNDO/REDO** | A13 (re-engage arms no undo; define semantics) · C12 (~5 KB stores already budgeted, keep journal off CCM) |
| **RETROACTIVE CAPTURE** | A1+A2 (determinism prerequisite) · A9 (256-vs-8192 pool fidelity) · B1/B2/B3 (robotize ring + ref_step hook + CaptureTrackOutput) |
| **TRIGGER GENERATORS** | A11 (pool sharing/starvation + sizing decision) · A6/A8 (render-stack-native + fences) · C1/C9/C10 (slot contract, OOB trg, wrap) |

---

## Coverage gaps (check before building)

- **UI / display surface** — menu/LED/encoder layer unexamined: SET menu section, unified-UNDO gesture
  binding, trigger-gen + self-bus editing pages, capture/bounce UX gestures. (Manual UI button-pressing
  is known to perturb pull/sel-view statics.)
- **CV/AOUT + gate outputs** — findings cover MIDI emission only; `SEQ_CV_Update` interaction unverified.
- **Song/arrangement mode + transport edge cases** — song-mode jumps, loop points, slave-clock
  (synch_to_measure) interaction with capture-ring bar closure not traced end-to-end.
- **Concurrency beyond the named mutexes** — the +3 MIDI-hooks vs +4 core-tick task preemption (a shared
  capture ring needs IRQ-disable/critical discipline, not just `MUTEX_MIDIIN`) flagged, not exhaustively audited.
- **Worst-case timing under load NOT measured** — scheduler peak with arp+echo+robotize+self-bus fan-out;
  the PHRASE-RECALL writeback budget with a capture ring / self-bus added — estimates, measure on device.
- **No by-ear validation** — per project discipline, the musical GO/NO-GO gates are downstream of all the above.
