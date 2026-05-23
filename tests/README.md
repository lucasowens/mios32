# MIDIbox hardware-in-the-loop tests

Pytest-based harness that drives a real MIDIbox board over USB MIDI. SEQ V4 is
the first app under coverage; other MIOS32 apps will live alongside it under
`apps/`.

## Setup

```sh
# from repo root:
python3 -m venv tests/.venv
. tests/.venv/bin/activate
pip install -e tests
```

## Day-to-day

```sh
make test-discover    # list MIDI ports + send a PING to the board
make test             # run the full pytest suite
```

Tests are decorated `@pytest.mark.hardware`; if the board isn't connected, the
session-scoped `board` fixture calls `pytest.skip()` and the suite continues.

## Layout

```
tests/
├── pyproject.toml
├── conftest.py             # session-scoped board fixture
├── discover.py             # `make test-discover` entry point
├── harness/
│   ├── board.py            # USB MIDI connection + SysEx round-trip
│   └── sysex.py            # wire-format constants (mirror seq_testctrl.c)
└── apps/seq_v4/
    └── test_smoke.py       # PING smoke test
```

## Adding a command

1. Add the command byte + handler to `mios32/apps/sequencers/midibox_seq_v4/core/seq_testctrl.c`.
2. Mirror the constant in `tests/harness/sysex.py`.
3. Add a helper to `Board` in `tests/harness/board.py`.
4. Write a test under `tests/apps/seq_v4/`.
