# G1.6 — Groove onto the grammar (the 2nd emission tenant, paintable shape)

**Date:** 2026-07-05 · **Follows:** G1.5 (Echo reference tenant, §9 ce9a9f70)
**Scope decision (user, by-ear):** the *rich* cut — Groove becomes a **paintable
16-step shape** on the GP row (like ChordMask's mask), full delay/length/velocity.

## Why Groove now
The design doc names Groove "the config-copy archetype" and the explicit next
emission tenant after Echo. Bringing it on (a) proves the emission-row pattern
generalises to a real 2nd instance — which flushes out the two latent hardcodes G1.5
flagged (`ECHO_REPEATS` in `RowState` + the double-tap), and (b) makes groove
*operable and paintable* from the rack instead of the buried stock TRKGRV page.

## Groove facts (verified against source)
- `groove_style` byte (CC `0x53`): `style:6` (0=off, 1..6 presets, 7..22 custom
  templates), `sync_to_track:1` (bit 6). **Bit 7 free.**
- `groove_value` (CC `0x52`): intensity 0..127 — scales the `VPOS`(+127)/`VNEG`(-128)
  template cells (so on the classic Shuffle, intensity *is* the swing depth; true
  0→max sweep, pass-through at 0). Fixed-value template cells ignore it.
- DSP: `SEQ_GROOVE_DelayGet` (timing) + `SEQ_GROOVE_Event` (velocity/gatelen), both
  early-out on `!style`. Called from the shared `seq_core.c` step-advance for BOTH
  melodic and drum paths. `sync_to_track` picks `t->step` vs `ref_step`.
- Presets (0..6) are `const`; only custom templates (7..22, `seq_groove_templates[16]`)
  are editable, persisted to `MBSEQ_G.V4` (`SEQ_FILE_G_Write`).

## The row (mirrors Echo)
Row 6, `PROC_ROW_EMISSION`. Four operate dials on the encoders:
| Cell | Dial | Backing | Notes |
|---|---|---|---|
| 1 | **Styl** | `groove_style.style` (RMW, keep 0x40/0x80) | headline/occupancy; 0=off dark row; engage-seeds Intn=32 on 0→on; name on right screen |
| 2 | **Intn** | `groove_value` 0..127 | swing depth; deflt 0 (pass-through) |
| 3 | **Sync** | `groove_style` bit 6 | Trk / RefS |
| 4 | **Lane** | UI static `proc_groove_paint_lane` 0..2 | which template lane the GP row paints: Dly/Len/Vel |

**GP row = the paintable shape.** For a *custom* style: GP1..16 toggle that step's
selected-lane cell between `0` and `VPOS`; LEDs paint the lane's nonzero steps; first
paint forces `num_steps=16` (paint-the-bar) and seeds Intn if 0. Presets show the
lane read-only; off = dark. Right screen: groove name + `paint:<lane>` / `(preset-ro)`.

**Bypass = new bit-7 disable** (`0x80`), mirroring Echo's `0x40`: double-tap the row
flips it (keeps config, live A/B against straight). 2-line DSP guard in
DelayGet+Event. Persisted transparently (rides the same CC; snaps in morph like
groove-style already does).

## Generalising the two G1.5 hardcodes
`proc_row_t` gains `u8 occ_cc, disable_mask`. Echo = `{ECHO_REPEATS, 0x40}`, Groove =
`{GROOVE_STYLE, 0x80}`. `RowState` emission branch and the B-row double-tap both read
these instead of hardcoding `ECHO_REPEATS`. Occupancy = `(raw & 0x3f) != 0`; enabled
= `!(raw & disable_mask)`; strength = `raw & 0x3f`.

## Persistence
Paint sets a file-scope `proc_groove_dirty`; `SEQ_UI_PROC_page_Exit` writes
`SEQ_FILE_G_Write(seq_file_session_name)` under `MUTEX_SDCARD` (mirrors TRKGRV exit).

## Deferred (flag, don't build)
- Negative (VNEG) paint / graded per-step values — stay on the stock TRKGRV page.
- Short-loop custom grooves via PROC paint (paint forces 16). Presets/TRKGRV keep short loops.
- Row-index identity uses a params-pointer compare (`proc_params_groove`), reorder-safe.

## By-ear GO gate
Set up a drum/melodic loop → focus Groove → dial Styl to a custom → paint steps →
sweep Intn → double-tap A/B → switch Lane, paint velocity/length. Feels like the same
movement as ChordMask/Echo. GO ⇒ fold to §9 + MANUAL_FORK + memory.
