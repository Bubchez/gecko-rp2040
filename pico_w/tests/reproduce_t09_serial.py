# SPDX-License-Identifier: MIT
"""Reproduce the T09 MIT CLI's fragmented-ACK defect using doubles, never USB.

Requires the existing 45437cf commit locally. This is not the historical upstream
source-extraction probe: it loads only this project's newly authored MIT tool.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import types
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pico_w/tools"))
from bench_support import SerialLink


class Clock:
    def __init__(self): self.now = 0
    def monotonic(self): self.now += 1; return self.now
    def sleep(self, delay): pass


class SerialDouble:
    def __init__(self, *args, **kwargs): self.parts = [b"CONFIG_", b"SAVED\n"]
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def reset_input_buffer(self): pass
    def write(self, data): return len(data)
    def flush(self): pass
    def read_until(self, *args, **kwargs): return self.parts.pop(0) if self.parts else b""
    def read(self, count): return self.read_until()


def main():
    os.chdir(ROOT)
    (ROOT / "work").mkdir(exist_ok=True)
    source = subprocess.check_output(["git", "show", "45437cf:pico_w/tools/provision_usb.py"], cwd=ROOT, text=True)
    legacy = types.ModuleType("t09_mit_provision")
    exec(compile(source, "T09_MIT_provision_usb.py", "exec"), legacy.__dict__)
    legacy.time = Clock()
    with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
        with mock.patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=SerialDouble)}), \
             mock.patch("builtins.input", return_value="test"), \
             mock.patch.object(legacy.getpass, "getpass", return_value="password"), \
             mock.patch.object(sys, "argv", ["provision_usb.py", "--port", "COM_TEST_DOUBLE", "--token-file", str(Path(temp) / "token")]):
            try:
                legacy.main()
            except SystemExit as error:
                print("Reproduced T09 defect:", str(error))
            else:
                raise AssertionError("Expected lost-fragment failure")
    clock = Clock()
    print("Corrected bounded SerialLink:", SerialLink(SerialDouble(), clock.monotonic).exchange(
        b"PROVISION test\n", lambda line: line == "CONFIG_SAVED", timeout=10))
    print("Both executions used only injected byte/time/serial doubles. No USB opened.")


if __name__ == "__main__":
    main()
