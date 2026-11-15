# SPDX-License-Identifier: MIT
"""HTTP/OpenAPI/IPC acceptance tests against the real host session executable."""
import copy
import http.client
import importlib.util
import json
from pathlib import Path
import queue
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pico_w/src/host"))
from bridge import (BackendUnavailable, CoreProcess, Maintenance, ModelCache, SCENARIOS,
                    strict_json, validate_snapshot)
from http_mock import App, Server, UNAVAILABLE

CORE = Path(sys.argv[2]).resolve()
GROUP = sys.argv[1]
TOKEN = "test_only_" + "a" * 40
SPEC = json.loads((ROOT / "pico_w/api/openapi-v1.json").read_text())


class Clock:
    def __init__(self): self.now = 100.0
    def __call__(self): return self.now


class Fixture:
    def __init__(self, scenario="healthy", milliseconds=100, rate=10000, burst=10000, timeout=0.4):
        self.core = CoreProcess(CORE, scenario)
        self.clock = Clock()
        self.cache = ModelCache(clock=self.clock)
        self.cache.publish(self.core.advance(milliseconds))
        self.app = App(self.cache, TOKEN, scenario=scenario, rate=rate, burst=burst, clock=self.clock)
        self.server = Server(("127.0.0.1", 0), self.app, request_timeout=timeout)
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        self.thread.start()

    def advance(self, milliseconds):
        self.cache.publish(self.core.advance(milliseconds))

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.core.close()

    def request(self, path="/api/v1/status", method="GET", body=None, headers=None, auth=True):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        values = {"Authorization": "Bearer " + TOKEN} if auth else {}
        values.update(headers or {})
        connection.request(method, path, body=body, headers=values)
        response = connection.getresponse()
        data = response.read()
        result = response.status, dict(response.getheaders()), strict_json(data) if data else None
        connection.close()
        return result

    def raw(self, request):
        with socket.create_connection(("127.0.0.1", self.server.server_port), timeout=3) as connection:
            connection.sendall(request)
            chunks = []
            while True:
                part = connection.recv(16384)
                if not part: break
                chunks.append(part)
        raw = b"".join(chunks)
        head, body = raw.split(b"\r\n\r\n", 1)
        return int(head.split(b" ", 2)[1]), head, strict_json(body)


def validate(name, document):
    from jsonschema import Draft202012Validator, FormatChecker
    schema = {"$ref": "#/components/schemas/" + name, "components": SPEC["components"]}
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)


class SchemaTests(unittest.TestCase):
    def test_committed_examples_are_valid_synthetic_handoff_data(self):
        examples = list((ROOT / "pico_w/api/examples").glob("*.json"))
        self.assertEqual(len(examples), 7)
        for path in examples:
            document = strict_json(path.read_text())
            validate("Status" if path.name.startswith("status-") else "Capabilities", document)
            self.assertEqual(document["transport"], "simulated")
            self.assertEqual(document["boot_id"], "0" * 32)

    def test_openapi_is_valid_and_read_only(self):
        from openapi_spec_validator import validate as validate_openapi
        validate_openapi(SPEC)
        self.assertEqual(SPEC["info"]["version"], "1.0.0")
        for path, methods in SPEC["paths"].items():
            for method, operation in methods.items():
                if method != "get":
                    self.assertEqual((path, method), ("/api/v1/commands", "post"))
                    self.assertFalse(any(code.startswith("2") for code in operation["responses"]))
        self.assertEqual(SPEC["paths"]["/api/v1/health"]["get"]["security"], [])

    def test_every_scenario_matches_schema_and_null_rules(self):
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario):
                f = Fixture(scenario)
                try:
                    for path, name in (("health", "Health"), ("capabilities", "Capabilities"),
                                       ("status", "Status"), ("diagnostics", "Diagnostics")):
                        status, headers, document = f.request("/api/v1/" + path)
                        self.assertEqual(status, 200)
                        self.assertEqual(headers["Content-Type"], "application/json")
                        self.assertEqual(headers["Cache-Control"], "no-store")
                        self.assertEqual(headers["Connection"], "close")
                        validate(name, document)
                    payload = f.request()[2]
                    self.assertEqual(payload["quality"]["unavailable_fields"], UNAVAILABLE)
                    for pointer in UNAVAILABLE:
                        value = payload["state"]
                        for segment in pointer.split("/")[1:]: value = value[segment]
                        self.assertIsNone(value)
                    if scenario == "unavailable":
                        self.assertFalse(payload["quality"]["valid"])
                        self.assertIsNone(payload["state"]["heater_on"])
                        self.assertEqual(payload["state"]["pumps"], [])
                    if scenario == "invalid_temperature":
                        self.assertIsNone(payload["state"]["temperature_c"])
                        self.assertEqual(payload["state"]["target_temperature_c"], 37)
                        self.assertIn("/temperature_c", payload["quality"]["invalid_fields"])
                finally: f.close()

    def test_schema_rejects_false_control_permission_or_invalid_values(self):
        f = Fixture()
        try:
            from jsonschema import ValidationError
            caps = f.request("/api/v1/capabilities")[2]
            bad = copy.deepcopy(caps); bad["physical_control_enabled"] = True
            with self.assertRaises(ValidationError): validate("Capabilities", bad)
            bad = copy.deepcopy(caps); bad["commands"]["light.set"]["enabled"] = True
            with self.assertRaises(ValidationError): validate("Capabilities", bad)
            state = f.request()[2]; state["state_revision"] = -1
            with self.assertRaises(ValidationError): validate("Status", state)
        finally: f.close()


class BackendTests(unittest.TestCase):
    def test_long_running_native_model_retires_only_complete_scripts(self):
        core = CoreProcess(CORE)
        try:
            first = core.advance(100)
            core.advance(60000)
            last = core.advance(120000)
            self.assertTrue(last["ready"])
            self.assertGreater(last["revision"], first["revision"])
            self.assertGreater(last["stats"]["checkpoints"], 100)
            self.assertGreater(last["stats"]["retired_trace_events"], 512)
            self.assertGreater(last["session"], 1)
            self.assertEqual(last["stats"]["failures"], 0)
        finally: core.close()

    def test_backend_ipc_rejects_malformed_and_nul_commands(self):
        commands = ["SNAPSHOT", "ADVANCE -1", "ADVANCE 1x", "ADVANCE 60001", "X" * 120,
                    "SNAPSHOT\x00ignored", "SNAPSHOT"]
        process = subprocess.run([str(CORE)], input="\n".join(commands) + "\n", text=True,
                                 capture_output=True, timeout=10, check=True)
        rows = [strict_json(line) for line in process.stdout.splitlines()]
        self.assertEqual(len(rows), len(commands))
        for row in rows[1:-1]: self.assertEqual(row, {"backend_error": "invalid_command"})
        self.assertEqual(rows[-1]["revision"], 0)
        self.assertNotIn("AddressSanitizer", process.stderr)

    def test_cache_bounds_and_backend_data_validation(self):
        core = CoreProcess(CORE)
        try:
            raw = core.advance(100)
            clock = Clock(); cache = ModelCache(clock=clock)
            cache.publish(raw); clock.now += 0.999
            self.assertGreaterEqual(cache.current()[1], 998)
            clock.now += 0.002
            with self.assertRaises(BackendUnavailable): cache.current()
            for mutate in (lambda d: d.update(hardware_verified=True), lambda d: d.update(revision=-1),
                           lambda d: d.update(schema_version=True),
                           lambda d: d.pop("status_age_us"),
                           lambda d: d["observed"]["pumps"].__setitem__(0, None),
                           lambda d: d.update(ready=True, observed=None),
                           lambda d: d["observed"].update(temperature_c=float("inf")),
                           lambda d: d["stats"].pop("statuses")):
                bad = copy.deepcopy(raw); mutate(bad)
                with self.assertRaises(ValueError): validate_snapshot(bad)
            for text in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}', '{"a":1e999}', '{"a":"\\ud800"}'):
                with self.assertRaises((ValueError, UnicodeError)): strict_json(text)
        finally: core.close()

    def test_worker_maintains_session_without_http_requests(self):
        core = CoreProcess(CORE); cache = ModelCache(); maintenance = Maintenance(core, cache)
        try:
            maintenance.start()
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                raw, _ = cache.current()
                if raw["ready"]: break
                time.sleep(0.01)
            self.assertTrue(raw["ready"])
            self.assertGreaterEqual(raw["stats"]["tx_submitted"], 4)
        finally: maintenance.close()

    def test_worker_reports_native_process_loss(self):
        core = CoreProcess(CORE); cache = ModelCache(); maintenance = Maintenance(core, cache)
        try:
            maintenance.start()
            core.process.terminate(); core.process.wait(timeout=2)
            deadline = time.monotonic() + 2
            unavailable = False
            while time.monotonic() < deadline:
                try: cache.current()
                except BackendUnavailable:
                    unavailable = True; break
                time.sleep(0.01)
            self.assertTrue(unavailable)
        finally: maintenance.close()


class ContractTests(unittest.TestCase):
    def setUp(self): self.f = Fixture()
    def tearDown(self): self.f.close()

    def test_authentication_and_minimal_public_health(self):
        status, _, health = self.f.request("/api/v1/health", auth=False)
        self.assertEqual(status, 200)
        self.assertNotIn(TOKEN, json.dumps(health))
        self.assertNotIn("profile", health)
        for path in ("status", "capabilities", "diagnostics", "commands/missing"):
            code, headers, error = self.f.request("/api/v1/" + path, auth=False)
            self.assertEqual(code, 401)
            self.assertEqual(headers["WWW-Authenticate"], "Bearer")
            validate("Error", error)
        for supplied in ("Bearer wrong", "Basic " + TOKEN, "Bearer " + TOKEN + "x", "Bearer " + TOKEN + " "):
            self.assertEqual(self.f.request(headers={"Authorization": supplied})[0], 401)
        self.assertEqual(self.f.request(headers={"Authorization": "bearer " + TOKEN})[0], 200)

    def test_boot_unknowns_become_observed_without_changing_boot_identity(self):
        self.f.close(); self.f = Fixture(milliseconds=0)
        initial = self.f.request()[2]
        self.assertEqual(initial["state_revision"], 0)
        self.assertFalse(initial["quality"]["valid"])
        self.assertEqual(initial["quality"]["source"], "unknown")
        self.assertIsNone(initial["connection"]["last_status_age_ms"])
        self.assertIsNone(initial["state"]["heater_on"])
        self.assertIsNone(self.f.request("/api/v1/capabilities")[2]["profile"]["config_file"])
        self.f.advance(100)
        ready = self.f.request()[2]
        self.assertEqual(initial["boot_id"], ready["boot_id"])
        self.assertTrue(ready["connection"]["ready"])
        self.assertEqual(ready["quality"]["source"], "synthetic")
        self.assertIs(self.f.request("/api/v1/capabilities")[2]["read_only"], True)

    def test_all_actuator_intents_are_rejected_without_advancing_core(self):
        before = self.f.core.snapshot()
        for name in ("light.set", "temperature.set", "program.set", "pump.set", "heater.set", "clock.set", "raw", "toggle", "unknown"):
            body = json.dumps(dict(request_id="client-1", type=name, parameters={"on": True})).encode()
            status, _, error = self.f.request("/api/v1/commands", "POST", body, {"Content-Type": "application/json"})
            self.assertEqual(status, 403)
            self.assertEqual(error["error"]["code"], "read_only")
            self.assertEqual(error["error"]["request_id"], "client-1")
            validate("Error", error)
        after = self.f.core.snapshot()
        self.assertEqual(before, after)
        self.assertEqual(self.f.request("/api/v1/commands/client-1")[0], 404)
        self.assertEqual(self.f.request("/api/v1/diagnostics")[2]["http"]["denied_commands"], 9)

    def test_gets_do_not_tick_or_mutate_core(self):
        before = self.f.core.snapshot()
        for _ in range(10):
            for path in ("health", "status", "capabilities", "diagnostics"):
                self.assertEqual(self.f.request("/api/v1/" + path)[0], 200)
        self.assertEqual(self.f.core.snapshot(), before)

    def test_stale_state_is_retained_but_not_ready(self):
        self.f.close(); self.f = Fixture("stale")
        first = self.f.request()[2]
        self.f.advance(2050)
        status, _, stale = self.f.request()
        self.assertEqual(status, 200)
        self.assertTrue(stale["quality"]["valid"] and stale["quality"]["stale"])
        self.assertFalse(stale["connection"]["ready"])
        self.assertEqual(stale["state"], first["state"])
        self.assertEqual(stale["state_revision"], first["state_revision"])
        self.assertGreaterEqual(stale["connection"]["last_status_age_ms"], 2000)
        self.assertIsNone(self.f.request("/api/v1/capabilities")[2]["profile"]["config_file"])
        validate("Status", stale)

    def test_fault_recovers_through_existing_session_core(self):
        self.f.close(); self.f = Fixture("recovery")
        first = self.f.request()[2]
        self.f.advance(1030)
        failed = self.f.request()[2]
        self.assertFalse(failed["connection"]["ready"])
        self.assertTrue(failed["quality"]["stale"])
        self.assertEqual(failed["connection"]["session_state"], "backoff")
        self.f.advance(1500)
        ready = self.f.request()[2]
        self.assertTrue(ready["connection"]["ready"])
        self.assertEqual(ready["boot_id"], first["boot_id"])
        self.assertGreater(ready["state_revision"], first["state_revision"])
        diag = self.f.request("/api/v1/diagnostics")[2]
        self.assertGreater(diag["counters"]["failures"], 0)
        self.assertIsNone(diag["last_error"])

    def test_dead_backend_or_expired_cache_is_503_not_fake_state(self):
        self.f.cache.unavailable()
        for path in ("status", "capabilities", "diagnostics"):
            code, headers, error = self.f.request("/api/v1/" + path)
            self.assertEqual(code, 503)
            self.assertEqual(headers["Retry-After"], "1")
            self.assertEqual(error["error"]["code"], "bridge_unavailable")
        self.assertEqual(self.f.request("/api/v1/health", auth=False)[0], 200)
        # Rejection must not depend on backend availability.
        self.assertEqual(self.f.request("/api/v1/commands", "POST", b"{}", {"Content-Type": "application/json"})[0], 403)

    def test_rate_limit_is_bounded_and_has_retry_header(self):
        self.f.app.rate = 5; self.f.app.burst = 2; self.f.app.allowance = 2
        self.assertEqual(self.f.request()[0], 200)
        self.assertEqual(self.f.request()[0], 200)
        code, headers, error = self.f.request()
        self.assertEqual((code, headers["Retry-After"], error["error"]["code"]), (429, "1", "rate_limited"))
        self.f.clock.now += 0.2
        self.assertEqual(self.f.request()[0], 200)

    def test_route_methods_and_cors_are_closed(self):
        code, headers, _ = self.f.request(method="POST", body=b"")
        self.assertEqual((code, headers["Allow"]), (405, "GET"))
        self.assertNotIn("Access-Control-Allow-Origin", headers)
        self.assertEqual(self.f.request("/api/v1/raw")[0], 404)
        self.assertEqual(self.f.request("/api/v1/status?token=secret")[0], 400)
        self.assertEqual(self.f.request("/api/v1/commands", "DELETE")[0], 405)


class FramingTests(unittest.TestCase):
    def setUp(self): self.f = Fixture(timeout=0.2)
    def tearDown(self): self.f.close()

    def test_json_rejections(self):
        bad = [b'{"a":1,"a":2}', b'{"a":NaN}', b'{"a":Infinity}', b'{"a":1e999}',
               b'{"a":"\xff"}', b'{}{}', b'{', b'{"a":"\\ud800"}']
        for body in bad:
            with self.subTest(body=body):
                self.assertEqual(self.f.request("/api/v1/commands", "POST", body, {"Content-Type": "application/json"})[0], 400)
        for body in (b'[]', b'null', b'7', b'"text"'):
            self.assertEqual(self.f.request("/api/v1/commands", "POST", body, {"Content-Type": "application/json"})[0], 422)
        self.assertEqual(self.f.request("/api/v1/commands", "POST", b'{}', {"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.f.request("/api/v1/commands", "POST", b'{}', {"Content-Type": "application/json; charset=utf-8"})[0], 403)

    def test_limits_and_ambiguous_framing(self):
        auth = ("Authorization: Bearer " + TOKEN + "\r\n").encode()
        prefix = b"POST /api/v1/commands HTTP/1.1\r\nHost: localhost\r\n" + auth
        for tail in (b"Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}", b"Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
                     b"Expect: 100-continue\r\nContent-Length: 2\r\n\r\n{}",
                     b"Content-Length: -1\r\n\r\n", b"\r\n"):
            self.assertEqual(self.f.raw(prefix + tail)[0], 400)
        self.assertEqual(self.f.raw(prefix + b"Content-Length: 513\r\n\r\n")[0], 413)
        self.assertEqual(self.f.raw(b"GET /" + b"a" * 128 + b" HTTP/1.1\r\nHost: localhost\r\n\r\n")[0], 414)
        self.assertEqual(self.f.raw(b"GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\nX: " + b"a" * 2048 + b"\r\n\r\n")[0], 431)
        self.assertEqual(self.f.raw(b"GET /api/v1/health HTTP/1.1\r\n\r\n")[0], 400)
        duplicate = b"GET /api/v1/status HTTP/1.1\r\nHost: localhost\r\n" + auth + auth + b"\r\n"
        self.assertEqual(self.f.raw(duplicate)[0], 401)
        doubled = b"GET //api/v1/status HTTP/1.1\r\nHost: localhost\r\n" + auth + b"\r\n"
        self.assertEqual(self.f.raw(doubled)[0], 400)

    def test_read_deadline_is_absolute_and_connection_closes(self):
        with socket.create_connection(("127.0.0.1", self.f.server.server_port), timeout=2) as s:
            s.sendall(b"GET /api/v1/health HTTP/1.1\r\nHost:")
            time.sleep(0.25)
            response = s.recv(8192)
            self.assertIn(b" 408 ", response)
        first = b"GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n\r\n"
        code, head, _ = self.f.raw(first + first)
        self.assertEqual(code, 200)
        self.assertIn(b"Connection: close", head)


class StartupTests(unittest.TestCase):
    def test_windows_executable_discovery_and_secret_provisioning(self):
        spec = importlib.util.spec_from_file_location("start_gecko", ROOT / "pico_w/tools/start_mock.py")
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            folder = Path(temp); (folder / "Release").mkdir()
            executable = folder / "Release/gecko_mock_core.exe"; executable.write_bytes(b"fixture")
            self.assertEqual(module.find_core(folder), executable)
            token = module.provision_token(folder / "token.txt")
            first = token.read_text()
            self.assertRegex(first.strip(), r"^[A-Za-z0-9_-]{32,128}$")
            module.provision_token(token)
            self.assertEqual(token.read_text(), first)

    def test_runnable_launcher_real_http_and_no_token_logging(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "work") as temp:
            token_file = Path(temp) / "local.token"
            process = subprocess.Popen([sys.executable, str(ROOT / "pico_w/tools/start_mock.py"),
                "--core", str(CORE), "--token-file", str(token_file), "--port", "0"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
            lines = queue.Queue()
            reader = threading.Thread(target=lambda: [lines.put(line) for line in process.stdout], daemon=True)
            reader.start()
            output = []
            try:
                deadline = time.monotonic() + 8
                port = None
                while time.monotonic() < deadline:
                    line = lines.get(timeout=3); output.append(line)
                    match = re.search(r"http://127.0.0.1:(\d+)/", line)
                    if match: port = int(match[1]); break
                self.assertIsNotNone(port)
                token = token_file.read_text().strip()
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
                connection.request("GET", "/api/v1/health")
                response = connection.getresponse(); self.assertEqual(response.status, 200); response.read(); connection.close()
                time.sleep(0.08)
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
                connection.request("GET", "/api/v1/status", headers={"Authorization": "Bearer " + token})
                response = connection.getresponse(); data = strict_json(response.read()); connection.close()
                self.assertEqual(response.status, 200)
                self.assertEqual(data["transport"], "simulated")
                self.assertTrue(data["connection"]["ready"])
                self.assertNotIn(token, "".join(output))
            finally:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=3)
                reader.join(timeout=2)
                stderr = process.stderr.read()
                if token_file.exists(): self.assertNotIn(token_file.read_text().strip(), stderr)
                process.stdout.close(); process.stderr.close()


if __name__ == "__main__":
    classes = {"schema": SchemaTests, "backend": BackendTests, "contract": ContractTests,
               "framing": FramingTests, "startup": StartupTests}
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(classes[GROUP])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
