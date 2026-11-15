# SPDX-License-Identifier: MIT
"""Future approved USB configuration only. Does not flash firmware or touch Gecko GPIO."""
import argparse
import getpass
import os
from pathlib import Path
import re
import secrets
import time


def command(ssid, password, token, device):
    if not re.fullmatch(r"[\x20-\x7e]{1,32}", ssid) or not re.fullmatch(r"[\x20-\x7e]{8,63}", password):
        raise ValueError("Use printable ASCII SSID (1–32) and WPA2 password (8–63)")
    if not re.fullmatch(r"[A-Za-z0-9_-]{32,128}", token) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device):
        raise ValueError("Invalid token or device identity")
    return f"PROVISION {ssid.encode().hex()} {password.encode().hex()} {token} {device}\n".encode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB CDC port, for example COM5")
    parser.add_argument("--token-file", type=Path, default=Path("work/gecko-pico/token.txt"))
    parser.add_argument("--device-id", default="gecko-pico-sim-001")
    args = parser.parse_args()
    work = Path("work")
    work.mkdir(exist_ok=True)
    ignore = work / ".gitignore"
    if not ignore.exists():
        ignore.write_text("*\n", encoding="ascii")
    # Existing secret files are reused. No password/token on argv or terminal output.
    path = args.token_file
    if path.exists():
        token = path.read_text(encoding="ascii").strip()
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        token = secrets.token_urlsafe(32)
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="ascii") as dest:
            dest.write(token + "\n")
    payload = command(input("Wi-Fi SSID: "), getpass.getpass("WPA2 password: "), token, args.device_id)
    import serial
    with serial.Serial(args.port, 115200, timeout=0.2, write_timeout=2) as connection:
        # USB may enumerate/reset on opening. Drain banners; never print received payloads.
        time.sleep(1)
        connection.reset_input_buffer()
        connection.write(payload)
        connection.flush()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            line = connection.read_until(b"\n", size=128).strip()
            if line == b"CONFIG_SAVED":
                print("Configuration saved. Token remains in the local token file.")
                return
            if line == b"CONFIG_REJECTED":
                raise SystemExit("Configuration rejected; previous valid configuration retained where available.")
    raise SystemExit("No confirmation. Do not assume configuration succeeded; USB/flash require hardware validation.")


if __name__ == "__main__":
    main()
