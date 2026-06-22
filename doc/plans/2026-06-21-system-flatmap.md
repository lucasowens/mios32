# System flat-map — the box as a whole (2026-06-21)

> **Why this exists.** We shifted strategy: stop validating fragments in isolation ("is this one function musical?" — unanswerable) and instead prove out the **whole system / flow** (sculpt → capture → travel → return → layer → land a set), then refine it into something **performable and fun**. This is the *paper pass*: the box laid flat so the structural walls (RAM/CPU, gesture collisions, the one primitive that breaks everything-return) are known **before** build cost is sunk. Scope = the box; Ableton is a fixed test-bed (a constant, not a variable). Every claim below is source-grounded against the HEAD elf (built 2026-06-20 21:03) and adversarially re-verified.

## The Flat-Map: the whole instrument on one page, before we play into it

This document lays the box flat on paper so the structural walls are known **before** build cost is sunk into playing. It is the deliberate shift away from validating fragments in isolation (which proved meaningless — you can't tell if one function is musical without making music with it) toward proving the **whole system / flow** first, then refining it into something **performable and fun**.

### The new validation grain: the SET, not the fragment

Until now the unit of by-ear validation was a workflow bundle. The flat-map raises the grain again: the thing we validate is the **whole bounded system running the north-star flow** —

> sculpt a state → capture it → travel/morph → return → layer → land a set

— against a **fixed** Ableton test-bed (a constant, not a variable; spec'd below). We no longer ask "is this function musical"; we ask "does the system hold together end-to-end as an instrument, and where does it not." The GO/NO-GO gate is at the set level.

### How to read the three maps

The map is one system seen through three overlays, each source-grounded against THIS fork at HEAD (every claim carries a `file:symbol` / `file:line`; anything knowable only on-device or by ear is called out as an open unknown, not asserted):

- **MAP A — Resource budget (the box's body).** What fits in RAM/CCM/CPU/flash. Headline correction: there is **no "everything-on" allocation spike** — every generative buffer is statically resident, so the elf already *is* the worst case. Both main RAM and CCM sit at ~9 KB free with **no relocation lever** (the design doc's "33 KB main / 40 KB CCM lever" is dead). The budget question is therefore entirely "what do the queued builds add."
- **MAP B — The control surface (the box's hands).** The live-performance gesture grammar laid flat: a handful of held-modifiers (SELECT, PATTERN, UTILITY, SELECT+BOOKMARK) and two invisible modal axes (`sel_view`, `FREEZE`) that silently re-define what the GP row, select row, and datawheel mean. The surface works but is currently playable only from **memory**.
- **MAP C — The signal-flow + dirty/writeback seam (the box's nervous system).** The render→emission pipeline and the one shared primitive — a global 4-bit `seq_pattern_dirty` mask — that **every return-family operation rides**. The canonical break (recall-freeze) lives here.

### Why overlay them

The walls that matter are the ones **no single map shows** — they appear only where two maps touch. The recall-freeze is the proof case: it is invisible to a budget audit alone, invisible to a gesture audit alone, and only becomes a structural verdict when you see Map C's shared mask intersect Map A's dead relocation lever (no RAM to buffer the writeback) and Map B's FREEZE mode (which silently changes its severity). The `cross_map_risks` below are the rest of those seams; the `emergent_refinement_queue` is the roadmap the *system* hands us — ranked by leverage for **performable and fun**, not by feature-completeness — replacing the old per-fragment feature ladder.

> **Verification status.** An adversarial pass re-checked every load-bearing claim against source/elf: **all confirmed** except one cosmetic overstatement — Map A's morph-buffer *breakdown dimensions* are approximate; the real resident morph footprint is **~7.7 KB** (incl. unlisted `phrase_morph_a/b`), not ~6.4 KB. Conclusion (resident, small, not a spike) unaffected. The two genuine unknowns — the **~290 ms/save** flash cost and the **all-16-tracks tick-prologue** wall-clock — are **on-device-only** (not source-visible); the queue points *at* them rather than asserting past them.

---

## System verdict

YES — it fits, and NO — it does not hold together at the two seams where the maps overlap. Bounded to the north-star flow with what is already built, the WHOLE SYSTEM is resident and runs: there is no "everything-on" allocation spike (Map A: every generative buffer is statically allocated and permanently resident, so the elf IS the worst case — 9KB main / 8.6KB CCM free is the steady state, not a future peak). The control surface (Map B) covers the entire flow with real gestures and real LEDs. The dirty/render seam (Map C) is correct in isolation at every individual call site. So nothing is missing and nothing is broken in a single map.

Where it structurally does NOT hold together is the OVERLAP:

1. The canonical break is real and uncontained: Map C's recall-WritebackAllDirty rides the SAME global 4-bit mask that Map C's generator auto-mutate dirties, and Map A confirms the only cure that doesn't lose wander (incremental save) touches the SD write path while the RAM-snapshot escape hatch is dead (~30KB needed vs 9KB free). So "travel with generators → return via phrase recall" — the literal center of the north-star flow — freezes the clock up to ~1.3s, and the margin system structurally cannot cover it (250ms cap, and recall uses the even-smaller FIXED 100ms fallback, not the measured margin). This is not a fragment bug; it is the system's defining flow hitting its defining SPOF.

2. The second structural break is that the build roadmap does not fit the box. The trigger-generator pool — a queued centerpiece (trigger gens DON'T EXIST today; the instrument is pitch-only) — is ~11.25KB and fits NEITHER 9KB region, with no relocation lever left. The system cannot grow in the direction the design wants without first being redesigned to share the existing 64-slot pool.

Everything else (SET layer, self-bus, capture-persistence, unified undo, melodic-init) fits cleanly. The honest bottom line: the instrument is PLAYABLE today but not yet PERFORMABLE — its center-of-flow gesture has a 1.3s freeze, its surface is memory-only (no labels for the invisible modes, inconsistent hold-polarity, no panic-UNDO), and its next intended growth step doesn't fit in RAM.

---

## Cross-map risk register

*The walls that only appear when the three maps are overlaid. These are the system breakages — none is visible at fragment grain.*

| # | Severity | Risk | Maps | So what (the move) |
|---|---|---|---|---|
| 1 | **WALL** | RECALL-FREEZE = the budget wall x the dirty SPOF, and FREEZE-the-mode partially gates it. Generator auto-mutate dirties every engaged group (Map C: write_loop_to_source -> DirtySetTrack, seq_generator.c:471); phrase recall's WritebackAllDirty (seq_pattern.c:474) then serializes up-to-4 x ~290ms flash saves into the recall window. Map A kills the only RAM escape (full snapshot ~30KB vs 9KB free; CCM also full, no relocation lever). VERIFIED NUANCE: seq_core_state.FREEZE 'continue's BEFORE the automutate dirty-window opens (seq_generator.c:703 vs :709), so groups under FREEZE stop re-dirtying via wander — engaging FREEZE before a recall is the de-facto live workaround, but it is invisible and undocumented as such. | A (budget) x B (FREEZE mode) x C (dirty mask) | The center of the north-star flow ('travel -> return') hitches up to 1.3s. The only standing cures are incremental-save (own bundle, touches seq_file_b.c PatternWrite) or the reverted DRIFT-gated writeback (phrase_drift already IS the second register, seq_pattern.c:249, just unused by writeback). Decide the by-ear question — should un-captured wander survive a recall — FIRST; it gates which cure is legal. Until then, surface FREEZE-before-recall as the documented live move. |
| 2 | **WALL** | The next intended build literally does not fit the box. Map A: trigger-generator pool (~11.25KB, mirroring the confirmed 0x2e00=11776B pool) > both ~9KB free regions, no relocation lever. Map C: trigger gens would be a NEW bypass-writer class (raw source mutation) that must hand-roll its own DirtySetTrack + its own save/restore-dirty dance for any offline drive — the third such uncoordinated dance after re-sim (seq_core.c:2004/2021) and tape (seq_core.c:2412/2475). | A (budget) x C (dirty/bypass-writer) | Trigger gens cannot be built as a twin pool. They must FOLD INTO the existing 64-slot pool (a capability/by-ear question: does sharing starve pitch gens?) AND inherit a shared offline-drive dirty discipline rather than reinventing a fourth save/restore dance. This reorders the roadmap: pool-sharing design is a prerequisite, not a detail. |
| 3 | **SERIOUS** | The all-16-sweeping CPU worst case (Map A) lands exactly on the travel/morph flow (Map C render stage) where the surface (Map B) invites it. chord_mask/tension/live-pitch force a full per-track re-render EVERY tick (memcpy 1024+256 + up-to-256-step inner loop, seq_core.c:2757,1335), bypassing sweep/quiet by design. The GRAVITY datawheel and held-chord gestures (Map B) are precisely what drives 16 tracks into this state live, and the tick prologue competes with the same TASK_MIDI emission timing that Map C's interrupts-on recall fix was built to protect. | A (CPU) x B (GRAVITY/chord gestures) x C (render stage) | Never measured. Before leaning on tension/chord across many tracks live, measure the tick prologue with 16 force-dirty tracks at fast BPM (per-track accumulation; stopwatch saturates at 65.5ms). If it starves emission, the cap is 'how many simultaneous live-input processors' — a performance ceiling that should be known before a set is built around sweeping them all. |
| 4 | **SERIOUS** | Hold-polarity reversal sits on the return/land flow with no UNDO net. Map B: PHRASE-waypoint hold = CREATE (capture) but SELECT+BOOKMARK hold = DESTROY (REVERT discards the live jam), same 1000ms. Map A/C: there is NO physical UNDO (BUTTON_UNDO 0 0) and REVERT's SnapshotRead passes writeback_dirty_first=0 (seq_pattern.c:570) — it deliberately does NOT save first. So a mis-aimed REVERT-hold annihilates the live jam with no recovery gesture on the panel. | B (gesture polarity) x C (REVERT no-pre-writeback) | At the highest-stakes moment of a set (keep this / go back), muscle memory trained on 'hold=keep' triggers 'hold=destroy' on the irreversible gesture. Either flip REVERT to require a confirm/give it a pre-writeback escape, or move it off the same hold-grammar. This is a performability blocker, not a fragment polish item. |
| 5 | **SERIOUS** | FREEZE is a single invisible global that changes BOTH a musical result (Map B: recall = regenerating posture vs static tape) AND, per the verified nuance above, the recall-freeze SEVERITY (Map C: frozen groups don't re-dirty). The player must track one bit that silently re-defines what recall sounds like AND how long it freezes — with feedback only at toggle-time (METRONOME LED), never at recall-time. | B (hidden mode) x C (dirty/freeze) | The same recall gesture has two different sounds AND two different freeze durations depending on a bit set bars ago. Surface FREEZE state AT the recall moment (e.g. recall-row LED tint), and document FREEZE-before-recall as both the 'static tape' move and the 'avoid the 1.3s hitch' move — it is one lever doing double duty and the player can't see it. |
| 6 | **SERIOUS** | Mirror-gesture destructive collision on shared hardware with a partial undo net. Map B: PATTERN-held+GP9-16 = capture-OUT to slot; track-held+GP9-16 (TRACKS view) = pull-IN from slot — both bar-aligned, destructive, separated only by which modifier is down. Map C: the pull's one-deep undo has a known arbitration gap (post-pull edits lose to the unconsumed victim, seq_ui.c:1662) and capture-to-slot span has NO undo at all. | B (mirror gestures) x C (undo arbitration gap) | A held-modifier slip swaps load-IN for overwrite-OUT on adjacent top-row numbers, and the recovery path is incomplete on both sides. The queued unified UNDO/REDO (Map A: fits, may even free main RAM) is the highest-leverage safety fix here — it should cover the capture-span gap, not just unify the existing one-deeps. |
| 7 | **WATCH** | Non-atomic snapshot writes (Map C: SnapshotWrite loops 4 group records, no temp+rename, seq_pattern.c:334) underlie CHECKPOINT and phrase-capture — the durable-return spine the whole 'capture it' flow depends on. The SET layer (Map A/C) was deliberately designed to AVOID the mask via file-copy + proven dir-atomic rename, but CHECKPOINT/capture still write non-atomically. | A (queued SET) x C (partial-write) | A flaky-SD or power-dip mid-capture leaves a mixed snapshot that only surfaces as a negative return. The SET layer's atomic temp+rename recipe should be back-ported to SnapshotWrite — same primitive, already proven, near-zero RAM (Map A: ~0). |

---

## The emergent refinement queue — the roadmap the *system* hands us

*Ranked for **performable and fun**, not feature-completeness. This replaces the old feature ladder (§8). Items 1–5 are play-readiness; 6–7 are capability/reliability and are gated on the play-readiness items.*

### #1 — DECIDE the recall-landing semantics (by ear): may un-captured generator wander be abandoned on phrase recall? Then ship the matching freeze cure — DRIFT-gated writeback if yes (tiny: swap the mask read in SnapshotRead's pre-writeback to phrase_drift, which already exists), incremental-save if no (own bundle in seq_file_b.c).
**Cost:** Decision: one focused by-ear session (toggle wander-vs-no-wander across recalls). Cheap cure: ~1 line + verify. Expensive cure: own bundle, invasive, needs on-device re-measure of the ~290ms/save figure first.

This is the single highest-leverage move for PERFORMABLE: it removes the up-to-1.3s freeze from the literal center of the north-star flow (travel->return). The flat-map proves the RAM escape hatch is dead, so it MUST be solved at the dirty-semantics or SD-write layer, not by buffering. It is a decision, not a build — and it unblocks both candidate cures. phrase_drift is already the second register the cheap cure needs.

### #2 — Make the invisible modes visible AT the moment they change meaning: a persistent sel_view label (not just while a view button is held), and a FREEZE indication on the recall row itself (not only the METRONOME LED). Document FREEZE-before-recall as the dual-purpose live move (static tape + dodges the freeze).
**Cost:** Small: LCD label for latched sel_view + a recall-row LED tint gated on seq_core_state.FREEZE. Mostly seq_ui.c LED/label code; no new buffers.

The surface is currently playable only from memory (Map B). sel_view silently re-defines the select row, the whole GP row, and the datawheel with no live label once latched; FREEZE silently re-defines what recall sounds like AND how long it hitches. For a live instrument, 'which grammar am I in' must be glanceable. This is pure performability and needs zero RAM/CPU — it is UI feedback only.

### #3 — Unified UNDO/REDO as the always-available panic net — and extend it to cover the capture-to-slot span (which has NO undo) and close the post-pull arbitration gap (seq_ui.c:1662).
**Cost:** Medium build (consolidate 5 one-deeps + add a redo ply, ~+1.3KB main worst case, possibly net-negative). Plus assign a panic gesture and an LED (no physical UNDO exists).

There is no physical UNDO button and REVERT-hold can irreversibly destroy the live jam (the polarity-reversal cross-risk). A uniform recover gesture is what lets a player take risks live without fear — the difference between 'fun' and 'tense'. Map A shows it FITS and may even free main RAM by collapsing the 5 bespoke one-deeps. Highest safety-per-byte item on the board.

### #4 — Resolve hold-polarity: make REVERT non-destructive-by-default (pre-writeback escape or a confirm), OR move REVERT off the 1000ms-hold grammar so 'hold=keep' (PHRASE) and 'hold=destroy' (anchor) never share a feel.
**Cost:** Small if solved via a pre-writeback escape (reuse WritebackAllDirty) or a confirm tap; design-by-ear which feels right live.

Same 1000ms hold means create on one gesture and destroy-with-no-undo on another. This is the one grammar inconsistency that can lose a whole live jam in a single mis-timed press. Once #3 (undo net) lands, REVERT becomes recoverable and this drops in severity — which is why it sits just under undo.

### #5 — Measure the all-16 live-input render worst case (chord_mask/tension/live-pitch force-dirty every tick) on device, per-track-accumulated at fast BPM, before building a set that sweeps GRAVITY/held-chord across many tracks.
**Cost:** On-device measurement session (no code). Stopwatch saturates at 65.5ms so accumulate per-track. Mitigation, if needed, is a later bundle (bound live-input re-render to a touched window).

This is the one CPU wall the design never measured and it lands exactly on the travel/morph flow the instrument is FOR. If 16 force-dirty tracks starve TASK_MIDI emission, that defines a hard 'how many live processors at once' performance ceiling — better known before a set is composed around it than discovered on stage.

### #6 — Redesign the trigger-generator pool to FOLD INTO the existing 64-slot pool (with a shared offline-drive dirty discipline), instead of a twin pool.
**Cost:** Design-heavy: pool-sharing scheme + by-ear check that shared slots don't starve pitch gens; then the generator-type build itself. Must reuse the existing DirtySetTrack/save-restore discipline, not reinvent it.

Trigger gens are a real capability gap (instrument is pitch-only today) but Map A proves an 11.25KB twin pool fits NEITHER region with no lever. The system says: share the pool or don't build it. This is the right next CAPABILITY step but it is gated on #1/#3 landing first (it would otherwise be a fourth uncoordinated bypass-writer riding the fragile mask). Capability, not performability — hence below the play-readiness items.

### #7 — Back-port the SET-layer atomic temp+rename to SnapshotWrite (CHECKPOINT + phrase capture), and ship the SET durable-baseline layer.
**Cost:** Small-to-medium: reuse the proven dir-atomic rename; SET layer itself is ~0 RAM (disk + reused file buffers).

The 'capture it' flow's durable spine writes 4 records non-atomically — a power dip mid-capture corrupts a snapshot silently. SET (Map A: ~0 RAM, file-copy, mask-untouched) already proves the atomic recipe; applying it to SnapshotWrite hardens the capture flow for near-zero cost. Reliability that protects everything captured upstream.

---

## MAP A — Everything-On Resource Budget

*Ground truth: `apps/sequencers/midibox_seq_v4/project_build/project.elf`, built 2026-06-20 21:03. STM32F407VG: 128 KB main SRAM @ `0x20000000`, 64 KB CCM @ `0x10000000`, 960 KB user FLASH @ `0x08010000` (`etc/ld/STM32F4xx/STM32F407VG.ld:9-15`).*

### 0. The headline correction

The biggest design-doc error is confirmed dead: the doc's "~33 KB free main + ~40 KB CCM-relocation lever" is **stale and the lever cannot fire**. Both regions sit at ~9 KB free *today*, with all generative buffers (capture ring/tape/snap, morph, undo) **statically allocated and permanently resident** — there is no allocate-on-engage. So "everything-on" is not a future spike; it is the steady state the elf already describes. The budget question is therefore entirely about **what the queued builds add**, not about whether current features fit.

### 1. Per-component RAM breakdown

**CRITICAL allocation fact:** par/trg layers and their output mirrors are split across regions by section attribute. Source layers are `AHB_SECTION` (main RAM, `seq_par.c:36`, `seq_trg.c:33`); output mirrors are `CCM_SECTION` (`seq_par.c:45`, `seq_trg.c:40`). All are dimensioned `[SEQ_CORE_NUM_TRACKS=16]` unconditionally (`seq_core.h:21-23`), `SEQ_PAR_MAX_BYTES=1024`, `SEQ_TRG_MAX_BYTES=256` (`seq_core.h:416,419`).

#### CCM region (0x10000000) — 55.4 KB used / **8.6 KB free** — CPU-only, no DMA

| Symbol | Bytes | What it is | Region-locked? |
|---|---:|---|---|
| `seq_par_output_value` | 32768 | par output mirror `[16][2][1024]` double-buffer (`seq_par.c:45`) | CCM-resident; CPU-only, **could move to main but main is also full** |
| `pool` | 11776 | generator pool `[64]` × sizeof(seq_generator_t)=184 (`seq_generator.c:46`, `seq_generator.h:63,144`) | CCM by design |
| `seq_trg_output_value` | 8192 | trg output mirror `[16][2][256]` (`seq_trg.c:40`) | CCM |
| `track_undo` | 2336 | per-track undo slots (`seq_core.c`) | CCM |
| `undo_slot` | 1026 | undo bookkeeping | CCM |
| `seq_processor_stack` | 384 | `[16][4]` processor slots (`seq_core.c:162`) | CCM |
| `pool_index` | 256 | `[16][16]` gen slot map (`seq_generator.c:54`) | CCM |
| **CCM total** | **56744** | matches `.bss_cmm` exactly (sum of above = 56738+pad) | |

#### Main RAM region (0x20000000) — 118.9 KB used / **9.06 KB free** — the 9 KB is shared with the MSP stack growing down from `_estack=0x20020000`

| Symbol | Bytes | What it is | CCM-eligible? |
|---|---:|---|---|
| `ucHeap` | 20480 | FreeRTOS heap (task stacks, dynamic alloc) | No — task stacks, keep main |
| `seq_par_layer_value` | 16384 | par SOURCE layers `[16][1024]`, `AHB_SECTION` (`seq_par.c:36`) | **No — AHB/DMA region, must stay main** |
| `seq_core_cap_ring` | 12784 | capture frame ring `[17]`×752 (`seq_core.c:215`; 17 bars = live + 16 grabbable, `:186`) | Yes (CPU-only) — but CCM full |
| `seq_core_cap_tape` | 6144 | live-tape event ring `[768]` note-ons (`seq_core.c:248,250`) | Yes — CCM full |
| `seq_core_cap_snap` | 5660 | capture working snapshot | Yes |
| `slottrk_par_snap` | 4096 | `[4][1024]` recombine slot par snapshot (`seq_core.c:2342`) | Yes |
| `seq_trg_layer_value` | 4096 | trg SOURCE layers `[16][256]` (`seq_trg.c:33`) | **No — main, no AHB tag but pairs with par** |
| `seq_core_trk` | 3520 | per-track runtime state `[16]` (`seq_core.c`) | partial |
| `seq_cc_trk` | 3456 | per-track CC config `[16]` | partial |
| `slottrk_gen_snap` | 2944 | `[4][4]` recombine gen snapshot (`seq_core.c:2351`) | Yes |
| `USB_OTG_dev` | 2708 | **USB OTG core — DMA** | **No — DMA, must stay main** |
| `uip_buf` | 1520 | **Ethernet/uIP buffer — DMA** | **No — DMA** |
| `aout_channel` | 1408 | **analog out (CV) DMA state** | **No — DMA** |
| morph buffers (sum) | ~6400 | `phrase_morph_*` per-group `[4]` lerp buffers (`seq_pattern.c:121-141`): vel_a/b+note_a/b+gate_thresh @1024 ea, main_a/b @512, gate_a/b @128 | Yes |
| undo/copypaste (par/trg/buffer) | ~3300 | `undo_par_layer`(1024)+`copypaste_par_layer`(1024)+trg/buf @256 ea | Yes |
| smaller (cc_last, song_steps, groove, midi_tracks, notestacks, file bufs, etc.) | ~8000 | mixed | mixed |
| **Main total** | **~120384** | matches `.bss`=120384 + `.data`=1152 | |

### 2. The "everything-on" model — does it fit TODAY? **YES, with no spike.**

Maximal-but-realistic live case: 16 tracks, 3-frozen-+-1-alive, generators engaged in the pool, capture ring + tape live, a morph armed, phrases + anchor resident.

**Verdict: it already fits, because none of these buffers are conditional.** There is no steady-state-vs-worst-case allocation gap — the elf *is* the worst case:

- **"render output only allocates for processor-bearing tracks" — REFUTED.** `seq_par_output_value[16][2][1024]` and `seq_trg_output_value[16][2][256]` are unconditional static arrays for all 16 tracks (`seq_par.c:45`, `seq_trg.c:40`). Engaging a processor on track 15 vs leaving it bare costs **zero additional RAM** — the double-buffer is already there. The *only* thing that scales with engagement is CPU (section 4), not RAM.
- **"morph buffers ~6.7 KB" — CONFIRMED (~6.4 KB).** Summed from `seq_pattern.c:121-141`: 6400 B, always resident regardless of whether a morph is armed (arm only fills them). Doc claim is accurate.
- Generator engagement draws from the fixed 64-slot `pool` (11776 B); engaging more generators consumes slots, not new memory, until the pool is exhausted (`alloc_slot()` scans `in_use`, `seq_generator.c:279-283`). Pool exhaustion is a **capability** ceiling (capture refuses with `seq_core_cap_ring_overflow` when a bar has >4 gens, `seq_core.c:431`), not a RAM fault.

So the honest steady-state worst case is the **9 KB / 9 KB free already measured** — the system runs there permanently. The risk is not "does everything-on fit" (it does) but "how close to the MSP-stack collision is the 9 KB main free region," which is an on-device measurement (open unknown).

### 3. Queued-build running balance — be a banker

Two scarce accounts: **~9.06 KB main / ~8.6 KB CCM.** Every queued item must name its region.

| Queued build | Claimed cost | Region it lands in | Fits? | Banker's note |
|---|---|---|---|---|
| **SET layer** (file-copy durable baseline) | ~0 RAM | main (file I/O reuses `file_read`/`file_write` 548 B ea, already allocated) | **YES** | File-copy is disk, not RAM. Genuinely ~0. Safe. |
| **Unified UNDO/REDO** (unify 5 bespoke one-deeps + add redo) | "reuses ~5 KB" | main | **CONDITIONAL** | Today undo is scattered: `track_undo`(2336,CCM)+`undo_slot`(1026,CCM)+`undo_par_layer`(1024)+`copypaste_par_layer`(1024)+`undo_trg_layer`/`undo_buffer`(256 ea, main). Unifying *reuses* these — net could be **negative** (frees main). Adding a REDO ply doubles whichever buffer holds the deepest snapshot. If redo = one more 1024+256 par/trg ply → +~1.3 KB main. **Fits, and may pay for itself.** |
| **Trigger-generator pool** | "11.25 KB full / won't fit CCM" | wants CCM (mirror `pool`) | **NO (CCM)** | CCM has 8.6 KB free; an 11.25 KB twin pool **does not fit CCM**. Confirmed wall. Options: (a) shrink to ~6 KB by halving slots and put in CCM, (b) put in the 9 KB main free — also doesn't fit at 11.25 KB. **This is the budget wall.** Must be redesigned smaller or share the existing `pool`. |
| **Self-bus** (self-modulation CC layer) | <100 B | main (a few per-track route fields in `seq_cc_trk`/`seq_core_trk`) | **YES** | Routes through existing `SEQ_CC_Set`; per-track route byte ×16 = trivial. Safe in either region. |
| **Capture-ring persistence** (seed-frame to disk) | not yet sized | main (RAM) + disk | **WATCH** | The RAM ring already exists (12784 B). Persistence is disk serialization — adds file-buffer transients (reuse `tmp_buffer` 512 B) not standing RAM. **Likely free in RAM.** |
| **True-melodic capture init** (1-voice Note init) | small | main | **YES** | Logic/format change, not a new buffer. Harness gap (only makes drum-layout), not a RAM gap. |

**Banker's verdict:** SET, self-bus, melodic-init, capture-persistence, and unified-undo all fit (undo may even free main). **The trigger-generator pool at 11.25 KB is the one item that does NOT fit either 9 KB region and must be resized or fold into the existing 64-slot pool before it can be built.**

### 4. CPU / tick wall — the unmeasured risk

Tick path: `SEQ_CORE_Tick` → `SEQ_CORE_RenderTracks()` prologue every tick (`seq_core.c:3315,2741`). For each of 16 tracks it scans 4 processor slots (`SEQ_CORE_NUM_PROCESSOR_SLOTS=4`, `seq_core.h:27`); **any track carrying an enabled CHORD_MASK or TENSION slot, or a PITCH slot with live input, is force-marked dirty every single tick** (`seq_core.c:2752-2762`). A dirty track triggers `SEQ_CORE_RenderTrack` (`:1318`) = full `memcpy(1024)` + `memcpy(256)` + a render-range pass per enabled processor over up to `num_p_steps` (≤256) steps (`:1335-1362`).

- **The doc's "processors run only when engaged (single-digit us)" is HALF TRUE.** Static transpose / FTS render only on edit (sweep regime, `SEQ_RENDER_SWEEP_MS=50`, `:388`). But **chord_mask + tension + live-pitch re-render EVERY TICK by design** because they depend on live held-chord / GRAVITY-knob inputs (`:2745-2752`). This is the all-16-sweeping worst case the doc never measured (A4 #5).
- **Reasoned worst case:** 16 tracks each carrying an enabled chord_mask or tension processor over 256 steps = 16 × (1280 B of memcpy + 256-step processor inner loop) **every tick**. At fast tempo the tick prologue is the dominant cost. This is a genuine, structural worst case — not hypothetical, since chord_mask is the default ChordMask playmode path (`SEQ_CORE_ChordMaskSlotSync`, `:1383`).
- **Mitigation already in code:** sweep/quiet detection bounds *touched* tracks to a window, but the chord/tension/live-pitch force-dirty bypasses it. So the optimization the doc points to ("Phase D will optimize via sweep/quiet") does NOT cover the live-input processors — they are intentionally exempt.

**Needs on-device measurement** (open unknown): tick prologue wall-clock with 16 chord_mask tracks at 256 steps, fast BPM. The stopwatch saturates at 65.5 ms (per MEMORY), so measure by per-track accumulation.

### 5. Flash budget + CMD byte ledger

**Flash:** payload landing in the 960 KB FLASH region (`0x08010000`): `.text`=377536 + `.rodata`=85168 + `.data`=1152 + `.ARM.exidx`=8 ≈ **463.9 KB used / ~496 KB free** (`size -A`). `.mios32_bsl` (16 KB) and `.isr_vector` (392 B) live in separate dedicated sectors and don't draw on the 960 KB. **Flash is not a constraint** — roughly half-empty; `make TESTCTRL=0` reclaims ~7.7 KB (MEMORY) for gig builds.

**testctrl CMD_ byte ledger** (authoritative, grepped from `core/`). USED: `0x01, 0x10, 0x11, 0x20, 0x30-0x32, 0x40, 0x49-0x4f, 0x50-0x7f` (the `0x49-0x7f` block is **completely packed, zero holes** — confirmed). FREE ranges:

| Free range | Count |
|---|---:|
| `0x00` | 1 |
| `0x02-0x0f` | 14 |
| `0x12-0x1f` | 14 |
| `0x21-0x2f` | 15 |
| `0x33-0x3f` | 13 |
| `0x41-0x48` | 8 |
| **Total free** | **65** |

The user's reported holes are confirmed exactly. **The natural next-command home is `0x41-0x48` (8 bytes)** — it sits adjacent to the live `0x49-0x4f` test-control cluster, keeping new capture/transport verbs contiguous with their relatives. The low ranges (`0x02-0x3f`, 57 bytes) are also free but sit among the legacy fixed-meaning bytes (PING/BUTTON/ENCODER/LCD/RESET/PAGE), so reserve those for structurally-distinct command classes.

**MAP A — system risks:**

- *[wall]* **Both main RAM and CCM sit at ~9 KB free with NO relocation lever (CCM is already full of the par output mirror + gen pool; the par SOURCE layer that would move to CCM is AHB/DMA-locked). The design doc's relocation escape hatch does not exist.** — bites at: layer/land — any new standing buffer added during the build-out (esp. trigger-gen pool) hits the wall; no headroom to grow the capture ring or add a second pool
- *[wall]* **Trigger-generator pool (11.25 KB claimed) fits neither free region. Must be resized or share the existing 64-slot pool before it is buildable.** — bites at: sculpt — building trigger generators (don't exist today, pitch-only) is blocked until the pool is redesigned smaller
- *[serious]* **chord_mask/tension/live-pitch force full per-track re-render every tick (1280 B memcpy + up to 256-step inner loop), bypassing the sweep/quiet optimization. 16 such tracks at fast BPM is a never-measured tick-prologue cost.** — bites at: travel/morph — sweeping GRAVITY or held-chord across all 16 tracks live is exactly the everything-on performance case; could starve TASK_MIDI emission timing
- *[watch]* **The 9 KB main free region is shared with the MSP stack growing down from _estack; deep call stacks (SD save under recall, re-sim capture) could collide with the top of .bss in the worst case.** — bites at: capture/return — re-sim and recall do deep work; stack high-water under those paths is unmeasured
- *[watch]* **Capture refuses (overflow flag) when a bar has more generators than the 4-slot ring frame can hold — a silent capability ceiling, not a fault.** — bites at: capture — grabbing a busy generative bar with >4 engaged gens on the recording track returns incomplete/refused

---

## MAP B — The Control Surface Laid Flat

Scope: live-performance gestures on the **midiphy V4+ LH panel** (`hwcfg/midiphy_lh/MBSEQ_HW.V4`). All dispatch evidence is `core/seq_ui.c` unless noted. The panel runs with `BUTTON_BEH_SIMPLIFIED_ANTILOG_FRONTPANEL 0` (`MBSEQ_HW.V4:641`) and `dout_gp_mapping==3` (`selbuttons_available`), so the **16-button select row is live** and is the workhorse of the fork's gestures.

Hardware reality checks (from `MBSEQ_HW.V4`):
- **No physical UNDO button**: `BUTTON_UNDO 0 0` (line 511, `0 0` = unassigned). The pull's undo is therefore SELECT+CLEAR.
- **METRONOME button is repurposed to FREEZE**, latching: `BUTTON_METRONOME M3C 1` (402), `BUTTON_BEH_METRONOME 1` = toggle (610), with a real LED `LED_METRONOME M3C 1` (335).
- BUTTON_PATTERN M8B 5 (435), BUTTON_SONG M8B 6 (436), BUTTON_PHRASE M7C 0 (437), BUTTON_UTILITY M8B 7 (507), BUTTON_SELECT M8A 7 (418), BUTTON_BOOKMARK M1C 0 (514), behaviors `BUTTON_BEH_BOOKMARK 0` (618, momentary), `BUTTON_BEH_STEP_VIEW 0` (619, momentary).
- Encoders: 1 datawheel + 16 GP encoders + BPM (`ENC_DATAWHEEL`, `ENC_GP1..GP8/...`, `MBSEQ_HW.V4:652+`). There is **no distinct "four window encoders" cluster** in software — encoder 0 = datawheel, 1–16 = per-GP-column encoders, 17 = BPM (`SEQ_UI_Encoder_Handler`, `seq_ui.c:3414`, `:3444`, `:3512`). The "windows" are the 4 LCD quadrants navigated by GP encoders/cursor.

---

### 1. Modifier-ownership table

A "modifier" here = a button whose **held** state changes what *another* press means. The fork stores each as a static or `seq_ui_button_state` bit and gates the GP / select-row handlers on it.

| Modifier (held) | Combo while held | What it does | Dispatch site |
|---|---|---|---|
| **PATTERN** (`PATTERN_PRESSED`, set `seq_ui.c:2005`) | GP1–8 | stash destination **group** for a static capture-to-slot | `seq_ui.c:788` |
| | GP9–16 | **commit** capture of visible track → slot pattern (same-group=save-only, cross-group=load live + resync) | `seq_ui.c:795`, calls `SEQ_CORE_CaptureToSlotTrack` `:829` |
| | select-row btn | stash destination **track** within the slot | `SEQ_UI_Button_DirectTrack` `:2380` |
| | (bare tap, no sub-gesture) | navigate to PATTERN page | `seq_ui.c:2029` |
| **UTILITY** (`capture_util_held`, set `seq_ui.c:1331`) | GP-n (n=1..16) | **commit** retroactive CAPTURE: grab last n bars of the ring → dst track (transport STOPPED→re-sim, PLAYING→live tape) | `seq_ui.c:862`, calls `SEQ_CORE_CaptureRingTrack`/`SEQ_CORE_CaptureSpan` |
| | select-row btn | stash CAPTURE **dst track**; visible track must NOT change (a switch invalidates the ring) | `seq_ui.c:2397` |
| | (bare tap <500ms, no sub) | navigate to UTILITY page | `CAPTURE_UTIL_TAP_MS` `:1321`, `:1350` |
| **SELECT** (`SELECT_PRESSED`, set `seq_ui.c:1879`) | **+CLEAR** | track-undo (pull's "one gesture back"); never falls through to destructive clear | `SEQ_UI_Button_Clear` `:1555` |
| | **+BOOKMARK** (tap) | CHECKPOINT — bless all 4 groups to anchor | `SEQ_UI_Button_Bookmark` arm `:1785`, fire `:1803` |
| | **+BOOKMARK** (hold ≥1000ms) | **REVERT** — discard live jam, restore blessed organism | `seq_ui.c:1792`, `ANCHOR_REVERT_HOLD_MS` `:1770` |
| | **+ waypoint** (PHRASE view) | ARM posture-morph toward that phrase (re-tap same = disarm) | `SEQ_UI_Button_DirectTrack` `:2540` |
| | **+GP1** (PITCHGEN page only) | DISENGAGE generator (page-scoped, NOT global) | `seq_ui_trkpitchgen.c:184` |
| | **+ (many in-page)** | stock "take changes immediately / section-select" modifier across PATTERN/MUTE/SONG/STEPSEL/ROBOTIZE/JAM pages | e.g. `seq_ui_pattern.c:172`, `seq_ui_mute.c:121`, `seq_ui_stepsel.c:71`, `seq_ui_song.c:209`, `seq_ui_robomold.c:58` |
| **MENU** (`MENU_PRESSED`) | GP1–16 | select shortcut page | `seq_ui.c:911` |
| | + SELECT | becomes BOOKMARK | `SEQ_UI_Button_Select` `:1875` |
| | + EXIT | becomes FOLLOW | `SEQ_UI_Button_Exit` `:1891` |
| | + METRONOME/FREEZE | becomes ExtRestart | `SEQ_UI_Button_Freeze` `:1242` |
| **BOOKMARK** (`BOOKMARK`, momentary) | (alone, simplified-panel off) | latches sel_view=BOOKMARKS; select row = bookmark recall | `:1813`, `:2469` |
| **A held select-row track btn** (`pull_held_track`, armed `seq_ui.c:2429`) | another select-row btn | pick pull SOURCE column | `:2437` |
| | GP1–8 (TRACKS view) | stash pull source pattern letter | `seq_ui.c:875` |
| | GP9–16 (TRACKS view) | **commit** pull: stored section → held track, bar-aligned, arms track-undo | `seq_ui.c:880`, calls `SEQ_CORE_LoadTrackFromSlot` `:891` |

**Confirmed:** SELECT alone fans out to (at least) 6 unrelated held-combos: +CLEAR (track-undo), +BOOKMARK tap (CHECKPOINT), +BOOKMARK hold (REVERT), +waypoint (morph-arm), +GP1-in-pitchgen (gen DISENGAGE), and the pervasive in-page "section/immediate" modifier. The review's "~5" is an undercount.

---

### 2. View-scoping (gestures whose meaning is set by `seq_ui_sel_view` + the other hand)

`seq_ui_sel_view` (`seq_ui.c:118`) is the single most meaning-bending state. The **select row** and the **GP row + datawheel** are entirely re-interpreted by it. Set by holding STEP/PAR/TRG/INS/MUTE/PHRASE/BOOKMARK (each does `prev=sel_view; sel_view=X; TAKE_OVER_SEL_VIEW=1` on press, e.g. PHRASE `:2073`), or **latched** if a GP/select press happens during the hold (`TAKE_OVER_SEL_VIEW` cleared at `:777`/`:2385`, so on release `sel_view` is NOT restored — `:2077`). SONG and PHRASE *buttons* both force `sel_view=PHRASE` (`SONG :2056`, `PHRASE :2074`).

Select-row dispatch by view (`SEQ_UI_Button_DirectTrack`, switch `:2468`):

| sel_view | select-row press means | evidence |
|---|---|---|
| TRACKS | radio/toggle track select **+ arms the pull gesture** | `:2475`, pull arm `:2426` |
| PHRASE | tap=RECALL phrase / hold≥1s=CAPTURE / SELECT+tap=morph-arm | `:2522`–`:2604` |
| MUTE | toggle track or layer mute | `:2497` |
| STEPS / PAR / TRG / INS / BOOKMARKS | delegate to that page's button handler | `:2469`–`:2495` |

GP-row / datawheel re-interpretation (priority-ordered intercepts in `SEQ_UI_Button_GP` and `SEQ_UI_Encoder_Handler`):
- PHRASE view + morph armed + on `morph_armed_page`: GP row = 16-seg morph position bar (`:903`), datawheel = fine morph throw (`:3474`).
- PHRASE view, no morph: datawheel = SWITCH-QUANTIZE grid (`:3486`).
- phrase_name_edit active: GP row + encoders = keypad (`:769`, `:3470`).

**Press disambiguated only by view + the other hand** (the phantom-pull lineage): a top-row GP press fires `LoadTrackFromSlot` **only if** `pull_held_track != 0xff` **AND** `sel_view==SEQ_UI_SEL_VIEW_TRACKS` (`:875`, `:880`). The TRACKS-view gate exists *because* in PHRASE view the same select-button-held + GP-press would otherwise fire a phantom pull during a phrase capture-hold (comment `:2416`–`:2422`). So "select-button held + top-row GP" means **pull** in TRACKS but **nothing** (or capture-hold) in PHRASE — pure view+second-hand disambiguation.

---

### 3. Tap-vs-hold polarity table

| Gesture | Tap does | Hold does | Threshold | Polarity verdict |
|---|---|---|---|---|
| UTILITY (bare) | open UTILITY page | arm CAPTURE (return on release) | <500ms = tap (`CAPTURE_UTIL_TAP_MS` `:1321`) | navigate vs arm-modifier — OK |
| PATTERN (bare) | open PATTERN page | arm capture-to-slot modifier | hold = any sub-gesture consumed (`:2024`) | navigate vs arm-modifier — OK |
| PHRASE-view waypoint | RECALL phrase (restore) | **CAPTURE** (create/overwrite phrase) | ≥1000ms (`PHRASE_CAPTURE_HOLD_MS` `:2359`,`:2578`) | hold = **create** |
| SELECT+BOOKMARK | CHECKPOINT (bless/save) | **REVERT** (discard live jam) | ≥1000ms (`ANCHOR_REVERT_HOLD_MS` `:1770`,`:1792`) | hold = **destroy** |
| METRONOME→FREEZE | toggle FREEZE (latching) | n/a (toggle, `BUTTON_BEH_METRONOME 1`) | — | — |

**Polarity inconsistency CONFIRMED:** both at the same 1000ms threshold, **PHRASE hold = create-a-thing (capture)** but **SELECT+BOOKMARK hold = destroy-your-work (REVERT)**. A muscle-memory "hold = commit/keep" learned on PHRASE is *exactly wrong* on the anchor gesture. Both are flagged in-source as provisional/by-ear (`:2531`, `:1768`).

---

### 4. Hidden modes a player must track (state that silently redefines a gesture)

| Mode state | What it silently changes | LED/feedback | Evidence |
|---|---|---|---|
| **`seq_core_state.FREEZE`** | Generator auto-mutate at bar-wrap is skipped (`seq_generator.c:703`). This re-defines RECALL: recall-while-live = regenerating posture, recall-while-FROZEN = static tape. Master mutation switch. | METRONOME LED lit = FROZEN (`seq_ui.c:4242`) + 1s "FROZEN/live" popup (`:1257`). The recall meaning-change itself is NOT surfaced. | `:1238`, `:703` |
| **`seq_ui_sel_view`** | Re-interprets the entire select row, the GP row, and the datawheel (§2). | Select-row LED color pattern is the only persistent cue; the *view name* shows only while the view button is held (latched view = no live label). | `:118`, switch `:2468` |
| **morph armed** (`SEQ_PATTERN_PhraseMorphTarget()>=0` + `morph_armed_page`) | Captures GP row (morph bar) + datawheel (fine throw) on the armed page, even with PHRASE view latched on top of EDIT. | GP-row thermometer override (`:4309`). | `:903`, `:3474`, `:236` |
| **`pull_held_track != 0xff`** | A held track button turns top-row GP into a destructive bar-aligned LoadTrackFromSlot (TRACKS view only). | held-overlay status string (`pull_status`, `:204`); no dedicated LED. | `:201`, `:875` |
| **`phrase_name_edit`** | GP row + encoders + select row become a name keypad; EXIT/GP16 commit. | the editor LCD itself. | `:769`, `:2367`, `:3470` |
| **`capture_util_held` / `PATTERN_PRESSED`** | Arm CAPTURE / capture-to-slot; select row + GP row repurposed. | UTILITY LED while held (`:4250`); GP-row CAPTURE thermometer (`:4317`). | `:1331`, `:2005` |
| **`SCRUB`** | datawheel scrubs transport instead of editing the cursor field. | SCRUB toggle popup (`:1179`). | `:3456` |

The reset-hardening path clears the transient ones on RESET_STATE (`SEQ_UI_ResetTransientState`, `:247`: `pull_held_track`, sel_view→NONE, FREEZE) — but FREEZE, sel_view-latch, and morph-armed persist across normal play and must be tracked by the player.

---

### 5. Window encoders / GP row / select row / datawheel — live function + LED inventory

**Encoders** (`SEQ_UI_Encoder_Handler` `:3414`): enc0=datawheel, enc1–16=GP encoders (per-column value edit via `ui_encoder_callback`, `:3512`), enc17=BPM (`:3444`). Datawheel is special-cased in priority order: SCRUB→transport scrub (`:3456`); MENU→page select (`:3466`); name-edit→keypad (`:3470`); PHRASE+morph→fine morph (`:3474`); PHRASE alone→switch-quantize grid (`:3486`); else→cursor field.

**GP button row** (`SEQ_UI_Button_GP` `:765`): intercept priority = name-edit → PATTERN-capture group/commit → UTILITY-CAPTURE grab → pull letter/commit → morph bar → MENU shortcut → normal per-page button. Each intercept `return`s, so the first matching modal owns the row.

**Select (track) row** (`SEQ_UI_Button_DirectTrack` `:2361`): intercept priority = name-edit swallow → PATTERN dst-track → UTILITY dst-track → pull arm/source → per-view handler (§2). Stuck-bit hardening maintains `button_state` even when swallowing (`:2382`, `:2399`).

**LED inventory:**

| LED surface | State communicated | Evidence |
|---|---|---|
| GP row (override) | morph position thermometer (PHRASE+armed) | `:4309` |
| GP row (override) | CAPTURE grabbable-K thermometer (UTILITY held), lit LEDs == grabbable bars, par/trg-aware via `SEQ_CORE_CaptureMaxK` | `:4317` |
| GP row (override) | MENU shortcut page indicator | `:4281` |
| Select row | occupied phrase mask (green) + current/last-recalled waypoint (red), drift-wink RED on `ui_cursor_flash` when edited off-waypoint (PHRASE view) | `:4713`–`:4731` |
| Select row | TRACKS: group (green 0xf<<4g) + selected tracks (red) | `:4690` |
| Select row | STEPS: selected step-view (green) + played step-view (red) | `:4666` |
| Select row | MUTE: muted mask (green), optionally inverted | `:4703` |
| Select row | PAR/TRG/INS/BOOKMARK: selected layer/slot (green) | `:4694`–`:4701` |
| METRONOME LED | FREEZE engaged (lit=FROZEN) | `:4242` |
| UTILITY LED | capture armed OR on UTIL page | `:4250` |
| COPY/PASTE/UNDO/CLEAR/MOVE/SCROLL LEDs | the corresponding `seq_ui_button_state` bit | `:4251`–`:4256` |
| track[0..3] LEDs | selected group (TRACKS) / other meanings per view | `:4136` |

---

### 6. COLLISIONS / AMBIGUITIES (where the grammar is inconsistent or memory-dependent)

1. **Hold-polarity reversal** (§3): PHRASE hold=create vs SELECT+BOOKMARK hold=destroy, same 1000ms. Highest-severity grammar inconsistency. (`:2578` vs `:1792`)
2. **SONG and PHRASE buttons both force `sel_view=PHRASE`** (`:2056`, `:2074`), and SONG also sets `SONG_PRESSED`. Two different buttons land you in the same view; the SONG page's own song-step editing is shadowed by the PHRASE-view waypoint overload (`:2529` comment admits this).
3. **FREEZE silently re-defines RECALL** with no cue at recall time — the only FREEZE feedback is the METRONOME LED + a transient popup at toggle, not at the moment recall behaves differently (regen vs tape). Player must remember FREEZE state. (`:703`, `:4242`)
4. **SELECT is overloaded across two grammars**: it is both a *global* held-modifier (CLEAR/BOOKMARK/waypoint) and the *stock in-page* "immediate/section" modifier in PATTERN/MUTE/SONG/STEPSEL/ROBOTIZE/JAM. A SELECT held for one purpose changes in-page behavior simultaneously. (`seq_ui_pattern.c:172`, `seq_ui_mute.c:121`, etc.)
5. **Pull vs capture-to-slot are mirror gestures on overlapping hardware**: PATTERN-held + GP9-16 = capture-OUT to a slot; track-held + GP9-16 (TRACKS view) = pull-IN from a slot. Both are "hold something, hit a top-row number, commit a bar-aligned destructive load." Distinguished only by *which* button is the held modifier. Source explicitly notes PATTERN-held events never reach the pull intercept (`:871`) — the ordering is load-bearing.
6. **Latched sel_view has no live label**: if you tap a GP/select during a PHRASE/STEP hold, the view stays latched after release (`TAKE_OVER_SEL_VIEW`, `:2077`) but the view-name only shows while the button is down. The select-row LED color is the only persistent cue to which grammar the row is currently in.
7. **Track-undo arbitration gap** (in-source, `:1662`): a copy/paste/clear edit made AFTER a pull still loses to the unconsumed pull victim on SELECT+CLEAR / UNDO.
8. **UTILITY held forbids changing the visible track** (a switch invalidates the ring, `:2394`) — the select row means "dst track" not "select track," a silent re-scope of the most reflexive gesture on the panel.
9. **No physical UNDO** (`BUTTON_UNDO 0 0`): the only undo reachable live is SELECT+CLEAR (pull) — generator/edit undo lives on the PITCHGEN/UTIL pages, not the performance surface.

**MAP B — system risks:**

- *[serious]* **Hold-polarity reversal between PHRASE-hold (create/capture) and SELECT+BOOKMARK-hold (destroy/REVERT) at the same 1000ms threshold. Muscle memory built on one is exactly wrong on the other; REVERT discards the live jam.** — bites at: return + land — at the moment a player reaches to keep or restore a state mid-set, a mistimed/mis-aimed hold either creates where they meant to revert or destroys where they meant to keep
- *[serious]* **FREEZE silently redefines what RECALL does (regenerating posture vs static tape) with feedback only at toggle-time (METRONOME LED + transient popup), not at recall-time. A forgotten FREEZE state changes the musical result of the identical recall gesture.** — bites at: return + layer — recalling a phrase to bring a state back behaves differently depending on an invisible global the player set bars ago
- *[serious]* **sel_view is a single invisible axis that re-interprets the select row, the entire GP row, and the datawheel, yet its current value has no persistent on-screen label once latched. The select-row LED color is the only live cue to which grammar the surface is in.** — bites at: sculpt + capture + travel — every row-press during a set is interpreted through a mode the player can only confirm by re-holding a view button
- *[serious]* **Pull (track-held + top GP) and capture-to-slot (PATTERN-held + top GP) are destructive, bar-aligned, mirror-image gestures separated only by which modifier is down, with load-bearing intercept ordering. A held-modifier slip swaps capture-OUT for load-IN.** — bites at: capture + travel — building a variation library vs recombining material; the wrong modifier overwrites the wrong slot/track bar-aligned
- *[watch]* **UTILITY-held re-scopes the most reflexive gesture (select-row = pick track) to 'pick CAPTURE dst track' and forbids changing the visible track. Reaching to switch tracks during a capture-arm silently aims a grab instead.** — bites at: capture — arming retroactive CAPTURE while wanting to glance at another track
- *[serious]* **No physical UNDO on the panel; the only live undo is SELECT+CLEAR for pulls, and even that has a known arbitration gap (post-pull edits lose to the unconsumed victim). Generative/edit undo lives off the performance surface (PITCHGEN/UTIL pages).** — bites at: return — recovering from a mistake mid-set has no uniform, always-available gesture; the safety net is partial and gesture-specific
- *[watch]* **SELECT is simultaneously a global held-modifier and the stock in-page 'immediate/section' modifier. Holding SELECT for a global combo also changes in-page edit behavior on whatever page is active.** — bites at: sculpt + capture — a SELECT held to reach a global combo perturbs the underlying page's editing semantics at the same time

---

## MAP C — Signal-Flow + Dirty/Writeback Seam

*The "what one primitive breaks everything" map. All evidence is against this fork's source at HEAD (commit 7ef8c2dc family). File paths relative to `apps/sequencers/midibox_seq_v4/`.*

---

### 1. The render → emission pipeline in one tick

`SEQ_CORE_Tick(bpm_tick, …)` (`core/seq_core.c:3301`) runs once per BPM tick. Its prologue and body split cleanly into a **render stage** (rewrites the per-track *output mirror*, double-buffered) and an **emission stage** (reads the active mirror and applies live coin-flip FX on the way to the MIDI scheduler). The seam is the output mirror: anything that must survive a FREEZE/BOUNCE/CAPTURE lives at render stage; anything emission-time is invisible to `OutputActive` and lost at bounce (the rule in CLAUDE.md §3).

**Prologue (render stage), in strict order at `seq_core.c:3307–3315`:**

| Step | Function | What it does | Stage |
|---|---|---|---|
| 1 | `SEQ_GENERATOR_Tick()` (`seq_generator.c:683`) | On a track-wrap-to-step-0, mutates+rewrites every engaged generator's loop **into the source** par buffer (`write_loop_to_source`, `seq_generator.c:471`). Gated off by `seq_core_state.FREEZE` (`seq_generator.c:703`). | render (writes *source*) |
| 2 | `SEQ_CORE_TensionResolveTick(bpm_tick)` (`seq_core.c:3311`) | Walks the GRAVITY dial toward detent so field tracks re-render this tick. | render input |
| 3 | `SEQ_CORE_RenderTracks()` (`seq_core.c:2741`) | For each track: force-dirty if it carries an enabled CHORD_MASK / TENSION / live-PITCH slot (`seq_core.c:2757`), then `SEQ_CORE_RenderTrack(track)`. | render |

**`SEQ_CORE_RenderTrack(track)` (`seq_core.c:1318`) — the render stack proper:**
- Early-out if `!seq_render_dirty[track]` (`seq_core.c:1322`).
- Identity copy: `memcpy(par_buf, seq_par_layer_value[track], …)` + trg (`seq_core.c:1335–1336`) — **source → inactive half of the output mirror**.
- Iterate `seq_processor_stack[track][slot]` in slot-index order (`seq_core.c:1342`), dispatching: `PITCH` → `pitch_render_range`, `CHORD_MASK` → `chord_mask_render_range`, `TENSION` → `tension_render_range`, `LIMIT` → `limit_render_range` (`seq_core.c:1346–1361`).
- Atomic buffer flip: `seq_render_active_buf[track] ^= 1` (single-byte XOR, atomic on Cortex-M — `seq_core.c:1364–1367`), then clear dirty. After this `SEQ_*_OutputActive()` returns the freshly-rendered half; the tick body never sees a half-rendered mirror.

So the **render stack = {PITCH (transpose/force-to-scale), CHORD_MASK, TENSION/GRAVITY, LIMIT}**, all writing the output mirror. The slot bridges (`SEQ_CORE_PitchSlotSync` `:1443`, `ChordMaskSlotSync` `:1383`, `TensionSlotSync` `:1413`) keep `seq_processor_stack` mirrored from the persistent `tcc` CC fields.

**Body (emission stage), per step in the tick body (`seq_core.c:~3700–3970`):** reads the **active output mirror** via `SEQ_PAR_Get`/`SEQ_TRG_Get` (e.g. `seq_core.c:3810`,`3818`) — already the *heard* pitch post-render — then applies emission-time, non-mirror FX:

| Emission FX | Evidence | Coin-flip? |
|---|---|---|
| Groove delay | `SEQ_GROOVE_DelayGet` (`seq_core.c:3708`,`3710`) | deterministic |
| Robotize | `SEQ_ROBOTIZE_Event(track, step, e)` (`seq_core.c:3890`) | RNG (per-track seed) |
| Drum probability | `SEQ_PAR_ProbabilityGet … SEQ_RANDOM_Gen_Range(0,99) >= prob → continue` (`seq_core.c:3884–3886`) | **coin-flip** |
| Nth-trigger play/mute/fx | `seq_core.c:3895–3926` | deterministic (bar count) |
| Legacy pitch (Arp / Drum / Chord-par only) | `legacy_pitch` gate (`seq_core.c:3937–3947`) → `SEQ_CORE_Transpose` | the migration fence |
| Humanize / echo / glide | emission chain (`seq_core.c:3953` glide; humanize/echo downstream) | humanize = coin-flip |

The **CAPTURE design split lives exactly here** (`seq_core.c:229` comment): stopped re-sim regenerates from source+seed (reproducible); while-playing tape tees the *emitted* stream because the emission coin-flips and live keys cannot be reproduced.

---

### 2. The dirty/writeback machinery — state diagram in prose

**Two RAM flags, both 4-bit (one bit per group):**

| Flag | Decl | Meaning | Set by | Cleared by |
|---|---|---|---|---|
| `seq_pattern_dirty` | `seq_pattern.c:64` (`u8`, global, non-static) | "live diverged from working slot" → FEARLESS auto-writeback eligibility | `SEQ_PATTERN_DirtySetTrack` (always), recall/revert tails OR-all (`seq_pattern.c:539`), pattern-switch CC-replay | `SEQ_PATTERN_Save` into working slot (`seq_pattern.c:1430`), `SEQ_PATTERN_Load` tail (`:1396`), `DirtyClearGroup` (`:264`), `Fix` (`:1526`), boot-init (`:161`) |
| `phrase_drift` | `seq_pattern.c:91` (`static u8`) | "MY deliberate edits since last recall/capture" → PHRASE drift LED | same chokepoint **BUT gated** `if(!seq_generator_in_automutate)` (`seq_pattern.c:249–250`) | recall/capture/probe tails (`:657`,`:686`,`:726`,`:766`) |

**The single source-write chokepoint:** `SEQ_PATTERN_DirtySetTrack(track)` (`seq_pattern.c:236`). IRQ-guarded read-modify-write. Maps track→group bit, ORs `seq_pattern_dirty`, and conditionally ORs `phrase_drift`. **Every legitimate source write is supposed to pass through it.** The clean callers that DO, via the Set chokepoints:
- `SEQ_PAR_Set` (`seq_par.c:261`), `SEQ_TRG_Set`/`SEQ_TRG_Set8` (`seq_trg.c:303`,`329`), `SEQ_CC_Set` (`seq_cc.c:523`).

**The render mirror deliberately does NOT pass through** — `SEQ_CORE_RenderTrack` writes the mirror directly, so per-tick rendering can never false-dirty (`seq_pattern.c:230–231`).

**The third actor — the auto-mutate window:** `seq_generator_in_automutate` (`seq_generator.c:51`, non-static global). Set to 1 around the per-measure mutate loop (`seq_generator.c:709–718`). Its whole job: let generator wander mark `seq_pattern_dirty` (so the wandered organism is written back faithfully on a switch) **without** dirtying `phrase_drift` (ambient wander ≠ "my edit"). This is the exact knob the reverted DRIFT-gated-writeback experiment toyed with.

**The bypass-writer class** — writers that hit `seq_*_layer_value[]` by raw `memcpy`/`memset`, bypassing the Set chokepoints, and must call `DirtySetTrack` manually (each is a place the mask can silently desync if someone forgets):
- `SEQ_GENERATOR_Undo` direct memcpy (`seq_generator.c:471`)
- generator `write_loop_to_source` memcpy (`seq_generator.c:471` region)
- UI track-clear memset (`seq_ui_pattern.c:565`)
- UI paste/clear preset-copy (`seq_ui_util.c:912`)
- UNDO_Track memcpys (`seq_ui_util.c:939`)
- file_t import (`seq_file_t.c:476`)

**Writeback:**
- `SEQ_PATTERN_WritebackIfDirty(group)` (`seq_pattern.c:274`): returns 0 unless `seq_pattern_dirty & bit` AND `pattern_loaded & bit` (the "loaded gate" — boot-init debris isn't a jam, `:280–281`); else `SEQ_PATTERN_Save(group, …)`.
- `SEQ_PATTERN_WritebackAllDirty()` (`seq_pattern.c:293`): loop all 4 groups → IfDirty. **This is the freeze multiplier.**
- `SEQ_PATTERN_Save` (`seq_pattern.c:1414`): `MUTEX_SDCARD_TAKE` → `SEQ_FILE_B_PatternWrite` (`seq_file_b.c:1503`) → clears dirty bit only if target == working slot (`:1426–1430`).

**The switch margin:** `seq_core_pattern_switch_margin_ms=100` fallback (`seq_core.c:517`); `…measured_ms` tracks the worst real I/O (`seq_pattern.c:1320–1323`); `SEQ_CORE_SwitchMarginMs()` adds headroom but **caps at 250 ms** (`seq_core.c:5232`). The pattern-switch path (`SEQ_PATTERN_Handler`, `seq_pattern.c:1238`) sizes its forward-delay from the measurement; **phrase recall keeps the fixed margin** (`seq_pattern.c:465`, comment `:1319`).

**State machine (one group):** `CLEAN(loaded)` --source write/wander--> `DIRTY` --switch/recall writeback--> Save→`CLEAN`; `Load`/`DirtyClearGroup`/`Fix` force `CLEAN` without saving (slot wins); recall/revert tail forces all 4 `DIRTY` (live≠slot after restore, `seq_pattern.c:538–541`).

---

### 3. The return-family-rides-the-mask table

Every return-family op touches the *same two globals*. This is the coupling the review named.

| Operation | Reads / writes on the mask | What breaks if the mask is wrong |
|---|---|---|
| **FEARLESS auto-writeback** (pattern switch) | `SEQ_PATTERN_Handler` (`seq_pattern.c:1262`) calls `WritebackIfDirty(group)` *before* `Load`; immediate-switch path same (`seq_pattern.c:1187`) | False-clean → outgoing jam silently lost on switch. False-dirty → needless ~290 ms save in the switch window (margin overrun → clock hitch) |
| **CAPTURE re-sim** (stopped) | `CaptureSpanSnapshot` saves `seq_pattern_dirty` (`seq_core.c:2004`); `CaptureSpanRestore` *restores* it (`seq_core.c:2021`) to "clear src's spurious wander-dirty" from the offline drive | If not restored: the re-sim's own wander-drive would leave src permanently dirty → next switch writes back a *simulated* future the user never heard |
| **CAPTURE tape** (while-playing) | `CaptureSpanPrepDst` snapshots one group's bit (`dirty_snap`, `seq_core.c:2412`) and restores exactly it (`seq_core.c:2475`); the grab itself dirties dst (`:2212`,`:2309`) | If the dst-group staging trample isn't masked out: a clean group comes out flagged → spurious auto-writeback of staging garbage |
| **PHRASE recall** | `SnapshotRead(…, writeback_dirty_first=1, …)` → `WritebackAllDirty()` (`seq_pattern.c:474–475`) *before* the 4-group read; tail OR-all 4 dirty (`:539`) | **THE FREEZE** (§4). Also: if writeback skipped, a live nudge is lost ("never lose work" guarantee broken) |
| **PHRASE capture** | `SnapshotWrite` reads live, **leaves `seq_pattern_dirty` untouched** (`seq_pattern.c:346`); clears `phrase_drift` in tail | Capturing must not auto-commit live into working slots; if it dirtied, a later switch would clobber |
| **CHECKPOINT** | `SnapshotWrite` into `MBSEQ_AN.V4` anchor bank — explicitly **leaves dirty untouched** (`seq_pattern.c:331`,`349`) | An anchor write is not a working-slot save; if it dirtied, REVERT semantics would entangle with switch writeback |
| **REVERT** | `SnapshotRead(ANCHOR, 0, writeback_dirty_first=0, 0)` (`seq_pattern.c:570`) — **no** pre-writeback (REVERT discards live by intent); tail OR-all 4 dirty | If it pre-wrote-back like recall, REVERT would persist the very state you're reverting away from |
| **THE PULL / track undo** | `SEQ_GENERATOR_Undo` (`seq_generator.c`) memcpys source then `DirtySetTrack` (`:471`); `UndoInvalidate` dropped on every Load/recall (`seq_pattern.c:545`,`seq_core.c:1403`) | One-deep snapshot stale-after-load → UNDO would clobber freshly-loaded bytes (the reason for the invalidate) |
| **generator undo** | same `SEQ_GENERATOR_Undo`; disengages generators so restored source isn't re-overwritten next measure | Without disengage, next `SEQ_GENERATOR_Tick` re-mutates over the undo |
| **QUEUED: unified UNDO/REDO** | will unify the 5 bespoke one-deeps (Pull, gen-undo, UNDO_Track, capture span has none, auto-undo) — all currently independent of the mask but all interact via `DirtySetTrack` | Risk: a unified overlay would centralize on the same fragile plumbing |
| **QUEUED: SET (durable baseline)** | **deliberately AVOIDS the mask** — file-copy layer (`SEQ_FILE_CreateBackup`→`FILE_Copy`), SD-only, ~0 RAM (design `doc/…DESIGN.md:2049–2057`) | Design explicitly rejected an overlay/divergence scheme because it "adds logic to the fragile shared dirty plumbing" (`DESIGN.md:2057`) — i.e. the review's SPOF concern is already acknowledged and routed around for SET |

---

### 4. The recall-freeze causal chain (precise)

1. A generators-running jam: `SEQ_GENERATOR_Tick` fires every track-wrap (`seq_generator.c:695`), runs `mutate_loop`+`write_loop_to_source` under `seq_generator_in_automutate=1` (`seq_generator.c:709–717`).
2. `write_loop_to_source` memcpys source then `SEQ_PATTERN_DirtySetTrack(track)` (`seq_generator.c:471`) → ORs `seq_pattern_dirty` for that group (phrase_drift suppressed by the gate). Over a few bars, **all 4 engaged groups go dirty.**
3. User taps PHRASE recall → `SnapshotRead(…, writeback_dirty_first=1, …)` → `SEQ_PATTERN_WritebackAllDirty()` (`seq_pattern.c:474`).
4. That loops 4 groups → `WritebackIfDirty` → `SEQ_PATTERN_Save` → `SEQ_FILE_B_PatternWrite` for each dirty group.
5. **One pattern Save ≈ 290 ms**, flash-PROGRAM-bound (NOT f_sync, NOT CPU, NOT zero-fill; ~12–16 ms × ~18 sectors), vs ~22 ms load. Source: design doc measured-on-hardware note (`doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md:2902` ff). The write path is `seq_file_b.c:1503` (`FILE_WriteOpen`/`FILE_WriteBuffer`×~18 records/`FILE_WriteClose` at `:1577`,`:1611`–`:1715`). **Confirmed by source: the cost magnitude is documented/measured, not in-source-visible — the .c only shows the sequence of `FILE_WriteBuffer` calls, not the flash timing.**
6. 4 dirty groups × 290 ms ≈ **~1.16–1.3 s** serial SD writes inside the recall window, while the recall body itself (open ~5 ms + 4-group read ~66 ms + render ~1 ms ≈ 72 ms) easily fits the margin. The clock freezes for the writeback duration because phrase recall uses the **fixed 250-ms-capped margin**, not a measured one (`seq_pattern.c:465`, comment `seq_pattern.c:1319`).

**Three candidate cures and their structural cost:**

| Cure | Mechanism | Cost |
|---|---|---|
| **Incremental save** | program only changed sectors (~18→few) | The "only structural cure that keeps wander" (`DESIGN.md`). Own bundle; touches the SD/file write path (`seq_file_b.c` `PatternWrite`) — invasive, must diff against on-disk record |
| **DRIFT-gated writeback (tried+reverted)** | gate recall writeback on `phrase_drift` (deliberate) not `seq_pattern_dirty` → ambient wander abandoned on recall | Tiny code change (swap the mask read in `SnapshotRead`'s pre-writeback). Worked on bench (wander→0 writebacks→~72 ms). **Reverted** on a "things revert" by-ear worry; gated on a by-ear call whether un-captured wander should survive recall (`DESIGN.md:~2918`) |
| **Accept** | leave the ~1.3 s freeze | Zero code. Margin-sizing can't help (needs ~1.3 s; `SEQ_CORE_SwitchMarginMs` caps 250 ms, `seq_core.c:5232`). Full-fidelity RAM-snapshot deferral is a dead end (~30 KB vs ~9 KB free main SRAM — corrected from the doc's stale ~33 KB) |

---

### 5. Failure modes + containment sketch

**Partial-write exposure.** `SnapshotWrite` (CHECKPOINT/phrase-capture) writes 4 group records in a loop with no atomic temp+rename; a mid-loop SD failure leaves a partial/mixed snapshot, surfaced only as a negative return (`seq_pattern.c:334–338`,`347`). Same power-loss class as a working-slot save. Containment: documented as accepted POC cost; fix = atomic temp+rename (already proven dir-capable for the SET layer, `DESIGN.md:2052`).

**The bypass-writer dirtying gap.** Six raw-memcpy/memset sites (§2) must each remember to call `DirtySetTrack` — the mask has no structural enforcement that a source write dirties. A new bypass writer that forgets → silent false-clean → lost-on-switch. Containment today is purely by-convention + the inline comments at each site.

**Mutex / critical-section ordering (the interrupts-on recall fix).** `SnapshotRead` takes `MUTEX_SDCARD_TAKE` and reads with **interrupts ON** (`seq_pattern.c:477–495`), explicitly NOT inside `portENTER_CRITICAL` — the old critical-section version blocked the higher-priority emission task (TASK_MIDI) for the whole multi-ms read → audible mid-bar timing glitch on a live switch. The SD mutex still serializes; a tick mid-read keeps emitting the *current* mirror (new mirror built by the forced render only after read completes), mirroring `SEQ_PATTERN_Load`. Invariant: **SDCARD mutex must be taken BEFORE entering any critical section or the take hangs** (`seq_pattern.c:462` comment). By contrast `SEQ_PATTERN_Handler` runs its load loop *inside* `portENTER_CRITICAL` (`seq_pattern.c:1248–1308`) with SD mutex taken first — that's the switch path, gated by the forward-delay margin.

**What per-feature dirty-accounting would look like (containment design):** the structural fix the review asks for is to stop every return-family op sharing one global mask. Options grounded in the current shape:
- *Per-feature dirty registers* — split `seq_pattern_dirty` into intent-tagged sources (user-edit vs wander vs CC-replay), so recall can choose which classes to write back without the brittle save/restore dance that CAPTURE re-sim (`seq_core.c:2004`/`2021`) and tape (`:2412`/`2475`) and the auto-mutate window (`seq_generator.c:709`) each reinvent locally. The DRIFT-gated experiment was a 2-class prototype (edit vs wander) — `phrase_drift` already IS that second register, unused by writeback.
- *Isolation*: the SET layer's choice (file-copy, mask-untouched — `DESIGN.md:2049–2057`) is the model — keep durable-return operations entirely off the live mask. CHECKPOINT/REVERT already do this (anchor bank, dirty untouched). The remaining mask-rider with no containment plan is **phrase recall's `WritebackAllDirty`** — it is the one that fires the freeze and the one place a per-class register would pay off.

**MAP C — system risks:**

- *[wall]* **Phrase recall's WritebackAllDirty serializes up-to-4 x ~290ms flash saves inside the recall window, freezing the clock up to ~1.3s. It is the one return-family op that rides the shared mask AND has no containment plan (unlike CHECKPOINT/SET which route around it). Margin-sizing structurally cannot fix it (250ms cap).** — bites at: return — landing a phrase while generators are running (the core north-star 'travel -> return' move)
- *[serious]* **The dirty mask is a single global with no structural enforcement that a source write dirties it. Six bypass-writers (raw memcpy/memset) rely on convention to call DirtySetTrack; a forgotten call = silent false-clean = jam lost on the next switch. Every new bypass writer reopens this.** — bites at: capture/layer — any new gesture that writes source bytes directly (a new generator type, a new paste/clear path)
- *[serious]* **Three different return-family ops (CAPTURE re-sim, CAPTURE tape, the auto-mutate window) each locally reinvent a save/restore-the-dirty-mask dance because there is no per-class dirty accounting. Each is correct in isolation but they are uncoordinated; adding a new offline-drive op means hand-rolling a fourth dance and getting the snapshot/restore exactly right or leaking a spurious writeback.** — bites at: capture — extending re-sim/tape or adding any new offline source mutation
- *[watch]* **SnapshotWrite (CHECKPOINT + phrase capture) writes 4 group records non-atomically; a mid-loop SD failure leaves a partial/mixed snapshot surfaced only as a negative return the caller must heed before trusting a later REVERT/recall.** — bites at: sculpt/capture — blessing a state to anchor or phrase slot under a flaky SD card or power dip
- *[watch]* **The reverted DRIFT-gated-writeback cure is re-buildable but gated on an unresolved by-ear question (should un-captured wander survive a recall?). Until decided, the freeze is 'accept'. phrase_drift already IS the second dirty register the fix would use — it exists but is unused by writeback.** — bites at: return — deciding the recall-landing semantics for live wander

---

## Open unknowns (on-device measurement / by-ear only — not determinable from source)

- MSP stack high-water mark vs the 9 KB main free region — whether deep paths (SD-save-under-recall, re-sim capture) approach the top of .bss. Requires on-device stack-watermark measurement; cannot be known from the elf.
- Tick-prologue wall-clock with 16 chord_mask/tension tracks at 256 steps and fast BPM — the all-16-sweeping CPU worst case. Stopwatch saturates at 65.5 ms (per MEMORY); measure by per-track accumulation on-device.
- Whether the trigger-gen pool can be usefully musical at a CCM-fitting size (~6 KB / ~half the slots) or whether folding into the existing 64-slot pool starves pitch generators — a by-ear capability question.
- Exact net RAM delta of unified UNDO/REDO: depends on how many bespoke one-deeps actually collapse vs. the new redo ply's snapshot depth — needs the consolidation design before it can be banked precisely.
- Real-world peak of the MIDI scheduler pool (SEQ_MIDI_OUT_MAX_EVENTS=256) under everything-on emission — flagged in MEMORY as needing measurement if peak >200; not an allocation question but a drop-risk one.
- Whether the 500ms UTILITY tap and 1000ms PHRASE/anchor hold thresholds feel right in live use (set by ear; source marks them provisional) — only on-device play can tune them.
- Whether the select-row LED color alone is a sufficient persistent cue for which sel_view grammar is active when no view button is held — needs by-eye validation under stage conditions.
- Whether the polarity reversal (PHRASE hold=create vs anchor hold=destroy) actually causes mis-fires in practice or whether the spatial separation of the two gestures (waypoint row vs SELECT+BOOKMARK) prevents confusion — only by-hand testing tells.
- Whether FREEZE-state forgetfulness causes real musical surprises at recall — needs a play session with FREEZE toggled across recalls.
- Exact physical adjacency / reach of UTILITY, PATTERN, SELECT, BOOKMARK, METRONOME on the midiphy LH faceplate (the .NGC/panel art, not the pin map) — determines how easy modifier-slip collisions are; not determinable from source pin assignments alone.
- The ~290ms-per-Save figure and its ~12-16ms x ~18-sector breakdown are documented as measured-on-hardware (DESIGN.md:2902 ff); the .c source only shows the sequence of FILE_WriteBuffer calls, not flash-program timing. Re-measure on device (1us stopwatch saturates at 65.5ms; accumulate per-record) before sizing an incremental-save cure.
- How many groups are actually dirty at recall time in a real jam depends on how many generators are engaged across groups — only by-device measurement (the 'engaged-generator-count' the doc flags as owner-unknown) tells whether the freeze is typically 1x290ms or 4x290ms.
- Whether un-captured generator wander SHOULD survive a phrase recall is a by-ear musical call, not determinable from source — it decides whether the DRIFT-gated-writeback cure is acceptable.
- Incremental-save feasibility: whether SEQ_FILE_B_PatternWrite can cheaply diff a record against its on-disk image to program only changed sectors needs investigation against FatFs/FILE_WriteBuffer behavior on this fork; not visible from the current write path alone.
- Actual margin-overrun audibility: whether a single ~290ms switch-path writeback (FEARLESS auto-writeback, measured-margin path) ever exceeds the 250ms cap enough to hitch the clock is an on-device by-ear observation.

---

## The fixed Ableton test-bed (build once, reuse every shakedown)

A FIXED Ableton Live set used purely as a stable, repeatable instrument under the box — it is a CONSTANT, never edited between sessions, so every change heard is attributable to the box. Build target ~20 min. Layout:

ROUTING / CLOCK (build first, 2 min):
- Box is MIDI clock + transport MASTER. Ableton: Link/MIDI prefs -> the box's port = Sync IN enabled (clock + start/stop), Track+Remote OFF on that input so it can't double-trigger. This makes the box's transport() / clock-step the single timebase — matches the new CMD_TRANSPORT verb and keeps re-sim vs live-tape capture comparable bar-for-bar.
- One MIDI-from = the box; fan it to the tracks below by channel.

TRACK 1 — SAMPLE-MAPPED DRUM RACK (pitch == cell), ch 10:
- A 16-pad Drum Rack, one one-shot sample per pad, mapped so MIDI note number == pad. This exercises the DRUM event-mode / par-layout path and the drum-K<=4 capture ceiling directly. Pick samples with sharp transients (clave, rim, cowbell, closed hat) so groove-delay, probability coin-flips, and precise-gate capture are AUDIBLE as timing/density, not smeared. Velocity -> volume so accent/velocity-lerp morph is visible.

TRACKS 2-4 — MELODIC INSTRUMENTS (exercise the full pitch range), ch 1/2/3:
- T2 = a plucky/percussive synth (short decay) so per-step NOTE swaps, force-to-scale, and transpose land cleanly and you can hear exact pitch. Map across >=3 octaves so SEQ_CORE_TrimNote octave-fold (not clamp) behavior shows if it triggers.
- T3 = a sustained pad/saw so GRAVITY/tension and chord-mask render-stage moves are audible as harmonic motion, not just note onsets. This is the track to sweep for the all-16 / live-input render-cost case.
- T4 = a mono bass so self-transpose / self-bus moves read clearly in the low end.

CHORD-SOURCE-ON-A-BUS RIG (for chord_mask / held-chord live input), ch 4:
- One track receiving a held chord (from a small controller or a held Ableton clip looping a sustained triad/quartal voicing) routed to the box as the live chord source that chord_mask reads. Keep it on its own channel/bus so you can mute the chord WITHOUT muting the melodic tracks — that mute/unmute is the A/B test for the per-tick force-dirty render cost. Put a sustained 4-note clip on a 1-bar loop as the default so the chord input is always present and stable.

CV/GATE + CLOCK MONITORING (so you SEE what the box emits):
- A MIDI monitor device (or a free monitor plugin) on a dedicated return/utility track logging raw note-on/off + CC + clock from the box. This is how you eyeball: probability coin-flips (notes dropping), robotize, precise-gate offs (the queued capture refinement), and self-bus CCs hitting the track's own config. If the box's CV/gate outs are wired, also patch gate+CV to an audio input and arm a scope/tuner track so emission timing/jitter is visible against Ableton's grid.
- A click/metronome reference track on the master clock so any recall/switch CLOCK FREEZE (the up-to-1.3s recall hitch) is immediately audible as a dropped click — this set must make the freeze HEARABLE, since that is the #1 thing the flat-map says to fix.

DISCIPLINE: save it read-only / as a template. No clips beyond the held-chord loop and the click. Everything else comes from the box. Re-open the identical set every session.
