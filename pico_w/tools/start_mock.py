#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and start the local read-only mock on Windows, Linux or macOS."""
import argparse
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def find_core(build):
    for name in ("gecko_mock_core.exe", "gecko_mock_core"):
        for folder in (build / "Release", build):
            candidate = folder / name
            if candidate.is_file():
                return candidate
    raise FileNotFoundError("native core executable not found after build")


def provision_token(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="ascii") as stream:
            stream.write(secrets.token_urlsafe(32) + "\n")
    return path


def main(argv=None):
    if sys.version_info < (3, 10):
        raise SystemExit("Python 3.10 or later is required")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--scenario", choices=("healthy", "unavailable", "stale", "recovery", "invalid_temperature"), default="healthy")
    parser.add_argument("--core", type=Path, help="use an existing native core instead of building")
    parser.add_argument("--token-file", type=Path, help="local secret file (default: work/gecko-http/token.txt)")
    args = parser.parse_args(argv)
    work = ROOT / "work"
    work.mkdir(exist_ok=True)
    ignore = work / ".gitignore"
    if not ignore.exists():
        ignore.write_text("*\n", encoding="ascii")
    if args.core:
        core = args.core.resolve(strict=True)
    else:
        cmake = shutil.which(args.cmake)
        if not cmake:
            parser.error("CMake 3.20+ and a C++17 compiler are required; see pico_w/docs/HOST_MOCK.md")
        build = work / "gecko-http" / "build"
        subprocess.run([cmake, "-S", str(ROOT / "pico_w"), "-B", str(build),
                        "-DBUILD_TESTING=OFF", "-DGECKO_SANITIZERS=OFF", "-DCMAKE_BUILD_TYPE=Release"], check=True)
        subprocess.run([cmake, "--build", str(build), "--config", "Release"], check=True)
        core = find_core(build)
    token_path = provision_token(args.token_file.resolve() if args.token_file else work / "gecko-http" / "token.txt")
    print(f"Local bearer token file: {token_path} (token value is not logged)", flush=True)
    sys.path.insert(0, str(ROOT / "pico_w" / "src" / "host"))
    from http_mock import main as serve
    serve(["--core", str(core), "--token-file", str(token_path), "--port", str(args.port), "--scenario", args.scenario])


if __name__ == "__main__":
    main()
