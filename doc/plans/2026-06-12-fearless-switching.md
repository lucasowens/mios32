# Plan — FEARLESS SWITCHING bundle (auto-writeback + CHECKPOINT / REVERT)

**Date:** 2026-06-12 · **Status:** forks RESOLVED (user-blessed), building.
Second bundle of the save-model rethink (D1/D2 mechanism — see
`doc/plans/2026-06-11-save-model-groups-performing-curating.md` §2/§3.2).
Delete once executed into design doc §9 / REFERENCE / MANUAL (§7 below lists
the exact edits).

**To resume cold:** design doc §9 "2026-06-11 — Save model" block + §10
save-model opens, RECOMBINE memory/REFERENCE §3 (track undo + pull), then this
file. Verified against main at `99342816`.

**The playable loop at GO (the bundle's by-ear gate):** *jam, switch away,
come back — nothing lost, including a living generator; REVERT = one gesture
back to the blessed anchor.*

---

## 1. Resolved forks (user-blessed 2026-06-12)

| Fork | Resolution |
|---|---|
| Anchor grain | **Organism gesture, track storage** — CHECKPOINT blesses all 16 tracks as individual track-grain anchors in one gesture; REVERT restores all 16. Phrases later pin single-track anchors; per-track REVERT addable later. |
| Anchor depth | **One-deep** — one blessed copy; CHECKPOINT overwrites it, REVERT returns to it (Octatrack Reload-Part precedent). Versioning arrives with phrases/librarian if needed. |
| Performance fence | **None in v1** — CHECKPOINT/REVERT bounds the blast radius; judge by ear whether committed-performance ever hurts (DN2 PERFORM KIT lesson stays on the watch list). |
| Gen-state on load | **Resume engaged** — the organism comes back alive; the sculpted loop keeps mutating exactly as left. The engaged generator is posture, and posture relaunches. |

Derived mechanism decisions (mine, grounded in source — flag at review if any
feels wrong):

- **Dirty grain = per-group bitmask** (writeback unit is the group record;
  track-grain dirty buys nothing while `PatternWrite` writes whole records).
- **Ext tag V4 carries gen state; sessions are re-created** — bank records have
  zero ext slack (fact 6 below); device sessions are disposable garbage
  (standing note) and `SESSION_CREATE` (testctrl 0x71) exists. Write path picks
  V4 when the record has room, else degrades to V3 (the existing fit check
  already arbitrates); read handles V1–V4.
- **Anchor carrier = internal fifth bank** `MBSEQ_ANC.V4`, bank-format file
  with 4 records (one per group), not user-navigable, created lazily at first
  CHECKPOINT. Reuses `PatternWrite`/`PatternRead`/`TrackRead` wholesale, stays
  track-addressable for later phrase pinning, and never touches the hard-wired
  bank↔group identity of the four user banks.
- **REVERT sets dirty** (live ≠ working slot after restore; the next switch
  writes anchor state into the working slot — the inversion working normally).
- **Writeback never arms undo** — under the inversion "undo the writeback" is
  the anchor's job; the RECOMBINE pull-victim undo stays for pull/aimed acts.

## 2. Mechanism

**Dirty flag.** `u8 seq_pattern_dirty` (bit per group). Set inside
`SEQ_PAR_Set` / `SEQ_TRG_Set` / `SEQ_TRG_Set8` / `SEQ_CC_Set` (source-write
chokepoints — the render mirror never passes through them, so per-tick
rendering can't false-dirty). The generator's measure-boundary rewrite goes
through `SEQ_PAR_Set` (seq_generator.c `SEQ_GENERATOR_*` write path) → an
engaged group is permanently dirty, correct by design. **Bypass writers need
manual dirty-set** (same bypass class that bit the slot syncs twice):
`SEQ_GENERATOR_Undo` (direct memcpy restore), `SEQ_CORE_LoadTrackFromSlot`
(the pull — live now differs from the group's slot), track-undo restore,
track-preset import. Sweep for others at build (`memcpy.*seq_par_layer_value`
/ `seq_trg_layer_value` / direct `seq_cc_trk` writers).

**Clear sites:** group load (slot==live by construction), explicit
`SEQ_PATTERN_Save` *when the target slot == the group's working slot* (clean
chokepoint inside Save itself), session load/create (clear all).

**Writeback hook.** In `SEQ_PATTERN_Handler`, per group, before
`SEQ_PATTERN_Load`: if dirty → `SEQ_PATTERN_Save(group, seq_pattern[group])`,
clear dirty. Inside the existing SDCARD-mutex + critical section (no tearing,
no tick mid-serialize); the forward-delay margin must now cover write+read —
widen `seq_core_pattern_switch_margin_ms` default (50 → 120 ms provisional,
then measure with `seq_pattern_log_load_time` and trim). Same hook in
`SEQ_PATTERN_Change`'s immediate branch (it calls Load directly). The handler's
three call sites all inherit correct behavior: 1ms task (normal), synched
point in `SEQ_CORE_Tick` (normal), midexp (dirty is naturally clear during
export → writeback inert). testctrl `pattern_load` stays a raw load (no
writeback) — useful for HIL.

**Session switch:** before `SEQ_FILE_B_UnloadAllBanks` (session change path),
write back all dirty groups — "nothing lost" must survive a session hop.

**Stall TODO (seq_pattern.c:135).** Per plan-doc §3.5 delta 1 the race is
softer than feared: requests are per-group, and with the writeback firing at
*service* time, an overwritten pending request loses only an intermediate
switch target — never the writeback decision. Resolve deliberately at build:
implement a stall only if a real hazard survives review; otherwise retire the
TODO with a comment stating the writeback-at-service argument.

**Gen-state ext tag V4.** Per track: V3 payload + gen sub-block =
`count` + up to **4** slot entries (cap; refuse-with-message beyond — pool
realities: typical live use is 1/track). Entry ≈ 177 B: instrument, par_layer,
engaged, range_min/max, rate, depth, contour, anchor_valid, loop[64],
locks[8], anchor[64], mult[32]. `SEQ_FILE_B_TRK_EXT_SIZE` grows ~710 B/track
(~2.8 KB/record, ~180 KB/bank — SD-irrelevant). Read path re-seeds the pool
slot directly (no `Engage` call: no undo re-snapshot, no ForceRewrite — the
saved par layer already holds the last-written loop; faithful) and resumes
engaged. Pool-full on load: refuse + message (LRU stays deferred). **The pull
inherits this**: `TrackRead` restores gen state too — pulling a track brings
its generator back alive (posture pull, consistent with resume-engaged).

**CHECKPOINT / REVERT.** CHECKPOINT = write all 4 groups' live state into
`MBSEQ_ANC.V4` records 0–3 (one gesture, ~4 group-record writes). REVERT =
read all 4 back + forced render + per-group dirty set + sustain-cancel/PC
fan-out per the load path. Both refuse cleanly when the anchor file is absent
(REVERT before first CHECKPOINT). Panel gesture decided with hardware in hand
at Stage C (candidates: SAVE-tap = CHECKPOINT / SELECT+SAVE = REVERT; or
double-press SAVE; midiphy maps no free button) — testctrl verbs land first so
HIL + by-ear don't wait on the gesture choice.

## 3. Stages (GRAVITY-style; HIL + by-ear check per stage)

**Stage A — auto-writeback ("nothing lost", minus gen state).**
Dirty mask + chokepoints + bypass sweep; writeback in Handler + immediate
branch; Save-clears-dirty; session-switch writeback; stall TODO resolution;
margin widen + measure. testctrl: `DIRTY_GET` (mask), `WRITEBACK_COUNT`
(diagnostic counter). HIL pins: edit→dirty; switch→slot carries the edit
(read-back via pull/track_par_get); clean-group switch→no write (counter
flat); explicit save clears; pull/gen-undo set dirty; raw `pattern_load`
doesn't writeback. By ear: jam, switch away, come back — edits persist.

*Stage A DONE 2026-06-12 — HIL 126/126, by-ear confirmed ("that fixed it"):
manual edits, pattern-page paste, and clears all survive the round trip; no
switch-feel complaint at 100ms margin. As-built deltas from the sketch
above:*
- *Bypass-writer sweep (by-ear found it)*: pattern-page paste =
  `SEQ_FILE_T_Read` preset import, which writes par/trg/CCs directly —
  played but never flagged dirty, discarded on switch. Fixed at end-of-read
  (covers TRKEVNT import too) + MultiClear memset + `CLEAR_Track` (all
  modes) + UTIL `UNDO_Track` memcpys. All other `RenderDirtySet` sites
  audited: global-scale/posture writes, correctly outside the dirty model.
  HIL pin: `test_clear_sets_dirty`.
- *AUTOTEST A1 incident*: the rigs test edited track 0 (fixture-parked on
  A1) then session-hopped mid-test — writeback-on-hop committed the debris
  into A1. Restored from pristine (0,20); `Board.session_load` now defaults
  `discard_dirty=True` (incident log in `tests/fixtures/AUTOTEST/CONTENTS.md`).
  The rule: under the inversion, the slot a dirty group is parked on IS a
  writeback target.
- *Writeback is gated on a per-group `pattern_loaded` mask* (set at the end
  of `SEQ_PATTERN_Load`): boot-init paths (track presets, CC defaults) run
  through the `SEQ_CC_Set` chokepoint before any session loads — ungated,
  the first session load would have committed boot debris over every slot.
- *`CMD_RESET_STATE` clears the dirty mask*: its robotize/CC clears dirty all
  4 groups on every harness reset, and the next switch/session-load would
  have auto-written test debris into the AUTOTEST A1–A3 baselines.
- *`SEQ_PATTERN_Fix` clears its group's dirty bit* (it tramples live RAM as
  temp storage — a later switch must not commit that).
- *`CaptureToSlotTrack` saves/restores the dst group's dirty bit* around its
  trample-restore (the internal CC replay would false-dirty a clean group).
- *Push-into-own-working-slot semantics note*: under the inversion, a freeze
  captured into the group's own working slot is overwritten by the ongoing
  jam at the next switch (the jam wins). Freezes belong in other slots (or
  the tape, later) — MANUAL line at fold time.
- *Save-all (`seq_ui_saveall_req`) routes through `SEQ_PATTERN_Save`* so all
  four dirty bits clear.
- *Stall TODO retired with rationale comment* (no stall built): per-group
  requests mean an overwrite loses only an intermediate target; the
  writeback decision survives to service time.
- testctrl verbs: `DIRTY_QUERY 0x73` (mask + 21-bit writeback count),
  `DIRTY_SET 0x74` (test knob), `PATTERN_CHANGE 0x75` (the real switch path;
  `PATTERN_LOAD 0x55` stays a raw load that bypasses writeback by design).
- 8 HIL tests in `tests/apps/seq_v4/test_fearless_switching.py` (incl. the
  round-trip pin, session-hop pin, push false-dirty pin).

**Stage B — gen-state V4 ("the organism comes back alive").**
V4 tag write/read; resume-engaged; pull carries gen state; 4-slot cap +
refuse; session re-create on device. testctrl: gen-state probe (engaged,
loop/locks/mult/anchor digests) — check what the workbench verbs already
expose first. HIL pins: engage→sculpt→switch away/back→loop+locks+mult+anchor
byte-identical, engaged=1, mutation resumes on measure wrap; V3-session
degrade (no crash, V3 payload intact); cap refusal. By ear: sculpted Turing
loop survives the round trip *alive*.

**Stage C — CHECKPOINT / REVERT (the full loop; bundle GO here).**
Anchor bank (lazy create); organism-gesture verbs; testctrl
`CHECKPOINT`/`REVERT`/`ANCHOR_PRESENT`; provisional panel gesture last, by
feel. HIL pins: checkpoint→edit→revert→anchor state byte-faithful (incl. gen
state); revert-sets-dirty; revert-without-anchor refuses; anchor survives
session writebacks untouched. By ear: the GO loop at the top of this file.

## 4. Edges & watch list

- **Power-loss mid-writeback**: in-place record writes, same exposure as
  manual save at higher frequency; the anchor is the mitigation (separate
  file). Accepted (§9-logged cost). Watch failure frequency in practice.
- **Switch latency**: write+read ≈ 2× current SD window (~12–18 KB with V4
  ext). Margin covers it; pre-generated ticks mask it. Judge the ~100ms-class
  commit stall by ear (RECOMBINE watch-list item, still unjudged).
- **Performance fence**: deliberately unbuilt. If a committed sweep ever hurts
  on stage, that's the trigger to design it.
- **Writeback commits pulled tracks** into the destination group's working
  slot — intended (the slot is the organism's memory); MANUAL line at Stage A.
- **Writeback-on-STOP**: not in v1; consider by ear (power-off after jam
  without a switch still loses the last stretch — the tape will cover this
  better than a STOP hook).
- **Display honesty**: slot==live holds by construction after every switch;
  mid-jam staleness is inherent to the model. No UI in v1; a dirty dot is
  cheap if wanted later.
- **Mutes / global terrain (GRAVITY, window source) are NOT in the anchor** —
  v1 anchors track contents (incl. gen state) only. Organism *posture* beyond
  tracks is a phrases-era question.
- **Mainline copy/paste undo arbitration** (RECOMBINE backlog) — unchanged
  here; writeback never touches undo.

## 5. Verified source facts (symbol anchors, at `99342816`)

1. `SEQ_PATTERN_Change` immediate branch calls `SEQ_PATTERN_Load` directly;
   deferred branch holds the stall TODO (seq_pattern.c:135).
2. `SEQ_PATTERN_Handler` call sites: `app.c:524` (1ms task),
   `seq_core.c:2312` (synched point), `seq_midexp.c:391` (export). Handler
   wraps mixer-coupling + load + RATOPC in SDCARD mutex + critical section;
   ">65 mS" stopwatch warning exists.
3. Write chokepoints: `SEQ_PAR_Set` (seq_par.c:241), `SEQ_TRG_Set`/`Set8`
   (seq_trg.c:279/309), `SEQ_CC_Set` (seq_cc.c:310). Generator rewrites via
   `SEQ_PAR_Set` (seq_generator.c:234); generator undo memcpys directly
   (seq_generator.c:416) — bypass.
4. `SEQ_PATTERN_Save` callers (all must keep working; dirty-clear lives
   inside Save): seq_ui_save.c:130, seq_ui.c:2552, seq_ui_song.c:1553,
   seq_ui_pattern_remix.c:415/665, seq_blm.c:725/740, seq_testctrl.c:1407.
5. `seq_core_pattern_switch_margin_ms` default 50 (seq_core.c:193), used at
   seq_core.c:1849/2286 + seq_pattern.c forward-delay.
6. **Bank records have zero ext slack**: `SEQ_FILE_B_Create` sizes
   `pattern_size` = base + `SEQ_FILE_B_TRK_EXT_SIZE` exactly
   (seq_file_b.c:315); `PatternWrite`'s fit check is all-or-nothing
   (seq_file_b.c:1023); pad loop at :1170. Ext tags V1–V3 exist
   (seq_file_b.c:59–77); V3 payload = 1 + extCC + 64 anchors.
7. Gen state per slot ≈ 180 B: `seq_generator_t` header 12 B + loop 64 +
   locks 8 + anchor 64 + mult 32 (seq_generator.h). Pool cap 64 global.

## 6. Budget

Stage A: RAM +~8 B (mask + counter), code small. Stage B: file-size only
(~180 KB/bank); zero RAM (pool exists). Stage C: zero RAM beyond a bank-info
slot; one new SD file. No CCM pressure (52.9/64 KB used as of RECOMBINE).

## 7. On completion — design-doc edits (mechanical)

1. §9 "2026-06-11 — Save model" block: dated as-built note on the
   auto-writeback bullet (mechanism shipped, margin measured value, stall TODO
   resolution) + CHECKPOINT/REVERT bullet (grain/depth resolution as blessed).
2. §10: close "Checkpoint storage" (anchor bank as-built), close "Gen-state
   extension-tag scope" (V4, 4-slot cap), annotate "Performance fence" =
   deliberately unbuilt, by-ear trigger.
3. REFERENCE: new section — dirty model, writeback hook + 3 handler sites,
   V4 ext layout, anchor bank format, margin value.
4. MANUAL: "Fearless switching" section (what persists when, CHECKPOINT/
   REVERT gesture, pulled-track commit note, power-loss caveat).
5. Memory: project memory updated per stage; this plan deleted at bundle GO.
