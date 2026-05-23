"""CLI tool: list MIDI ports and try to find the SEQ V4. Run via `make test-discover`."""

import sys

from harness import Board, BoardNotFound
from harness.board import list_ports


def main() -> int:
    ports = list_ports()
    print("MIDI inputs:")
    for name in ports["inputs"]:
        print(f"  {name}")
    print("MIDI outputs:")
    for name in ports["outputs"]:
        print(f"  {name}")
    print()
    try:
        with Board() as b:
            print(f"matched IN  -> {b.input_port_name}")
            print(f"matched OUT -> {b.output_port_name}")
            print("sending PING...")
            try:
                payload = b.ping(timeout=2.0)
                print(f"PONG payload: {payload!r}")
                return 0
            except TimeoutError as e:
                print(f"no reply: {e}")
                return 2
    except BoardNotFound as e:
        print(f"board not found: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
