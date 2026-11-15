# SPDX-License-Identifier: MIT
"""Digest of native firmware inputs; no generated files, secrets or build paths."""
import hashlib
from pathlib import Path

PICO = Path(__file__).resolve().parents[1]


def inputs():
    files = [Path(__file__).resolve()]
    for directory in ("firmware", "include", "src/core", "src/session", "src/transport", "src/simulation"):
        files += [p for p in (PICO / directory).rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    return sorted(files)


def build_id():
    digest = hashlib.sha256()
    for path in inputs():
        digest.update(path.relative_to(PICO).as_posix().encode() + b"\0" + path.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()


if __name__ == "__main__":
    print(build_id())
