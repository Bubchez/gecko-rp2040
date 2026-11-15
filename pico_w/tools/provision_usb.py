# SPDX-License-Identifier: MIT
"""Low-level USB helper; prefer bench.ps1 -IsolatedPico for verified bench reports."""
import argparse
import getpass
from pathlib import Path
import re
import time
import warnings


def command(ssid, password, token, device):
    if not re.fullmatch(r"[\x20-\x7e]{1,32}", ssid) or not re.fullmatch(r"[\x20-\x7e]{8,63}", password):
        raise ValueError("Use printable ASCII SSID (1–32) and WPA2 password (8–63)")
    if not re.fullmatch(r"[A-Za-z0-9_-]{32,128}", token) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device):
        raise ValueError("Invalid token or device identity")
    return f"PROVISION {ssid.encode().hex()} {password.encode().hex()} {token} {device}\n".encode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB CDC port, for example COM5")
    parser.add_argument("--isolated-pico", action="store_true", help="Declare all Gecko wiring disconnected")
    parser.add_argument("--token-file", type=Path, default=Path("work/gecko-pico/token.txt"))
    parser.add_argument("--device-id", default="gecko-pico-sim-001")
    args = parser.parse_args()
    if not args.isolated_pico:
        raise SystemExit("Use bench.ps1 -IsolatedPico, or explicitly declare --isolated-pico.")
    work = Path("work")
    work.mkdir(exist_ok=True)
    ignore = work / ".gitignore"
    if not ignore.exists():
        ignore.write_text("*\n", encoding="ascii")
    from bench_support import Failure, SerialLink, local_token, select_port
    token = local_token(args.token_file, create=True)
    with warnings.catch_warnings():
        warnings.simplefilter("error", getpass.GetPassWarning)
        payload = command(input("Wi-Fi SSID: "), getpass.getpass("WPA2 password: "), token, args.device_id)
    import serial
    import serial.tools.list_ports
    selected = select_port(serial.tools.list_ports.comports(), args.port)
    with serial.Serial(selected, 115200, timeout=0.1, write_timeout=2) as connection:
        # USB may enumerate/reset on opening. Drain banners; never print received payloads.
        time.sleep(1)
        connection.reset_input_buffer()
        try:
            line = SerialLink(connection).exchange(payload, lambda s: s in ("CONFIG_SAVED", "CONFIG_REJECTED"), timeout=10)
            if line == "CONFIG_SAVED":
                print("Configuration saved. Token remains in the local token file.")
                return
            raise SystemExit("Configuration rejected; previous valid configuration retained where available.")
        except Failure as error:
            raise SystemExit(error.code) from None


if __name__ == "__main__":
    main()
