#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate provenance/byte completeness, then check each fixture with the C++ core."""
import json
from pathlib import Path
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, f"Duplicate JSON key: {key}")
        out[key] = value
    return out


def validate(document, executable):
    require(document["schema_version"] == 1, "Unsupported fixture schema")
    require(document["hardware_captures"] == [], "No hardware evidence exists in this milestone")
    require(len(document["examples"]) == 15, "Expected 15 complete command examples")
    ids = set()
    for example in document["examples"]:
        require(example["id"] not in ids, "Duplicate example ID")
        ids.add(example["id"])
        require(example["origin"] in {"documentation", "synthetic"}, "Unknown provenance")
        require(example["hardware_verified"] is False, "Examples cannot assert hardware validation")
        require(example["source"].startswith("https://github.com/zteifel/esphome-gecko/blob/"), "Missing source")
        require(bool(example["derivation"]), "Explain completed/derived bytes")
        require(example["address"] == 23 and example["direction"] == "controller_to_pack", "Invalid example metadata")
        require(example["at_us"] is None, "Documentation examples have no capture timestamps")
        text = example["hex"]
        require(bool(re.fullmatch(r"(?:[0-9A-F]{2}){1,128}", text)), "Incomplete or oversize hex")
        data = bytes.fromhex(text)
        require(len(data) == example["length"], "Length mismatch")
        if example["integrity"] == "xor":
            checksum = 0
            for byte in data:
                checksum ^= byte
            require(checksum == 0, "Bad XOR")
        else:
            require(example["integrity"] == "literal", "Unknown integrity policy")
        subprocess.run([executable, "fixture", text, example["kind"]], check=True)
    print(f"PASS: {len(ids)} complete, attributed, hardware-unverified fixtures")


def main():
    document = json.loads(Path(sys.argv[1]).read_text(), object_pairs_hook=no_duplicates)
    validate(document, sys.argv[2])
    # Exercise validator rejection paths independently of the core parser.
    for mutate in (
        lambda d: d["examples"][0].update(hex="17...4F"),
        lambda d: d["examples"][0].update(length=999),
        lambda d: d["examples"][0].update(hardware_verified=True),
        lambda d: d["examples"][0].update(origin="hardware_capture"),
    ):
        bad = json.loads(json.dumps(document))
        mutate(bad)
        try:
            validate(bad, sys.argv[2])
        except ValueError:
            continue
        raise ValueError("Malformed fixture unexpectedly accepted")
    print("PASS: incomplete/mislabeled fixture rejection")


if __name__ == "__main__":
    main()
