# SPDX-License-Identifier: MIT
"""Isolated Pico simulation bench runner. Never flashes or accesses Gecko."""
import argparse
from datetime import datetime, timezone
import getpass
import json
import os
from pathlib import Path
import sys
import time
import warnings

from bench_support import CODE, Failure, Http, PICO, ROOT, SerialLink, local_token, migrate_token, select_port, verify_artifact
from provision_usb import command as provision_command

STAGES = ("artifact", "usb_identity", "provisioning", "wifi_online", "http_identity", "http_authentication",
          "http_read_only", "http_malformed", "status_freshness", "diagnostics", "wifi_reconnection", "post_recovery")
METRICS = frozenset(("checks", "samples", "revision_advanced", "ready", "unavailable_fields", "wifi_attempts_delta",
                     "wifi_reconnects_delta", "offline_observed", "same_boot", "simulation_advanced", "memory_measured",
                     "provisioned_now", "token_migrated", "denied_commands_delta", "unauthorized_delta"))
UNVERIFIED = ("electrical_gpio_levels", "flash_readback_attestation", "heap_stack_irq_watermarks",
              "real_access_point_outage", "flash_power_loss_endurance", "windows_usb_if_not_run_on_windows",
              "all_gecko_electrical_bus_protocol_behaviour")


class Report:
    """An allowlist projection, never a dump/scrub of serial, HTTP or exceptions."""
    def __init__(self, isolated=False):
        self.data = dict(report_version="1.0", tool_version="0.1.0", started_at=datetime.now(timezone.utc).isoformat(),
                         platform="windows" if os.name == "nt" else "linux" if sys.platform.startswith("linux") else "other",
                         overall="blocked", blocker="ok", isolation_declared=bool(isolated), hardware_accessed=False,
                         artifact=None, checks=[dict(id=s, status="not_run", code="ok", duration_ms=0, metrics={}) for s in STAGES],
                         unverified=list(UNVERIFIED))

    def step(self, name, operation):
        entry = next(c for c in self.data["checks"] if c["id"] == name)
        start = time.monotonic()
        try:
            metrics = operation() or {}
            if any(k not in METRICS or type(v) not in (bool, int) or (type(v) is int and not 0 <= v <= 2**32 - 1) for k, v in metrics.items()):
                raise Failure("unexpected_error")
            entry.update(status="pass", metrics=metrics)
        except BaseException as error:
            code = error.code if isinstance(error, Failure) else "interrupted" if isinstance(error, KeyboardInterrupt) else "unexpected_error"
            entry.update(status="fail", code=code)
            raise Failure(code) from None
        finally:
            entry["duration_ms"] = min(2**32 - 1, max(0, int((time.monotonic() - start) * 1000)))
            print(entry["id"] + ": " + entry["status"])

    def artifact(self, manifest):
        # Only exact known version/build data from the locally verified artifact.
        self.data["artifact"] = dict(sha256=manifest["uf2_sha256"], build_id=manifest["firmware_build_id"],
                                     firmware_version=manifest["firmware"], uf2_bytes=manifest["uf2_bytes"])

    def fail(self, code):
        self.data.update(overall="fail", blocker=code if code in CODE else "unexpected_error")

    def save(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(".partial")
        temporary.write_text(json.dumps(self.data, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(path)


def require(condition, code="http_contract"):
    if not condition:
        raise Failure(code)


class Bench:
    def __init__(self, link, manifest, token_path, reprovision=False, clock=time.monotonic, sleep=time.sleep,
                 client_factory=Http, prompt=None):
        self.link, self.manifest, self.token_path, self.reprovision = link, manifest, token_path, reprovision
        self.clock, self.sleep, self.client_factory = clock, sleep, client_factory
        self.prompt = prompt or self.credentials_prompt
        self.info = self.http = self.token = self.before = self.first_status = None
        from jsonschema import Draft202012Validator, FormatChecker
        spec = json.loads((PICO / "api/openapi-v1.json").read_text())
        self.validators = {name: Draft202012Validator({"$ref": "#/components/schemas/" + name, "components": spec["components"]},
                                                     format_checker=FormatChecker()) for name in ("Health", "Capabilities", "Status", "Diagnostics", "Error")}

    @staticmethod
    def credentials_prompt():
        # A noninteractive console must fail rather than falling back to echoed secrets.
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            return input("Wi-Fi SSID: "), getpass.getpass("WPA2 password: ")

    def validate(self, name, document):
        if not self.validators[name].is_valid(document):
            raise Failure("http_contract")

    def identity(self):
        self.info = self.link.info(self.manifest)

    def provision(self):
        needed = not self.info["provisioned"] or self.reprovision
        migrated = not needed and migrate_token(ROOT / "work/gecko-pico/token.txt", self.token_path)
        self.token = local_token(self.token_path, create=needed)
        if needed:
            try:
                ssid, password = self.prompt()
                payload = provision_command(ssid, password, self.token, "gecko-pico-sim-001")
            except (ValueError, EOFError, getpass.GetPassWarning):
                raise Failure("credentials_invalid") from None
            answer = self.link.exchange(payload, lambda s: s in ("CONFIG_SAVED", "CONFIG_REJECTED"), timeout=10)
            require(answer == "CONFIG_SAVED", "provision_rejected")
            self.info = self.link.info(self.manifest)
            require(self.info["provisioned"], "provision_rejected")
        return dict(provisioned_now=needed, token_migrated=bool(migrated))

    def online(self):
        deadline = self.clock() + 180
        original_boot = self.info["boot_id"]
        while self.clock() < deadline:
            info = self.link.info(self.manifest)
            require(info["boot_id"] == original_boot, "identity_mismatch")
            if info["wifi"] == "online":
                self.info = info
                self.http = self.client_factory(info["ip"], self.token)
                return
            self.sleep(1)
        raise Failure("wifi_timeout")

    def get(self, route, schema):
        status, _, data = self.http.request("/api/v1/" + route, auth=route != "health")
        if status == 503:
            raise Failure("http_unavailable")
        if status == 401 and route != "health":
            raise Failure("local_token_invalid")
        require(status == 200, "unexpected_status")
        self.validate(schema, data)
        require(data["boot_id"] == self.info["boot_id"], "identity_mismatch")
        return data

    def http_identity(self):
        # Radio can become online before maintenance has caught up after SDK init.
        deadline = self.clock() + 10
        while True:
            try:
                health = self.get("health", "Health")
                caps = self.get("capabilities", "Capabilities")
                break
            except Failure as error:
                if error.code != "http_unavailable" or self.clock() >= deadline:
                    raise
                self.sleep(1)
        require(health["live"] is True)
        require(caps.get("firmware_build_id") == self.manifest["firmware_build_id"] and caps["firmware_version"] == self.manifest["firmware"], "identity_mismatch")
        require(caps["device_id"] == self.info["device_id"], "identity_mismatch")
        require(caps["transport"] == "simulated" and caps["mode"] == "simulation" and caps["read_only"] is True and caps["physical_control_enabled"] is False)
        require(all(v["supported"] is False and v["enabled"] is False for v in caps["commands"].values()))
        self.before = self.get("diagnostics", "Diagnostics")
        return dict(checks=3)

    def expect(self, response, status, code):
        actual, headers, data = response
        if 200 <= actual < 300 and code == "read_only":
            raise Failure("control_not_denied")
        require(actual == status, "unexpected_status")
        self.validate("Error", data)
        require(data["error"]["code"] == code)
        return headers

    def authentication(self):
        for options in (dict(auth=False), dict(headers={"Authorization": "Bearer deliberately-invalid"})):
            headers = self.expect(self.http.request("/api/v1/status", **options), 401, "unauthorized")
            require(headers.get("www-authenticate") == "Bearer")
        return dict(checks=2)

    def read_only(self):
        for intent in ("light.set", "temperature.set", "pump.set", "program.set", "unknown"):
            body = json.dumps(dict(request_id="isolated-bench", command=intent), separators=(",", ":")).encode()
            self.expect(self.http.request("/api/v1/commands", "POST", body), 403, "read_only")
        self.expect(self.http.request("/api/v1/commands/isolated-bench"), 404, "unknown_request")
        return dict(checks=6)

    def malformed(self):
        for body, status, code in ((b'{"a":1,"a":2}', 400, "invalid_json"), (b'[]', 422, "invalid_parameters"),
                                   (b'{"a":1e999}', 400, "invalid_json"), (b" " * 513, 413, "body_too_large")):
            self.expect(self.http.request("/api/v1/commands", "POST", body), status, code)
        self.expect(self.http.request("/api/v1/commands", "POST", b"{}", headers={"Content-Type": "text/plain"}), 415, "unsupported_media_type")
        self.expect(self.http.request("/api/v1/status?x=1"), 400, "invalid_http")
        return dict(checks=6)

    def status(self):
        data = self.get("status", "Status")
        require(data["device_id"] == self.info["device_id"], "identity_mismatch")
        require(data["transport"] == "simulated" and data["mode"] == "simulation" and data["quality"]["source"] == "synthetic")
        require(data["connection"]["ready"] is True and data["quality"]["valid"] is True and data["quality"]["stale"] is False, "stale_state")
        age, completed = data["connection"]["last_status_age_ms"], data["connection"]["status_completed_age_ms"]
        require(type(age) is int and type(completed) is int and completed <= age < data["connection"]["stale_after_ms"], "stale_state")
        from http_mock import UNAVAILABLE
        require(data["quality"]["unavailable_fields"] == UNAVAILABLE)
        for pointer in UNAVAILABLE:
            value = data["state"]
            for key in pointer.split("/")[1:]:
                value = value[key]
            require(value is None)
        return data

    def freshness(self):
        first = self.status()
        self.sleep(2)
        second = self.status()
        require(second["state_revision"] > first["state_revision"], "stale_state")
        self.first_status = second
        return dict(samples=2, revision_advanced=True, ready=True, unavailable_fields=13)

    def diagnostics(self):
        data = self.get("diagnostics", "Diagnostics")
        safety = data.get("safety", {})
        require(safety.get("physical_i2c_compiled") is False and safety.get("actuator_execution_compiled") is False and safety.get("gecko_gpio") == "inactive")
        require(data.get("wifi", {}).get("state") == "online")
        require(data["counters"]["queue_overflows"] == 0 and data["counters"]["invalid_rx"] == 0)
        denied = data["http"]["denied_commands"] - self.before["http"]["denied_commands"]
        unauthorized = data["http"]["unauthorized"] - self.before["http"]["unauthorized"]
        require(denied >= 5 and unauthorized >= 2)
        return dict(denied_commands_delta=denied, unauthorized_delta=unauthorized,
                    memory_measured=all(v is not None for v in data["memory"].values()))

    def reconnect(self):
        before = self.link.info(self.manifest)
        baseline = self.get("diagnostics", "Diagnostics")
        answer = self.link.exchange(b"WIFI_RECONNECT\n", lambda s: s in ("RECONNECTING", "RECONNECT_REJECTED"))
        require(answer == "RECONNECTING", "reconnect_rejected")
        current = self.link.info(self.manifest)
        offline = current["wifi"] != "online"
        self.online()
        after = self.info
        require(offline, "reconnect_not_observed")
        require(before["boot_id"] == after["boot_id"] and before["device_id"] == after["device_id"], "identity_mismatch")
        attempts, reconnects = after["wifi_attempts"] - before["wifi_attempts"], after["wifi_reconnects"] - before["wifi_reconnects"]
        require(attempts >= 1 and reconnects >= 1, "reconnect_not_observed")
        self.recovery_baseline = baseline
        return dict(offline_observed=True, same_boot=True, wifi_attempts_delta=attempts, wifi_reconnects_delta=reconnects)

    def recovery(self):
        self.http_identity()  # Bounded post-association maintenance/cache grace.
        state = self.status()
        diagnostics = self.get("diagnostics", "Diagnostics")
        require(state["state_revision"] >= self.first_status["state_revision"], "stale_state")
        require(diagnostics["simulation_time_ms"] > self.recovery_baseline["simulation_time_ms"])
        require(diagnostics["counters"]["statuses"] >= self.recovery_baseline["counters"]["statuses"])
        require(all(diagnostics["counters"][key] >= value for key, value in self.recovery_baseline["counters"].items()))
        return dict(ready=True, same_boot=True, simulation_advanced=True)

    def run(self, report):
        for name, method in (("usb_identity", self.identity), ("provisioning", self.provision), ("wifi_online", self.online),
                             ("http_identity", self.http_identity), ("http_authentication", self.authentication),
                             ("http_read_only", self.read_only), ("http_malformed", self.malformed), ("status_freshness", self.freshness),
                             ("diagnostics", self.diagnostics), ("wifi_reconnection", self.reconnect), ("post_recovery", self.recovery)):
            report.step(name, method)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--isolated-pico", action="store_true", help="Declare Pico disconnected from all Gecko equipment")
    mode.add_argument("--verify-only", action="store_true", help="No hardware access; default")
    parser.add_argument("--port", help="Optional disambiguation; must match an enumerated Pico VID/PID")
    parser.add_argument("--reprovision", action="store_true", help="Explicitly replace local Wi-Fi configuration; never firmware")
    parser.add_argument("--artifact-dir", type=Path, default=PICO / "artifacts/t09")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    report = Report(args.isolated_pico)
    work = ROOT / "work/pico-bench"
    path = args.report or work / "reports" / ("bench-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".json")
    port = None
    result = 1
    try:
        require(path.resolve().is_relative_to((ROOT / "work").resolve()), "report_write_failed")
        (ROOT / "work").mkdir(exist_ok=True)
        ignore = ROOT / "work/.gitignore"
        if not ignore.exists():
            ignore.write_text("*\n", encoding="ascii")
        manifest = {}
        def artifact():
            manifest.update(verify_artifact(args.artifact_dir))
            report.artifact(manifest)
        report.step("artifact", artifact)
        if not args.isolated_pico:
            report.data["overall"] = "verified_only"
        else:
            try:
                import serial
                import serial.tools.list_ports
                import jsonschema  # noqa: F401: prerequisite before any device access
            except ImportError:
                raise Failure("dependency_missing") from None
            chosen = select_port(serial.tools.list_ports.comports(), args.port)
            try:
                port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2, rtscts=False, dsrdtr=False)
                port.dtr, port.rts = True, False  # SDK CDC stdio requires DTR; no baud reset/BOOTSEL command.
                port.port = chosen
                report.data["hardware_accessed"] = None  # An unsuccessful open may have partially touched USB.
                port.open()
                report.data["hardware_accessed"] = True
                time.sleep(0.25)
                port.reset_input_buffer()
            except Exception:
                raise Failure("usb_open_failed") from None
            Bench(SerialLink(port), manifest, work / "private/token.txt", args.reprovision).run(report)
            report.data["overall"] = "pass"
        result = 0
    except BaseException as error:
        report.fail(error.code if isinstance(error, Failure) else "interrupted" if isinstance(error, KeyboardInterrupt) else "unexpected_error")
    finally:
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
        try:
            require(path.resolve().is_relative_to((ROOT / "work").resolve()), "report_write_failed")
            report.save(path)
            print("Redacted report: " + str(path))
        except Exception:
            print("Report could not be written.")
            result = 1
    print("Result: " + report.data["overall"] + "; " + report.data["blocker"])
    return result


if __name__ == "__main__":
    sys.exit(main())
