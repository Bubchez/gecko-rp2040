# SPDX-License-Identifier: MIT
"""Exercise the native firmware HTTP engine, comparing v1 responses to T08."""
import json
from pathlib import Path
import random
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pico_w/src/host"))
from bridge import CoreProcess, SCENARIOS, strict_json
from http_mock import App
from jsonschema import Draft202012Validator, FormatChecker

PROBE, CORE = map(lambda p: Path(p).resolve(), sys.argv[1:3])
SPEC = json.loads((ROOT / "pico_w/api/openapi-v1.json").read_text())
TOKEN = "0123456789abcdef0123456789abcdef"


def validate(name, document):
    Draft202012Validator({"$ref": "#/components/schemas/" + name, "components": SPEC["components"]},
                         format_checker=FormatChecker()).validate(document)


def wire(path="/api/v1/status", method="GET", body=None, headers=None, auth=True):
    values = [("Host", "test")]
    if auth:
        values.append(("Authorization", "Bearer " + TOKEN))
    if body is not None:
        values += [("Content-Type", "application/json"), ("Content-Length", str(len(body)))]
    values += headers or []
    return (f"{method} {path} HTTP/1.1\r\n" + "".join(f"{k}: {v}\r\n" for k, v in values) + "\r\n").encode() + (body or b"")


class Probe:
    def __init__(self, scenario="healthy"):
        self.process = subprocess.Popen([str(PROBE), scenario], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def command(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("native probe exited")
        return strict_json(line)

    def advance(self, now):
        self.command(f"ADVANCE {now}")

    def request(self, raw=None, now=100, start=None):
        return self.command(f"REQUEST {now} {now if start is None else start} {(wire() if raw is None else raw).hex()}")

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.process.stdin.close()
        result = self.process.wait(timeout=5)
        self.process.stdout.close()
        if result != 0:
            raise RuntimeError(f"native probe exit {result}")


class NativeContract(unittest.TestCase):
    def test_all_scenarios_match_existing_host_and_schema(self):
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario), Probe(scenario) as native:
                core = CoreProcess(CORE, scenario)
                app = App(None, TOKEN, scenario=scenario, clock=lambda: 0)
                app.boot_id = "0" * 32
                try:
                    for now in (0, 100, 1100, 1400, 2500, 24000, 60000, 120000):
                        raw = core.advance(now)
                        native.advance(now)
                        # Public health is exempt from authenticated rate limits.
                        health = native.request(wire("/api/v1/health", auth=False), now)
                        self.assertEqual(health["status"], 200)
                        validate("Health", health["body"])
                        expected = (("capabilities", app.capabilities(raw)), ("status", app.status(raw, 0)),
                                    ("diagnostics", app.diagnostics(raw)))
                        for path, document in expected:
                            answer = native.request(wire("/api/v1/" + path), now)
                            self.assertEqual(answer["status"], 200)
                            actual = answer["body"]
                            validate(path.title(), actual)
                            if path == "capabilities":
                                actual["firmware_version"] = document["firmware_version"]
                            if path == "diagnostics":
                                actual.pop("wifi")
                                actual.pop("safety")
                            self.assertEqual(actual, document)
                finally:
                    core.close()

    def test_auth_routes_limits_and_deny_every_intent(self):
        cases = [(wire(auth=False), 401), (wire(headers=[("Authorization", "Bearer " + TOKEN)]), 401),
                 (wire(headers=[("Host", "other")]), 400), (wire("/api/v1/other"), 404),
                 (wire("/api/v1/commands/req-1"), 404), (wire(method="PUT"), 405),
                 (wire(method="HEAD"), 405), (wire(method="POST"), 405)]
        for name in ("light.set", "temperature.set", "pump.set", "program.set", "blower.set", "raw_i2c", "reset", "write_enable", "firmware.upload", "unknown"):
            cases.append((wire("/api/v1/commands", "POST", json.dumps({"request_id": "native-1", "command": name}).encode()), 403))
        for raw, status in cases:
            with self.subTest(raw=raw[:50]), Probe() as p:
                p.advance(100)
                response = p.request(raw)
                self.assertEqual(response["status"], status)
                validate("Error", response["body"])
                if status == 401:
                    self.assertTrue(response["authenticate"])
                if status == 405:
                    self.assertEqual(response["allow"], "GET")
                if status == 403:
                    self.assertEqual(response["body"]["error"]["request_id"], "native-1")
        with Probe() as p:
            p.advance(100)
            for _ in range(10):
                self.assertEqual(p.request()["status"], 200)
            self.assertEqual(p.request()["status"], 429)
            p.advance(300)
            self.assertEqual(p.request(now=300)["status"], 200)

    def test_json_matches_strict_host_semantics(self):
        corpus = [b"{}", b"[]", b"null", b"true", b"1", b"NaN", b"{", b'{"a":1,"a":2}',
                  b'{"a":1,"\\u0061":2}', b'{"n":1e999}', b'{"n":1e-999}', b'{"n":' + b"9" * 500 + b'}',
                  b'{"x":"\\ud800"}', b'{"x":"\\udc00"}', b'{"x":"\\ud83d\\ude00"}', b'{"x":"\xf0\x9f\x98\x80"}',
                  b'{"x":"\xc0\x80"}', b'{"x":"\xed\xa0\x80"}', b'{"x":"\xf4\x90\x80\x80"}',
                  b'{"x":"\\u0000"}', b'{"x":01}', b'{"x":+1}', b'{"x":1.}', b'{"x":1e+}', b'{}{}',
                  b'{"request_id":"\\u0061"}', b'{"request_id":"bad/id"}', b'{"request_id":0}',
                  b'{"a":{"x":1},"b":{"x":2}}', b"[" * 255 + b"{}" + b"]" * 255]
        rng = random.Random(9)
        for _ in range(150):
            value = {"request_id": "test_1", "data": [rng.randrange(-10000, 10000), rng.random(), None, "☃😀"]}
            body = json.dumps(value, ensure_ascii=rng.choice([True, False])).encode()
            if rng.choice([True, False]):
                at = rng.randrange(len(body))
                body = body[:at] + bytes([rng.randrange(256)]) + body[at + 1:]
            corpus.append(body)
        with Probe() as p:
            now = 0
            for body in corpus:
                now += 200
                p.advance(now)
                try:
                    value = strict_json(body.decode("utf-8"))
                    expected = 403 if isinstance(value, dict) else 422
                except (ValueError, UnicodeError, RecursionError):
                    expected = 400
                with self.subTest(body=repr(body[:60])):
                    response = p.request(wire("/api/v1/commands", "POST", body), now)
                    self.assertEqual(response["status"], expected)
                    validate("Error", response["body"])

    def test_framing_sizes_and_deadline(self):
        cases = [(wire("//api/v1/status"), 400), (wire("http://test/api/v1/status"), 400),
                 (wire("/api/v1/status?token=x"), 400), (wire("/api/v1/%73tatus"), 400),
                 (wire("/" + "x" * 128), 414), (wire(headers=[("X-Large", "a" * 2000)]), 431),
                 (wire(headers=[("Content-Length", "9999999999")]), 413),
                 (wire(headers=[("Content-Length", "1"), ("Content-Length", "1")]), 400),
                 (wire(headers=[("Transfer-Encoding", "chunked")]), 400),
                 (wire(headers=[("Expect", "100-continue")]), 400),
                 (wire(headers=[("Content-Length", "1")]) + b"x", 400),
                 (wire("/api/v1/commands", "POST"), 400),
                 (wire("/api/v1/commands", "POST", b"{}", [("Content-Type", "text/plain")]), 415),
                 (wire("/api/v1/commands", "POST", b"{}", [("Content-Encoding", "gzip")]), 415)]
        for raw, expected in cases:
            with self.subTest(raw=raw[:80]), Probe() as p:
                self.assertEqual(p.request(raw)["status"], expected)
        with Probe() as p:
            request = wire("/api/v1/commands", "POST", b'{"request_id":"fragmented"}')
            for length in range(1, len(request)):
                self.assertEqual(p.request(request[:length])["status"], 0)
            self.assertEqual(p.request(request)["status"], 403)
            self.assertEqual(p.request(b"GET /api/v1/health HTTP/1.1\r\n", now=2000, start=0)["status"], 408)

    def test_cache_failure_freshness_and_independent_denial(self):
        with Probe() as p:
            p.advance(100)
            self.assertTrue(p.request()["body"]["connection"]["ready"])
            response = p.request(now=1099)
            self.assertEqual(response["status"], 200)
            self.assertGreaterEqual(response["body"]["connection"]["last_status_age_ms"], 999)
            self.assertEqual(p.request(now=1100)["status"], 503)
            self.assertEqual(p.request(wire("/api/v1/health", auth=False), 1100)["status"], 200)
            self.assertEqual(p.request(wire("/api/v1/commands", "POST", b"{}"), 1100)["status"], 403)
            p.advance(1100)
            self.assertEqual(p.request(now=1100)["status"], 200)
            p.command("UNAVAILABLE")
            self.assertEqual(p.request(now=1100)["status"], 503)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
