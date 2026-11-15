# SPDX-License-Identifier: MIT
"""Bench logic offline: fake USB/radio, real native session/API, loopback HTTP, PS VerifyOnly."""
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pico_w/tools"))
import bench
import bench_support as support
import build_firmware as build
import firmware_identity
import runtime_contract as native
from jsonschema import Draft202012Validator, FormatChecker

POWERSHELL = sys.argv[3] if len(sys.argv) > 3 else "none"
CANARY = "DO_NOT_SHARE_PRIVATE_DATA_987654321"
REPORT_SCHEMA = json.loads((ROOT / "pico_w/api/bench-report-v1.schema.json").read_text())


def validate_report(data):
    Draft202012Validator(REPORT_SCHEMA, format_checker=FormatChecker()).validate(data)


def artifact(folder):
    folder.mkdir(parents=True, exist_ok=True)
    image = build.uf2(b"\x42" * 300)
    manifest = dict(format=1, firmware=support.VERSION, firmware_build_id=firmware_identity.build_id(), api="1.0.0",
                    inputs=json.loads((ROOT / "pico_w/firmware/build-lock.json").read_text()), physical_i2c=False,
                    actuator_execution=False, gecko_gpio="inactive", uf2_sha256=hashlib.sha256(image).hexdigest(), uf2_bytes=len(image))
    write_artifact(folder, image, manifest)
    return manifest


def write_artifact(folder, image, manifest):
    (folder / support.IMAGE).write_bytes(image)
    manifest["uf2_sha256"] = hashlib.sha256(image).hexdigest()
    (folder / "manifest.json").write_text(json.dumps(manifest))
    (folder / "SHA256SUMS").write_text(manifest["uf2_sha256"] + "  " + support.IMAGE + "\n")


class Clock:
    def __init__(self): self.now = 0.1
    def __call__(self): return self.now
    def sleep(self, n): self.now += n


class ChunkPort:
    def __init__(self, chunks, clock): self.chunks, self.clock = list(chunks), clock
    def write(self, payload): return len(payload)
    def flush(self): pass
    def read(self, size):
        self.clock.sleep(0.11)
        return self.chunks.pop(0) if self.chunks else b""


class SupportTests(unittest.TestCase):
    def test_fragmented_serial_reply_survives_empty_read_timeouts(self):
        clock = Clock()
        port = ChunkPort([b"boot banner\nCON", b"FIG_", b"", b"SAVED\r\n"], clock)
        self.assertEqual(support.SerialLink(port, clock).exchange(b"PROVISION dummy\n", lambda s: s == "CONFIG_SAVED"), "CONFIG_SAVED")

    def test_serial_short_write_timeout_flood_and_non_ascii_fail_closed(self):
        for chunks, expected in (([], "usb_timeout"), ([b"a" * 513], "usb_framing"), ([b"\xff\n"], "usb_framing"), ([b"x\n" * 17], "usb_framing")):
            clock = Clock()
            with self.subTest(expected=expected), self.assertRaises(support.Failure) as error:
                support.SerialLink(ChunkPort(chunks, clock), clock).exchange(b"INFO\n", lambda _: False, timeout=1)
            self.assertEqual(error.exception.code, expected)
        clock = Clock()
        port = ChunkPort([], clock)
        port.write = lambda payload: len(payload) - 1
        with self.assertRaises(support.Failure) as error:
            support.SerialLink(port, clock).exchange(b"INFO\n", lambda _: True)
        self.assertEqual(error.exception.code, "usb_write_failed")

    def test_only_matching_pico_vid_pid_and_unambiguous_port_are_selected(self):
        pico = SimpleNamespace(vid=0x2e8a, pid=0x000a, device="COM12")
        other = SimpleNamespace(vid=0x2341, pid=0x0043, device="COM8")
        self.assertEqual(support.select_port([other, pico]), "COM12")
        self.assertEqual(support.select_port([pico], "com12"), "COM12")
        for ports, name, code in (([other], None, "no_pico"), ([pico, pico], None, "ambiguous_pico"), ([other], "COM8", "wrong_port")):
            with self.assertRaises(support.Failure) as error: support.select_port(ports, name)
            self.assertEqual(error.exception.code, code)

    def test_artifact_hash_structure_reserved_flash_safety_and_source_identity(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            folder = Path(temp)
            good = artifact(folder)
            self.assertEqual(support.verify_artifact(folder), good)
            for offset in (0, 8, 12, 16, 20, 24, 28, 288, 508):
                image = bytearray(build.uf2(b"\x42" * 300))
                image[offset] ^= 1
                write_artifact(folder, image, copy.deepcopy(good))  # Re-sign to exercise structural checks, not just hash.
                with self.assertRaises(support.Failure): support.verify_artifact(folder)
            for key, value, code in (("physical_i2c", True, "artifact_invalid"), ("actuator_execution", True, "artifact_invalid"),
                                     ("firmware_build_id", "f" * 64, "source_mismatch")):
                broken = copy.deepcopy(good)
                broken[key] = value
                write_artifact(folder, build.uf2(b"\x42" * 300), broken)
                with self.assertRaises(support.Failure) as error: support.verify_artifact(folder)
                self.assertEqual(error.exception.code, code)

    def test_tokens_reuse_without_overwrite_and_reject_oversized_files(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            path = Path(temp) / "token"
            with self.assertRaises(support.Failure): support.local_token(path)
            first = support.local_token(path, create=True)
            self.assertEqual(first, support.local_token(path, create=True))
            path.write_text("x" * 131)
            with self.assertRaises(support.Failure): support.local_token(path)
            self.assertEqual(path.read_text(), "x" * 131)
            source, target = Path(temp) / "legacy", Path(temp) / "private/token"
            source.write_text(first)
            self.assertTrue(support.migrate_token(source, target))
            self.assertFalse(support.migrate_token(source, target))
            self.assertEqual(support.local_token(target), first)
            self.assertEqual(source.read_text(), first)

    def test_crlf_checkouts_have_identical_firmware_and_source_digests(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            p = Path(temp) / "x.cpp"
            p.write_bytes(b"line\nnext\n")
            with mock.patch.object(firmware_identity, "PICO", Path(temp)), mock.patch.object(firmware_identity, "inputs", return_value=[p]):
                first = firmware_identity.build_id()
                p.write_bytes(b"line\r\nnext\r\n")
                self.assertEqual(firmware_identity.build_id(), first)
            pico = Path(temp) / "pico_w"
            pico.mkdir()
            cmake = pico / "CMakeLists.txt"
            cmake.write_bytes(b"line\nnext\n")
            with mock.patch.object(build, "PICO", pico), mock.patch.object(build, "ROOT", Path(temp)):
                first = build.source_digest()
                cmake.write_bytes(b"line\r\nnext\r\n")
                self.assertEqual(build.source_digest(), first)

    def test_report_is_allowlisted_not_exception_or_response_scrubbing(self):
        report = bench.Report(True)
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(support.Failure):
            report.step("usb_identity", lambda: (_ for _ in ()).throw(ValueError(CANARY)))
        report.fail(CANARY)
        self.assertNotIn(CANARY, json.dumps(report.data))
        self.assertEqual(report.data["blocker"], "unexpected_error")
        validate_report(report.data)
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(support.Failure):
            report.step("diagnostics", lambda: {"token": CANARY})
        self.assertNotIn(CANARY, json.dumps(report.data))

    def test_verify_only_never_enumerates_or_opens_usb_and_writes_report(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            folder = Path(temp)
            artifact(folder / "artifact")
            target = folder / "report.json"
            with mock.patch("serial.tools.list_ports.comports", side_effect=AssertionError("USB access")), contextlib.redirect_stdout(io.StringIO()):
                result = bench.main(["--verify-only", "--artifact-dir", str(folder / "artifact"), "--report", str(target)])
            self.assertEqual(result, 0)
            document = json.loads(target.read_text())
            validate_report(document)
            self.assertEqual(document["overall"], "verified_only")
            self.assertFalse(document["hardware_accessed"])
            self.assertTrue(all(c["status"] == "not_run" for c in document["checks"][1:]))

    def test_isolated_open_sets_sdk_dtr_without_bootsel_or_baud_reset(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            folder = Path(temp)
            artifact(folder / "artifact")
            events = []
            class Port:
                def __init__(self, **kwargs):
                    self.kwargs = kwargs
                    events.append(self)
                def open(self): pass
                def close(self): pass
                def reset_input_buffer(self): pass
            class StopBeforeProtocol:
                def __init__(self, *args): pass
                def run(self, report):
                    report.step("usb_identity", lambda: (_ for _ in ()).throw(support.Failure("identity_mismatch")))
            target = folder / "report.json"
            pico = SimpleNamespace(vid=0x2e8a, pid=0x000a, device="COM12")
            with mock.patch("serial.tools.list_ports.comports", return_value=[pico]), mock.patch("serial.Serial", Port), mock.patch.object(bench, "Bench", StopBeforeProtocol), mock.patch.object(bench.time, "sleep"), contextlib.redirect_stdout(io.StringIO()):
                result = bench.main(["--isolated-pico", "--artifact-dir", str(folder / "artifact"), "--report", str(target)])
            self.assertEqual(result, 1)
            self.assertEqual(len(events), 1)
            self.assertTrue(events[0].dtr)
            self.assertFalse(events[0].rts)
            self.assertEqual(events[0].kwargs["baudrate"], 115200)
            data = json.loads(target.read_text())
            validate_report(data)
            self.assertEqual(data["blocker"], "identity_mismatch")

    def test_device_identity_mismatch_and_legacy_firmware_stop_before_provisioning(self):
        for line, code in ((b"CONFIG_REJECTED\n", "firmware_update_required"),
                           (json.dumps(dict(schema_version=1, firmware_version=CANARY, build_id="f" * 64)).encode() + b"\n", "identity_mismatch")):
            clock = Clock()
            with self.assertRaises(support.Failure) as error:
                support.SerialLink(ChunkPort([line], clock), clock).info(dict(firmware=support.VERSION, firmware_build_id="a" * 64))
            self.assertEqual(error.exception.code, code)

    def test_only_private_usb_ipv4_literals_are_accepted(self):
        self.assertEqual(support.bench_ip("192.168.5.1"), "192.168.5.1")
        for value in ("localhost", "127.0.0.1", "8.8.8.8", "::1", "http://192.168.5.1", CANARY):
            with self.assertRaises(support.Failure): support.bench_ip(value)


class RadioDouble:
    """Only USB identity/radio are doubled. Session/parser/serializer run in the native executable."""
    def __init__(self, clock, manifest):
        self.clock, self.manifest, self.ready_at, self.provisioned = clock, manifest, float("inf"), False
        self.attempts = self.reconnects = 0
    def info(self, manifest):
        return dict(schema_version=1, firmware_version=support.VERSION, build_id=manifest["firmware_build_id"],
                    boot_id="0" * 32, device_id="gecko-host-mock-001", provisioned=self.provisioned,
                    wifi="online" if self.clock() >= self.ready_at else "backoff", ip="192.168.50.20", port=8765,
                    wifi_attempts=self.attempts, wifi_reconnects=self.reconnects, mode="simulation",
                    physical_i2c=False, actuator_execution=False, gpio="inactive", unknown_private_field=CANARY)
    def exchange(self, payload, accepts, timeout=5):
        if payload.startswith(b"PROVISION "):
            self.provisioned = True
            self.ready_at = self.clock() + 1
            self.attempts += 1
            return "CONFIG_SAVED"
        if payload == b"WIFI_RECONNECT\n":
            self.reconnects += 1
            self.attempts += 1
            self.ready_at = self.clock() + 1
            return "RECONNECTING"
        raise AssertionError("Unexpected USB command")


class NativeHttp:
    def __init__(self, probe, clock, radio, manifest, unsafe=False):
        self.probe, self.clock, self.radio, self.manifest, self.unsafe = probe, clock, radio, manifest, unsafe
    def request(self, path, method="GET", body=None, auth=True, headers=None):
        self.clock.sleep(0.3)
        at = int(self.clock() * 1000)
        self.probe.advance(at)
        values = {"Authorization": "Bearer " + native.TOKEN} if auth else {}
        values.update(headers or {})
        answer = self.probe.request(native.wire(path, method, body, list(values.items()), auth=False), at)
        document = answer["body"]
        if path.endswith("capabilities") and answer["status"] == 200:
            document["firmware_build_id"] = self.manifest["firmware_build_id"]  # Host test target has zero build ID.
        if path.endswith("diagnostics") and answer["status"] == 200:
            document["wifi"]["state"] = "online"  # Radio is explicitly not part of this native executable.
        if self.unsafe and path == "/api/v1/commands" and answer["status"] == 403:
            return 202, {}, {"private": CANARY}
        meta = {"www-authenticate": "Bearer"} if answer["authenticate"] else {}
        return answer["status"], meta, document


class FlowTests(unittest.TestCase):
    def test_full_workflow_native_session_plus_usb_radio_doubles_and_redacted_report(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp, native.Probe() as probe:
            folder = Path(temp)
            manifest = artifact(folder / "artifact")
            clock = Clock()
            radio = RadioDouble(clock, manifest)
            client = NativeHttp(probe, clock, radio, manifest)
            flow = bench.Bench(radio, manifest, folder / "token", clock=clock, sleep=clock.sleep,
                               client_factory=lambda *_: client, prompt=lambda: ("TEST_ONLY_SSID", CANARY))
            report = bench.Report(True)
            report.artifact(manifest)
            with contextlib.redirect_stdout(io.StringIO()):
                report.step("artifact", lambda: None)
                flow.run(report)
            report.data.update(overall="pass", hardware_accessed=True)  # Test-only simulated access, never committed as hardware evidence.
            validate_report(report.data)
            self.assertEqual(radio.reconnects, 1)
            text = json.dumps(report.data)
            for secret in (CANARY, "TEST_ONLY_SSID", "192.168.50.20", "gecko-host-mock-001", (folder / "token").read_text().strip()):
                self.assertNotIn(secret, text)

    def test_unexpected_command_acceptance_stops_workflow_before_reconnect(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp, native.Probe() as probe:
            folder = Path(temp)
            manifest = artifact(folder / "artifact")
            clock = Clock()
            radio = RadioDouble(clock, manifest)
            flow = bench.Bench(radio, manifest, folder / "token", clock=clock, sleep=clock.sleep,
                               client_factory=lambda *_: NativeHttp(probe, clock, radio, manifest, unsafe=True),
                               prompt=lambda: ("test", "password"))
            report = bench.Report(True)
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(support.Failure) as error:
                flow.run(report)
            self.assertEqual(error.exception.code, "control_not_denied")
            self.assertEqual(radio.reconnects, 0)
            self.assertNotIn(CANARY, json.dumps(report.data))


class TransportTests(unittest.TestCase):
    def response(self, raw):
        clock = Clock()
        class Wire:
            def __init__(self): self.data = raw
            def settimeout(self, n): pass
            def sendall(self, data): pass
            def recv(self, n):
                clock.sleep(0.01)
                part, self.data = self.data[:n], self.data[n:]
                return part
            def close(self): pass
        with mock.patch.object(support.socket, "create_connection", return_value=Wire()) as create:
            client = support.Http("192.168.50.20", native.TOKEN, clock, clock.sleep)
            result = client.request("/api/v1/health", auth=False)
            self.assertEqual(create.call_count, 1)
            return result

    def test_response_bounds_duplicate_lengths_and_invalid_json_are_rejected(self):
        prefix = b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n"
        self.assertEqual(self.response(prefix + b"Content-Length: 2\r\n\r\n{}")[2], {})
        for tail in (b"Content-Length: 8193\r\n\r\n", b"Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
                     b"Content-Length: 2\r\n\r\n{", b"Content-Length: 2\r\n\r\n{}extra", b"X: " + b"a" * 2048 + b"\r\n\r\n"):
            with self.assertRaises(support.Failure): self.response(prefix + tail)
        with self.assertRaises(support.Failure) as error: self.response(prefix + b"Content-Length: 3\r\n\r\nNaN")
        self.assertEqual(error.exception.code, "http_json")

    def test_redirect_is_returned_without_forwarding_credentials(self):
        raw = b"HTTP/1.1 302 Found\r\nLocation: http://evil.invalid/\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: 2\r\n\r\n{}"
        self.assertEqual(self.response(raw)[0], 302)

    def test_absolute_http_deadline_is_not_renewed_by_dripping_bytes(self):
        clock = Clock()
        class Drip:
            def settimeout(self, n): pass
            def sendall(self, data): pass
            def recv(self, n): clock.sleep(0.6); return b"x"
            def close(self): pass
        with mock.patch.object(support.socket, "create_connection", return_value=Drip()), self.assertRaises(support.Failure) as error:
            support.Http("192.168.50.20", native.TOKEN, clock, clock.sleep).request("/api/v1/health", auth=False)
        self.assertEqual(error.exception.code, "http_unavailable")
        self.assertLess(clock(), 4)

    def test_real_loopback_http_mock_is_readable_through_bounded_client(self):
        from http_mock import App, Server
        from bridge import CoreProcess, ModelCache
        core = CoreProcess(native.CORE, "healthy")
        cache = ModelCache()
        cache.publish(core.advance(100))
        server = Server(("127.0.0.1", 0), App(cache, native.TOKEN))
        worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01})
        worker.start()
        original = support.socket.create_connection
        try:
            with mock.patch.object(support.socket, "create_connection", side_effect=lambda address, timeout: original(("127.0.0.1", server.server_port), timeout)):
                answer = support.Http("192.168.50.20", native.TOKEN).request("/api/v1/status")
                self.assertEqual(answer[0], 200)
                self.assertTrue(answer[2]["connection"]["ready"])
        finally:
            server.shutdown(); server.server_close(); worker.join(); core.close()


@unittest.skipIf(POWERSHELL == "none", "PowerShell unavailable; Windows/PowerShell execution remains unverified")
class PowerShellTests(unittest.TestCase):
    def test_setup_probe_preserves_nonzero_exit_without_dumping_stderr(self):
        with tempfile.TemporaryDirectory(prefix="setup probe ", dir=ROOT / "work") as temp:
            def quoted(value): return "'" + str(value).replace("'", "''") + "'"
            script = Path(temp) / "probe.ps1"
            code = "import sys; sys.stderr.write('" + CANARY + "'); sys.exit(7)"
            script.write_text("$ErrorActionPreference='Stop'\n$t=$null;$e=$null\n" +
                "$ast=[System.Management.Automation.Language.Parser]::ParseFile(" + quoted(ROOT / "pico_w/tools/bench.ps1") + ",[ref]$t,[ref]$e)\n" +
                "$fn=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-Setup'},$true)\n" +
                ". ([scriptblock]::Create($fn.Extent.Text))\n" +
                "$result=Invoke-Setup " + quoted(sys.executable) + " @('-c'," + quoted(code) + ")\n" +
                "if ($result -ne 7) { exit 1 }; Write-Output 'setup_exit:7'\n")
            env = dict(os.environ, XDG_CACHE_HOME=str(ROOT / "work/bench/cache"), XDG_CONFIG_HOME=str(ROOT / "work/bench/config"), XDG_DATA_HOME=str(ROOT / "work/bench/data"))
            result = subprocess.run([POWERSHELL, "-NoLogo", "-NoProfile", "-File", str(script)], stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("setup_exit:7", result.stdout)
            self.assertNotIn(CANARY, result.stdout)

    def test_entrypoint_verify_only_from_path_with_spaces_and_no_hardware(self):
        with tempfile.TemporaryDirectory(prefix="bench space ", dir=ROOT / "work") as temp:
            folder = Path(temp)
            shutil.copytree(ROOT / "pico_w", folder / "pico_w", ignore=shutil.ignore_patterns("__pycache__"))
            artifact(folder / "pico_w/artifacts/t09")
            env = dict(os.environ, XDG_CACHE_HOME=str(ROOT / "work/bench/cache"), XDG_CONFIG_HOME=str(ROOT / "work/bench/config"), XDG_DATA_HOME=str(ROOT / "work/bench/data"))
            command = [POWERSHELL, "-NoLogo", "-NoProfile", "-File", str(folder / "pico_w/tools/bench.ps1"), "-VerifyOnly", "-Python", sys.executable]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout)
            files = list((folder / "work/pico-bench/reports").glob("*.json"))
            self.assertEqual(len(files), 1)
            data = json.loads(files[0].read_text(encoding="utf-8-sig"))
            validate_report(data)
            self.assertFalse(data["hardware_accessed"])
            self.assertEqual(data["overall"], "verified_only")

    def test_entrypoint_conflicting_modes_produce_redacted_bootstrap_failure(self):
        with tempfile.TemporaryDirectory(prefix="bench fail ", dir=ROOT / "work") as temp:
            folder = Path(temp)
            tools = folder / "pico_w/tools"
            tools.mkdir(parents=True)
            shutil.copyfile(ROOT / "pico_w/tools/bench.ps1", tools / "bench.ps1")
            env = dict(os.environ, XDG_CACHE_HOME=str(ROOT / "work/bench/cache"), XDG_CONFIG_HOME=str(ROOT / "work/bench/config"), XDG_DATA_HOME=str(ROOT / "work/bench/data"))
            result = subprocess.run([POWERSHELL, "-NoLogo", "-NoProfile", "-File", str(tools / "bench.ps1"), "-IsolatedPico", "-VerifyOnly"],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            data = json.loads(next((folder / "work/pico-bench/reports").glob("*.json")).read_text(encoding="utf-8-sig"))
            validate_report(data)
            self.assertEqual(data["blocker"], "bootstrap_failed")
            self.assertFalse(data["hardware_accessed"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
