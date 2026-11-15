# SPDX-License-Identifier: MIT
"""Bounded bench I/O and verification. No flashing/device-discovery side effects on import."""
import hashlib
import socket
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PICO = ROOT / "pico_w"
sys.path.insert(0, str(PICO / "src/host"))
from bridge import strict_json
from firmware_identity import build_id

IMAGE = "gecko_pico_w_SIMULATION_NO_I2C.uf2"
VERSION = "0.2.1-pico-sim"
CODE = frozenset(("ok", "artifact_invalid", "source_mismatch", "isolation_required", "dependency_missing",
                  "no_pico", "ambiguous_pico", "wrong_port", "usb_open_failed", "usb_write_failed",
                  "usb_timeout", "usb_framing", "firmware_update_required", "identity_mismatch",
                  "invalid_info", "local_token_missing", "local_token_invalid", "credentials_invalid",
                  "provision_rejected", "wifi_timeout", "http_unavailable", "http_framing", "http_json",
                  "http_contract", "unexpected_status", "control_not_denied", "stale_state",
                  "reconnect_rejected", "reconnect_not_observed", "unexpected_error", "interrupted",
                  "report_write_failed", "bootstrap_failed"))


class Failure(Exception):
    def __init__(self, code):
        self.code = code if code in CODE else "unexpected_error"
        super().__init__(self.code)


def verify_artifact(folder):
    """Check repository inputs + UF2 bytes/structure before any USB or Wi-Fi I/O."""
    try:
        path = folder / IMAGE
        size = path.stat().st_size
        if size < 512 or size > 4 * 1024 * 1024 or size % 512:
            raise ValueError()
        manifest_path = folder / "manifest.json"
        if manifest_path.stat().st_size > 16384:
            raise ValueError()
        manifest = strict_json(manifest_path.read_bytes())
        expected = manifest["uf2_sha256"]
        if not re.fullmatch(r"[a-f0-9]{64}", expected):
            raise ValueError()
        checksum_path = folder / "SHA256SUMS"
        if checksum_path.stat().st_size > 256 or checksum_path.read_text().strip() != expected + "  " + IMAGE:
            raise ValueError()
        if manifest["format"] != 1 or manifest["firmware"] != VERSION or manifest["api"] != "1.0.0":
            raise ValueError()
        if manifest["physical_i2c"] is not False or manifest["actuator_execution"] is not False or manifest["gecko_gpio"] != "inactive":
            raise ValueError()
        if manifest["inputs"] != strict_json((PICO / "firmware/build-lock.json").read_bytes()):
            raise ValueError()
        if manifest["uf2_bytes"] != size or not re.fullmatch(r"[a-f0-9]{64}", manifest["firmware_build_id"]):
            raise ValueError()
        digest = hashlib.sha256()
        blocks = size // 512
        with path.open("rb") as source:
            for index in range(blocks):
                block = source.read(512)
                digest.update(block)
                header = struct.unpack("<8I", block[:32])
                if header != (0x0A324655, 0x9E5D5157, 0x2000, 0x10000000 + index * 256, 256, index, blocks, 0xE48BFF56):
                    raise ValueError()
                if block[288:508] != bytes(220) or struct.unpack("<I", block[-4:])[0] != 0x0AB16F30:
                    raise ValueError()
                if header[3] + 256 > 0x101FE000:
                    raise ValueError()
        if digest.hexdigest() != expected:
            raise ValueError()
    except (ValueError, KeyError, TypeError, OSError, struct.error):
        raise Failure("artifact_invalid") from None
    # A reported ID is correlation evidence, not flash readback/attestation.
    if manifest["firmware_build_id"] != build_id():
        raise Failure("source_mismatch")
    return manifest


def select_port(ports, requested=None):
    candidates = [p for p in ports if p.vid == 0x2E8A and p.pid == 0x000A]
    if requested:
        candidates = [p for p in candidates if p.device.lower() == requested.lower()]
        if not candidates:
            raise Failure("wrong_port")
    if not candidates:
        raise Failure("no_pico")
    if len(candidates) != 1:
        raise Failure("ambiguous_pico")
    return candidates[0].device


class SerialLink:
    """Carry partial read timeouts across chunks; a timeout is not a line boundary."""
    def __init__(self, port, clock=time.monotonic):
        self.port, self.clock = port, clock
        self.buffer = bytearray()

    def exchange(self, payload, accepts, timeout=5):
        try:
            if not payload.endswith(b"\n") or len(payload) > 513 or self.port.write(payload) != len(payload):
                raise Failure("usb_write_failed")
            self.port.flush()
            deadline = self.clock() + timeout
            total = lines = 0
            while self.clock() < deadline:
                while b"\n" in self.buffer:
                    raw, _, rest = self.buffer.partition(b"\n")
                    self.buffer[:] = rest
                    lines += 1
                    if len(raw) > 512 or lines > 16:
                        raise Failure("usb_framing")
                    try:
                        line = raw.rstrip(b"\r").decode("ascii")
                    except UnicodeError:
                        raise Failure("usb_framing") from None
                    if accepts(line):
                        return line
                chunk = self.port.read(64)
                total += len(chunk)
                self.buffer.extend(chunk)
                if len(self.buffer) > 1024 or total > 8192 or (b"\n" not in self.buffer and len(self.buffer) > 512):
                    raise Failure("usb_framing")
        except Failure:
            raise
        except Exception:
            raise Failure("usb_open_failed") from None
        raise Failure("usb_timeout")

    def info(self, manifest):
        line = self.exchange(b"INFO\n", lambda s: s.startswith("{") or s == "CONFIG_REJECTED")
        if line == "CONFIG_REJECTED":
            raise Failure("firmware_update_required")
        try:
            info = strict_json(line)
            if info["schema_version"] != 1 or info["firmware_version"] != manifest["firmware"] or info["build_id"] != manifest["firmware_build_id"]:
                raise Failure("identity_mismatch")
            if not re.fullmatch(r"[0-9a-f]{32}", info["boot_id"]) or not re.fullmatch(r"[A-Za-z0-9_-]{0,64}", info["device_id"]):
                raise ValueError()
            if type(info["provisioned"]) is not bool or (info["provisioned"] and not info["device_id"]):
                raise ValueError()
            if info["wifi"] not in ("unprovisioned", "connecting", "online", "backoff") or info["port"] != 8765:
                raise ValueError()
            if info["mode"] != "simulation" or info["physical_i2c"] is not False or info["actuator_execution"] is not False or info["gpio"] != "inactive":
                raise ValueError()
            ipaddress.IPv4Address(info["ip"])
            for key in ("wifi_attempts", "wifi_reconnects"):
                if type(info[key]) is not int or not 0 <= info[key] <= 2**32 - 1:
                    raise ValueError()
            return info
        except Failure:
            raise
        except (ValueError, KeyError, TypeError):
            raise Failure("invalid_info") from None


def local_token(path, create=False):
    try:
        if not path.exists():
            if not create:
                raise Failure("local_token_missing")
            path.parent.mkdir(parents=True, exist_ok=True)
            token = secrets.token_urlsafe(32)
            descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "w", encoding="ascii") as dest:
                dest.write(token + "\n")
        if path.stat().st_size > 130:
            raise ValueError()
        token = path.read_text(encoding="ascii").strip()
        if not re.fullmatch(r"[A-Za-z0-9_-]{32,128}", token):
            raise ValueError()
        return token
    except Failure:
        raise
    except (OSError, ValueError):
        raise Failure("local_token_invalid") from None


def migrate_token(source, destination):
    if destination.exists() or not source.exists():
        return False
    value = local_token(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="ascii") as dest:
            dest.write(value + "\n")
    except FileExistsError:
        return False
    except OSError:
        raise Failure("local_token_invalid") from None
    return True


def bench_ip(value):
    try:
        address = ipaddress.IPv4Address(value)
        if not any(address in ipaddress.IPv4Network(n) for n in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16")):
            raise ValueError()
        return str(address)
    except (ValueError, TypeError):
        raise Failure("invalid_info") from None


class Http:
    """Literal USB-reported private IPv4 only; no DNS, redirects or environment proxies."""
    def __init__(self, address, token, clock=time.monotonic, sleep=time.sleep):
        self.address, self.token = bench_ip(address), token
        self.clock, self.sleep, self.last = clock, sleep, float("-inf")

    def request(self, path, method="GET", body=None, auth=True, headers=None):
        self.sleep(max(0, 0.3 - (self.clock() - self.last)))
        self.last = self.clock()
        values = {"Authorization": "Bearer " + self.token} if auth else {}
        if body is not None:
            values["Content-Type"] = "application/json"
        values.update(headers or {})
        connection = None
        try:
            values.update({"Host": self.address + ":8765", "Connection": "close"})
            if body is not None:
                values["Content-Length"] = str(len(body))
            request = (f"{method} {path} HTTP/1.1\r\n" + "".join(f"{k}: {v}\r\n" for k, v in values.items()) + "\r\n").encode("ascii") + (body or b"")
            deadline = self.clock() + 3
            connection = socket.create_connection((self.address, 8765), timeout=3)
            remaining = deadline - self.clock()
            if remaining <= 0:
                raise Failure("http_unavailable")
            connection.settimeout(remaining)
            connection.sendall(request)
            data = bytearray()
            header_end = None
            while header_end is None:
                remaining = deadline - self.clock()
                if remaining <= 0:
                    raise Failure("http_unavailable")
                connection.settimeout(remaining)
                part = connection.recv(512)
                if not part:
                    raise Failure("http_framing")
                data.extend(part)
                end = data.find(b"\r\n\r\n")
                if end >= 0:
                    header_end = end + 4
                if (header_end if header_end is not None else len(data)) > 2048:
                    raise Failure("http_framing")
            try:
                lines = bytes(data[:header_end - 4]).decode("ascii").split("\r\n")
                if not re.fullmatch(r"HTTP/1\.1 [1-5][0-9]{2} [\x20-\x7e]*", lines[0]):
                    raise ValueError()
                status = int(lines[0].split(" ")[1])
                meta = {}
                for line in lines[1:]:
                    key, value = line.split(":", 1)
                    key = key.lower()
                    if not re.fullmatch(r"[!#$%&'*+.^_`|~0-9a-z-]+", key) or key in meta or any((ord(c) < 32 and c != "\t") or ord(c) > 126 for c in value):
                        raise ValueError()
                    meta[key] = value.strip()
                if not re.fullmatch(r"[0-9]{1,5}", meta.get("content-length", "")):
                    raise ValueError()
                size = int(meta["content-length"])
                if size > 8192 or "transfer-encoding" in meta or meta.get("content-type") != "application/json" or meta.get("cache-control") != "no-store" or meta.get("connection", "").lower() != "close":
                    raise ValueError()
            except (ValueError, UnicodeError):
                raise Failure("http_framing") from None
            del data[:header_end]
            while len(data) < size:
                remaining = deadline - self.clock()
                if remaining <= 0:
                    raise Failure("http_unavailable")
                connection.settimeout(remaining)
                part = connection.recv(min(1024, size - len(data)))
                if not part:
                    raise Failure("http_framing")
                data.extend(part)
            if len(data) != size:
                raise Failure("http_framing")
            try:
                document = strict_json(bytes(data))
            except (ValueError, UnicodeError, RecursionError):
                raise Failure("http_json") from None
            return status, meta, document
        except Failure:
            raise
        except Exception:
            raise Failure("http_unavailable") from None
        finally:
            if connection:
                connection.close()
