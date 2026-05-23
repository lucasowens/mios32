# midibox workspace

Native macOS dev setup for MIOS32, focused on iterating on **MIDIbox SEQ v4** running
on **MBHP_CORE_STM32F4** + MIDIPHY frontpanel.

Subtrees:
- `mios32/` — modern ARM codebase (this is what we build)
- `mios8/` — legacy PIC codebase (untouched; see *Deferred* below)
- `.devcontainer/` — old Docker setup, no longer used (kept around, not relied on)

---

## One-time setup

### 1. Toolchain

```bash
brew install --cask gcc-arm-embedded
```

This installs `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, etc. plus `newlib-nano`
(required by `--specs=nano.specs` in `mios32/include/makefile/common.mk`).

Verify:

```bash
make toolchain-check
```

### 2. MIOS Studio (uploader + MIDI monitor)

Used for sysex upload to the bootloader and as a debug terminal.

- Easiest: grab a community-built macOS `.app`, drop it in `/Applications`.
- From source: `mios32/tools/mios_studio/` ships a Projucer project. Per
  [tools/mios_studio/README.txt](mios32/tools/mios_studio/README.txt), install
  JUCE 7.0.7 into `~/JUCE`, open `mios_studio.jucer` in Projucer, then build the
  Xcode project under `Builds/MacOSX/`.

`make upload` calls `open -a "MIOS Studio" project.hex`, so as long as the app
is named "MIOS Studio" in `/Applications`, this will work.

---

## Daily loop

```bash
make seq          # build SEQ v4 -> mios32/apps/sequencers/midibox_seq_v4/project.hex
make upload       # opens MIOS Studio with project.hex selected, hit Upload
```

Then on the SEQ v4: enter bootloader (hold the appropriate button combo while
powering on, per the SEQ v4 manual) and click Upload in MIOS Studio.

A trivial round-trip smoke test:

1. Edit a visible string in `mios32/apps/sequencers/midibox_seq_v4/core/seq_ui.c`
2. `make seq`
3. `make upload` → click Upload in MIOS Studio
4. Verify on the LCD.

### Other targets

| Command | Action |
|---|---|
| `make` / `make help` | List all targets |
| `make seq` | Build SEQ v4 |
| `make seq-clean` | Clean SEQ v4 |
| `make skeleton` | Build `app_skeleton` (fast toolchain check) |
| `make smoke` | Clean+build skeleton + SEQ v4 — local CI replacement |
| `make app APP=synthesizers/goom` | Build any app under `mios32/apps/<APP>` |
| `make new APP=my_idea` | Scaffold `mios32/apps/quickies/my_idea/` from skeleton |
| `make upload HEX=path/to.hex` | Open hex in MIOS Studio (defaults to SEQ v4) |
| `make hwcfg-rh SD=/Volumes/MBSEQ` | Copy MIDIPHY right-hand hwcfg to SD card |
| `make hwcfg-lh SD=/Volumes/MBSEQ` | Copy MIDIPHY left-hand hwcfg to SD card |

The top-level [Makefile](Makefile) exports the right `MIOS32_*` env vars inline,
so you do **not** need to `source mios32/source_me_MBHP_CORE_STM32F4` first.

---

## VS Code

`.vscode/` configures:
- **IntelliSense** ([c_cpp_properties.json](.vscode/c_cpp_properties.json)) — two
  configurations: "SEQ v4 (STM32F4)" and "app_skeleton (STM32F4)". Pick from
  the C/C++ extension's status-bar selector. Requires the `ms-vscode.cpptools`
  extension.
- **Tasks** ([tasks.json](.vscode/tasks.json)) — `Cmd+Shift+B` runs `build SEQ v4`.
  `Tasks: Run Test Task` runs `smoke`.
- **Settings** ([settings.json](.vscode/settings.json)) — hides build artifacts
  from the file tree and from search; excludes `mios8/` from search.

If "Go to Definition" lands on `mios32/mios32/STM32F4xx/mios32_board.c` for
`MIOS32_BOARD_LED_Set`, IntelliSense is wired correctly.

---

## MIDIPHY hardware notes

The SEQ v4 firmware is the **same binary** for stock SEQ v4, Wilba, and MIDIPHY
panels — the difference is the `MBSEQ_HW.V4` text file on the SD card, which
remaps buttons/LEDs/encoders to the actual hardware.

- Right-handed JA panel → `mios32/apps/sequencers/midibox_seq_v4/hwcfg/midiphy_rh/MBSEQ_HW.V4`
- Left-handed JA panel → `mios32/apps/sequencers/midibox_seq_v4/hwcfg/midiphy_lh/MBSEQ_HW.V4`

If you need to re-image the SD card or have lost the file:

```bash
make hwcfg-rh SD=/Volumes/MBSEQ    # adjust SD path to whatever your card mounts as
```

---

## Source map (SEQ v4)

Where to look when adding features:

- `mios32/apps/sequencers/midibox_seq_v4/core/app.c` — MIOS32 hooks (init, tick, MIDI rx, DIN/ENC/AIN)
- `mios32/apps/sequencers/midibox_seq_v4/core/seq_core.c` — sequencer engine, step playback
- `mios32/apps/sequencers/midibox_seq_v4/core/seq_ui.c` — UI dispatch
- `mios32/apps/sequencers/midibox_seq_v4/core/seq_ui_*.c` — individual pages (one per file)
- `mios32/apps/sequencers/midibox_seq_v4/core/seq_hwcfg.c` — runtime hwcfg parser
- `mios32/apps/sequencers/midibox_seq_v4/CHANGELOG.txt` — recent feature history

---

## Deferred

- **MIOS8 / PIC firmware** (`mios8/`) — needs SDCC + gputils; revisit if you
  want to work on old PIC hardware.
- **CLI uploader** — `make upload` currently shells out to MIOS Studio. A
  scripted bootloader sysex uploader (Python + `mido`) is doable by porting
  `mios32/tools/mios_studio/src/UploadHandler.cpp`. Worth doing if the GUI
  click becomes annoying.
- **Version control** — neither `mios32/` nor `mios8/` is a git repo here. Run
  `git init` at the workspace root (or inside `mios32/`) before serious feature
  work so you can diff against upstream.
