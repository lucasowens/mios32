"""Pytest plugin + importable shim: log every testctrl SysEx command sent.
Activate in pytest with `-p log_traffic_plugin` (rootdir tests/), or import
and call `install(path)` from a plain script. Writes one line per outbound
frame: monotonic-ms + hex bytes."""
import os
import time

_LOG_PATH = os.environ.get("TESTCTRL_LOG", "/tmp/testctrl_traffic.log")


def install(path: str | None = None) -> None:
    from harness.board import Board

    log_path = path or _LOG_PATH
    if getattr(Board, "_traffic_patched", False):
        return
    orig = Board.send_raw
    t0 = time.monotonic()

    def send_raw(self, data):
        with open(log_path, "a") as f:
            f.write(f"{(time.monotonic()-t0)*1000:9.1f}  {bytes(data).hex()}\n")
        return orig(self, data)

    Board.send_raw = send_raw
    Board._traffic_patched = True


def pytest_configure(config):
    install()
