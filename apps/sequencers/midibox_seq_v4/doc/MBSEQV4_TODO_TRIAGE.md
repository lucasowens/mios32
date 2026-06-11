# MBSEQ V4 — Open TODO Triage

Priority list for the 66 open TODOs/FIXMEs/XXX markers catalogued in
[MBSEQV4_REFERENCE.md § 6](MBSEQV4_REFERENCE.md#6-open-todos--fixmes--xxx-66-items).

Tiers are ordered by **user-visible impact × leverage** (severity × hit rate),
not by implementation effort. Paths are relative to this `doc/` directory.

Generated: 2026-05-18.

---

## Tier 1 — High user impact (data safety & correctness)

1. **Overwrite confirmation when saving** — [seq_file_m.c:410](../core/seq_file_m.c#L410), [seq_file_b.c:690](../core/seq_file_b.c#L690), [seq_file_s.c:404](../core/seq_file_s.c#L404).
   Silent destruction of work when saving over an existing map/pattern/song slot. Three sites, one fix pattern. Highest-impact, lowest-risk item on the list.
2. **Pattern-change stall race** — [seq_pattern.c:135](../core/seq_pattern.c#L135).
   "stall here if previous pattern change hasn't been finished yet!" — affects live pattern switching, a headline workflow.
   *(Re-read 2026-06-11: the TODO marks a **missing** stall — in the deferred branch a
   new request silently overwrites a pending unserviced `seq_pattern_req[group]`. The
   SD read itself blocks inside `portENTER_CRITICAL()` in `SEQ_PATTERN_Handler` (can
   exceed 65 ms), covered by the pre-generated forward-delay ticks. Becomes
   load-bearing if pattern switches gain an auto-writeback — write+read in the same
   window; see `doc/plans/2026-06-11-save-model-groups-performing-curating.md`.)*
3. **Record-while-reverse correctness** — [seq_record.c:454](../core/seq_record.c#L454), [:588](../core/seq_record.c#L588).
   Notes land on wrong steps when a reverse-direction track is in record mode. Two sites.
4. **Invalid-bank error feedback** — [seq_ui_disk.c:692](../core/seq_ui_disk.c#L692), [seq_ui_pattern.c:140](../core/seq_ui_pattern.c#L140).
   User gets silent no-op when bank is empty — classic "is it broken?" pitfall.
5. **Song mode `bpm_start`** — [seq_song.c:244](../core/seq_song.c#L244).
   Per-song starting BPM is ignored. Surprises anyone using songs across tempos.

## Tier 2 — Feature gaps users will notice

6. **Song prefetch to end-of-step** — [seq_song.c:413](../core/seq_song.c#L413). Tightness of song-step transitions.
7. **Copy preset to all selected tracks** — [seq_ui_trkevnt.c:1529](../core/seq_ui_trkevnt.c#L1529). QoL win for multi-track edits.
8. **MIDI import: dedicated-track selection** — [seq_midimp.c:214](../core/seq_midimp.c#L214), [:379](../core/seq_midimp.c#L379), [:542](../core/seq_midimp.c#L542). Currently all-or-nothing import; three sites, one feature.
9. **Custom groove templates on SD** — [seq_groove.c:5](../core/seq_groove.c#L5). Long-standing wishlist; broad creative impact.
10. **Auto-load patterns on SD insert** — [app.c:801](../core/app.c#L801). Small UX polish, often-asked.
11. **Record tolerance scaled by BPM** — [seq_record.c:580](../core/seq_record.c#L580). Improves quantize-while-record feel at slow tempos.

## Tier 3 — Correctness / robustness (latent bugs, lower hit rate)

12. **`app.c` mutex handling** — [app.c:595](../core/app.c#L595). Concurrency smell; TK's quirks apply ([feedback_tk_code_respect]).
13. **SysEx multi-device collision notification** — [seq_midi_sysex.c:192](../core/seq_midi_sysex.c#L192).
14. **SysEx independent Remote stream** — [seq_midi_sysex.c:193](../core/seq_midi_sysex.c#L193).
15. **`clk_divider` special-value handling moved to SEQ_CV** — [seq_core.c:839](../core/seq_core.c#L839). TK flagged as "dirty code."
16. **CV-divider `get_dec` 64-bit support** — [seq_file_gc.c:282](../core/seq_file_gc.c#L282), [:294](../core/seq_file_gc.c#L294).
17. **Pattern-remix sync with `SEQ_PATTERN_Handler()`** — [seq_ui_pattern_remix.c:705](../core/seq_ui_pattern_remix.c#L705).
18. **Song/`DirectTrack` alignment** — [seq_ui_song.c:61-62](../core/seq_ui_song.c#L61). Drift hazard between two button paths.

- *(unnumbered, added 2026-06-11)* **Guide-track clamp uses group count** —
  [seq_file_s.c:351](../core/seq_file_s.c#L351): `seq_song_guide_track` is clamped
  against `SEQ_CORE_NUM_GROUPS` (4) while the adjacent comment says the range is
  0..16 — guide tracks 5–16 are silently zeroed on song load. Found during the
  2026-06-11 group-coupling census (no TODO marker in source, hence unnumbered).

## Tier 4 — BLM / Pattern Remix polish (only affects accessory-hardware users)

19. BLM red-LED accents — [seq_blm.c:226](../core/seq_blm.c#L226)
20. BLM selected-trg-layer awareness — [seq_blm.c:440](../core/seq_blm.c#L440)
21. BLM layer cycling when full — [seq_blm.c:505](../core/seq_blm.c#L505)
22. BLM 8row/16row mode split — [seq_blm.c:1291](../core/seq_blm.c#L1291)
23. Pattern-remix incrementer helper — [seq_ui_pattern_remix.c:154](../core/seq_ui_pattern_remix.c#L154)
24. Pattern-remix Ableton remote-script — [seq_ui_pattern_remix.c:311](../core/seq_ui_pattern_remix.c#L311) (big scope)
25. Pattern-remix options page — [seq_ui_pattern_remix.c:50](../core/seq_ui_pattern_remix.c#L50)

## Tier 5 — Performance / micro-optimizations (no behavior change)

26. MIDI router 3-word packing — [seq_midi_router.c:164](../core/seq_midi_router.c#L164)
27. OSC availability special-case — [seq_midi_router.c:275](../core/seq_midi_router.c#L275)
28. Ethernet-availability checks — [seq_midi_port.c:415](../core/seq_midi_port.c#L415), [:436](../core/seq_midi_port.c#L436), [:453](../core/seq_midi_port.c#L453)
29. `seq_random.c` timer-mixing — [seq_random.c:51](../core/seq_random.c#L51)
30. Layer Q&D CC forwarding cleanup — [seq_layer.c:479](../core/seq_layer.c#L479), [:840](../core/seq_layer.c#L840)

## Tier 6 — Cosmetic / debug / orphaned markers (lowest)

- LCD enhanced messages + chord-velocity print — [seq_lcd.c:383](../core/seq_lcd.c#L383), [:701](../core/seq_lcd.c#L701), [:716](../core/seq_lcd.c#L716), [:836](../core/seq_lcd.c#L836)
- Empty/placeholder TODOs — [seq_blm.c:1734](../core/seq_blm.c#L1734), [seq_ui_bpm_presets.c:78](../core/seq_ui_bpm_presets.c#L78), [seq_cc.c:212](../core/seq_cc.c#L212), [:414](../core/seq_cc.c#L414)
- Orphaned marker — [seq_file.h:18](../core/seq_file.h#L18)
- Debug print — [seq_ui_opt.c:1344](../core/seq_ui_opt.c#L1344)
- Reserved UI page stubs — [seq_ui_pages.c:44](../core/seq_ui_pages.c#L44), [seq_ui_todo.c](../core/seq_ui_todo.c)
- Layer minor TODOs — [seq_layer.c:404](../core/seq_layer.c#L404), [:1084](../core/seq_layer.c#L1084), [seq_morph.c:144](../core/seq_morph.c#L144), [seq_ui_trkevnt.c:956](../core/seq_ui_trkevnt.c#L956)

---

## Suggested attack order

Quick first sweep with disproportionate user value:

1. **#1 — overwrite confirm** (3 sites, same fix pattern)
2. **#4 — bank-empty error message** (2 sites)
3. **#5 — song bpm_start**

All three are scoped, user-visible, and unlikely to disturb TK's tight
timing paths. Escalate to **#2** (pattern-change stall) and **#3** (reverse
record) once back in core/timing-sensitive territory.
