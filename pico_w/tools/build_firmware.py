# SPDX-License-Identifier: MIT
"""Pinned Linux x86_64/WSL2 offline-validation build. Never accesses a USB device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tarfile
import urllib.request
from firmware_identity import build_id

ROOT = Path(__file__).resolve().parents[2]
PICO = ROOT / "pico_w"
LOCK = json.loads((PICO / "firmware/build-lock.json").read_text())
FLASH_END = 0x101FE000


def run(*args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def output(*args):
    return subprocess.check_output([str(x) for x in args], text=True).strip()


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for part in iter(lambda: source.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def download(url, path):
    temporary = path.with_suffix(path.suffix + ".partial")
    try:
        with urllib.request.urlopen(url, timeout=60) as source, temporary.open("wb") as dest:
            shutil.copyfileobj(source, dest)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(work):
    sdk = work / "pico-sdk"
    if not sdk.exists():
        run("git", "clone", "--no-checkout", "--filter=blob:none", LOCK["sdk"]["url"], sdk)
        run("git", "-C", sdk, "checkout", "--detach", LOCK["sdk"]["commit"])
    if output("git", "-C", sdk, "rev-parse", "HEAD") != LOCK["sdk"]["commit"]:
        raise RuntimeError("SDK commit mismatch; use a fresh work directory")
    for path, commit in LOCK["sdk"]["submodules"].items():
        if not (sdk / path / ".git").exists():
            run("git", "-C", sdk, "submodule", "update", "--init", "--depth", "1", path)
        if output("git", "-C", sdk / path, "rev-parse", "HEAD") != commit:
            raise RuntimeError("SDK submodule mismatch: " + path)
        if output("git", "-C", sdk / path, "status", "--porcelain", "--untracked-files=all"):
            raise RuntimeError("Modified SDK submodule: " + path)
    if output("git", "-C", sdk, "status", "--porcelain", "--untracked-files=all"):
        raise RuntimeError("Modified SDK")
    compiler = LOCK["compiler"]
    archive = work / compiler["archive"]
    if not archive.exists():
        download(compiler["url"], archive)
    if sha(archive) != compiler["sha256"]:
        raise RuntimeError("Compiler archive SHA256 mismatch")
    toolchain = work / compiler["directory"]
    if not toolchain.exists():
        with tarfile.open(archive, "r:gz") as source:
            # Python's data filter rejects escaping paths, devices and unsafe links.
            source.extractall(work, filter="data")
    gcc = toolchain / "bin/arm-none-eabi-gcc"
    if output(gcc, "-dumpfullversion") != compiler["version"]:
        raise RuntimeError("Compiler version mismatch")
    # Detect altered extracted toolchains as well as archive substitution. No extraction may overwrite user files.
    with tarfile.open(archive, "r:gz") as source:
        for member in source:
            if member.isfile():
                installed = work / member.name
                if not installed.is_file() or sha(installed) != hashlib.sha256(source.extractfile(member).read()).hexdigest():
                    raise RuntimeError("Extracted compiler mismatch: " + member.name)
    return sdk, toolchain


def source_digest():
    files = [PICO / "CMakeLists.txt"]
    for directory in ("firmware", "include", "src", "tools"):
        files += sorted(p for p in (PICO / directory).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(path.relative_to(ROOT).as_posix().encode() + b"\0" + path.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()


def uf2(binary):
    if not binary or 0x10000000 + len(binary) > FLASH_END:
        raise ValueError("Image empty or overlaps configuration reservation")
    count = (len(binary) + 255) // 256
    blocks = []
    for index in range(count):
        header = struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x2000,
                             0x10000000 + index * 256, 256, index, count, 0xE48BFF56)
        data = binary[index * 256:(index + 1) * 256].ljust(256, b"\0")
        blocks.append(header + data + bytes(220) + struct.pack("<I", 0x0AB16F30))
    return b"".join(blocks)


def inspect_image(build, toolchain):
    elf = build / "gecko_pico_sim.elf"
    symbols = output(toolchain / "bin/arm-none-eabi-nm", "-n", "-S", elf)
    values = {}
    for line in symbols.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            values[parts[-1]] = int(parts[0], 16)
    forbidden = ("i2c_", "encode_temperature", "encode_program", "encode_switch", "replace_pump_demand", "request_control", "platform_launch_core1")
    if any(any(word in line for word in forbidden) for line in symbols.splitlines()):
        raise RuntimeError("Forbidden physical/control/core1 symbol linked")
    end = values["__bss_end__"]
    heap_end = values["__HeapLimit"]
    stack_bottom, stack_top = values["__StackBottom"], values["__StackTop"]
    binary_size = values["__flash_binary_end"] - 0x10000000
    # An explicit 64 KiB unused gap beyond heap, with a 16 KiB stack, is required.
    if heap_end != values["__end__"] + 8192 or stack_top - stack_bottom != 16384 or stack_bottom - heap_end < 65536:
        raise RuntimeError("RAM/heap/stack budget failed")
    if values["__StackLimit"] != heap_end or binary_size > FLASH_END - 0x10000000:
        raise RuntimeError("Memory reservation mismatch")
    own_stack = []
    for path in build.rglob("*.su"):
        for line in path.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) == 3 and fields[1].isdigit():
                own_stack.append((int(fields[1]), fields[0].replace(str(ROOT) + "/", ""), fields[2]))
    if not own_stack or max(n for n, _, _ in own_stack) > 4096 or any(kind == "dynamic" for _, _, kind in own_stack):
        raise RuntimeError("Project per-function stack budget failed")
    return dict(flash_bytes=binary_size, main_ram_static_bytes=end - 0x20000000,
                libc_heap_bytes=8192, core0_stack_bytes=16384, scratch_stack_reservation_bytes=4096,
                unused_main_ram_gap_bytes=stack_bottom - heap_end,
                max_project_function_stack_bytes=max(n for n, _, _ in own_stack),
                largest_project_stack_frames=sorted(own_stack, reverse=True)[:10],
                caveat="Static link/per-function evidence only; SDK call chains, interrupt nesting and runtime watermarks unverified",
                forbidden_symbols_absent=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=ROOT / "work/t09")
    parser.add_argument("--build-name", default="build")
    parser.add_argument("--artifacts", type=Path, default=ROOT / "work/t09/artifacts")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
        raise RuntimeError("Pinned toolchain requires Linux x86_64 (Windows: WSL2)")
    if not args.build_name or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in args.build_name):
        raise ValueError("Invalid build directory name")
    import cmake
    import ninja
    cmake_bin = Path(cmake.CMAKE_BIN_DIR) / "cmake"
    ninja_bin = Path(ninja.BIN_DIR) / "ninja"
    if output(cmake_bin, "--version").splitlines()[0] != "cmake version " + LOCK["cmake"] or output(ninja_bin, "--version") != LOCK["ninja_version"]:
        raise RuntimeError("Install pinned firmware/build-requirements.txt in the active venv")
    work = args.work.resolve()
    # Scratch must stay within the repository's ignored work directory.
    if not work.is_relative_to(ROOT / "work"):
        raise ValueError("--work must be under repository work/")
    work.mkdir(parents=True, exist_ok=True)
    ignore = ROOT / "work/.gitignore"
    if not ignore.exists():
        ignore.write_text("*\n", encoding="ascii")
    destination = args.artifacts.resolve()
    if not (destination.is_relative_to(ROOT / "work") or destination.is_relative_to(PICO / "artifacts")):
        raise ValueError("Artifacts must stay in repository work/ or pico_w/artifacts/; no device paths")
    sdk, toolchain = prepare(work)
    build = work / args.build_name
    if args.clean and build.exists():
        shutil.rmtree(build)
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(LOCK["source_date_epoch"]),
               PICO_SDK_PATH=str(sdk), PICO_TOOLCHAIN_PATH=str(toolchain / "bin"))
    # Normalize source/tool paths, locale and timezone. No timestamp or absolute paths in UF2.
    env.update(LC_ALL="C", TZ="UTC")
    run(cmake_bin, "-S", PICO / "firmware", "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MAKE_PROGRAM=" + str(ninja_bin), env=env)
    run(cmake_bin, "--build", build, "--parallel", "2", env=env)
    budget = inspect_image(build, toolchain)
    binary = (build / "gecko_pico_sim.bin").read_bytes()
    if build_id().encode("ascii") + b"\0" not in binary:
        raise RuntimeError("Compiled firmware identity does not match current inputs")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    image = args.artifacts / "gecko_pico_w_SIMULATION_NO_I2C.uf2"
    image.write_bytes(uf2(binary))
    manifest = dict(format=1, firmware="0.2.1-pico-sim", firmware_build_id=build_id(), api="1.0.0", purpose="offline-reviewed simulation only; not hardware validated",
                    source_tree_sha256=source_digest(), uf2_sha256=sha(image), uf2_bytes=image.stat().st_size,
                    inputs=LOCK, memory=budget, physical_i2c=False, actuator_execution=False, gecko_gpio="inactive")
    (args.artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.artifacts / "SHA256SUMS").write_text(sha(image) + "  " + image.name + "\n")
    print(json.dumps(dict(uf2_sha256=manifest["uf2_sha256"], memory=budget), indent=2))


if __name__ == "__main__":
    main()
