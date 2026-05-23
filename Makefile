# Top-level orchestration for MIOS32 builds (MBHP_CORE_STM32F4).
# Sets MIOS32_* env vars inline so per-app Makefiles work without `source`ing
# mios32/source_me_MBHP_CORE_STM32F4 first.

export MIOS32_PATH       := $(CURDIR)/mios32
export MIOS32_BIN_PATH   := $(MIOS32_PATH)/bin
export MIOS32_FAMILY     := STM32F4xx
export MIOS32_PROCESSOR  := STM32F407VG
export MIOS32_BOARD      := MBHP_CORE_STM32F4
export MIOS32_LCD        := universal
export MIOS32_GCC_PREFIX := arm-none-eabi

SEQ_DIR      := mios32/apps/sequencers/midibox_seq_v4
SKELETON_DIR := mios32/apps/templates/app_skeleton
QUICKIES_DIR := mios32/apps/quickies

# midiphy hwcfg lives inside the SEQ v4 app
HWCFG_DIR := $(SEQ_DIR)/hwcfg

.PHONY: help seq seq-clean skeleton skeleton-clean smoke new app app-clean \
        hwcfg-rh hwcfg-lh upload toolchain-check \
        test test-discover test-setup

help:
	@echo "Targets:"
	@echo "  seq            Build MIDIbox SEQ v4 (STM32F4) -> $(SEQ_DIR)/project.hex"
	@echo "  seq-clean      Clean SEQ v4 build artifacts"
	@echo "  skeleton       Build app_skeleton (toolchain smoke)"
	@echo "  smoke          Clean+build SEQ v4 and skeleton (local CI)"
	@echo "  new APP=name   Scaffold a new app under $(QUICKIES_DIR)/<name>"
	@echo "  app APP=path   Build any app under mios32/apps/<path>"
	@echo "  app-clean APP=path"
	@echo "  hwcfg-rh SD=/Volumes/MBSEQ   Copy MIDIPHY right-hand hwcfg to SD card"
	@echo "  hwcfg-lh SD=/Volumes/MBSEQ   Copy MIDIPHY left-hand hwcfg to SD card"
	@echo "  upload HEX=path/to.hex       Open MIOS Studio with the hex selected"
	@echo "  toolchain-check              Verify arm-none-eabi-gcc is available"
	@echo "  test-setup                   Create tests/.venv and install pytest+rtmidi"
	@echo "  test-discover                List MIDI ports and PING the connected board"
	@echo "  test                         Run the pytest hardware-in-the-loop suite"

toolchain-check:
	@command -v $(MIOS32_GCC_PREFIX)-gcc >/dev/null 2>&1 || { \
	  echo "error: $(MIOS32_GCC_PREFIX)-gcc not in PATH"; \
	  echo "install: brew install --cask gcc-arm-embedded"; \
	  exit 1; }
	@$(MIOS32_GCC_PREFIX)-gcc --version | head -1

seq: toolchain-check
	$(MAKE) -C $(SEQ_DIR)

seq-clean:
	$(MAKE) -C $(SEQ_DIR) cleanall

skeleton: toolchain-check
	$(MAKE) -C $(SKELETON_DIR)

skeleton-clean:
	$(MAKE) -C $(SKELETON_DIR) cleanall

smoke: seq-clean skeleton-clean skeleton seq
	@echo "smoke: ok"

# Build arbitrary app: `make app APP=synthesizers/goom`
app: toolchain-check
	@test -n "$(APP)" || { echo "usage: make app APP=<relpath under apps/>"; exit 1; }
	$(MAKE) -C apps/$(APP)

app-clean:
	@test -n "$(APP)" || { echo "usage: make app-clean APP=<relpath under apps/>"; exit 1; }
	$(MAKE) -C apps/$(APP) cleanall

new:
	@test -n "$(APP)" || { echo "usage: make new APP=<name>"; exit 1; }
	../tools/new-app.sh "$(APP)"

# SD card hwcfg helpers
hwcfg-rh:
	@test -n "$(SD)" || { echo "usage: make hwcfg-rh SD=/Volumes/<sdname>"; exit 1; }
	@test -d "$(SD)" || { echo "error: $(SD) not mounted"; exit 1; }
	cp $(HWCFG_DIR)/midiphy_rh/MBSEQ_HW.V4 "$(SD)/MBSEQ_HW.V4"
	@echo "wrote $(SD)/MBSEQ_HW.V4 (midiphy_rh)"

hwcfg-lh:
	@test -n "$(SD)" || { echo "usage: make hwcfg-lh SD=/Volumes/<sdname>"; exit 1; }
	@test -d "$(SD)" || { echo "error: $(SD) not mounted"; exit 1; }
	cp $(HWCFG_DIR)/midiphy_lh/MBSEQ_HW.V4 "$(SD)/MBSEQ_HW.V4"
	@echo "wrote $(SD)/MBSEQ_HW.V4 (midiphy_lh)"

TESTS_DIR := tests
TESTS_VENV := $(TESTS_DIR)/.venv
TESTS_PY := $(TESTS_VENV)/bin/python

test-setup:
	@test -d $(TESTS_VENV) || python3 -m venv $(TESTS_VENV)
	$(TESTS_PY) -m pip install --quiet --upgrade pip
	$(TESTS_PY) -m pip install --quiet -e $(TESTS_DIR)
	@echo "tests venv ready at $(TESTS_VENV)"

test-discover:
	@test -x $(TESTS_PY) || { echo "run 'make test-setup' first"; exit 1; }
	cd $(TESTS_DIR) && ../$(TESTS_PY) discover.py

test:
	@test -x $(TESTS_PY) || { echo "run 'make test-setup' first"; exit 1; }
	cd $(TESTS_DIR) && ../$(TESTS_PY) -m pytest

# Open MIOS Studio with the hex selected. Default to SEQ v4 hex.
upload:
	@hex="$(HEX)"; \
	  [ -n "$$hex" ] || hex="$(SEQ_DIR)/project.hex"; \
	  test -f "$$hex" || { echo "error: $$hex not found (run make seq first)"; exit 1; }; \
	  echo "opening MIOS Studio with $$hex"; \
	  open -a "MIOS_Studio" "$$hex"
