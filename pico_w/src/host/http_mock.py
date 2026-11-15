# SPDX-License-Identifier: MIT
"""Loopback-only, read-only HTTP v1 adapter. Runtime dependencies: stdlib + native core."""
import argparse
import hmac
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import socket
import threading
import time
import uuid

from bridge import BackendUnavailable, CoreProcess, Maintenance, ModelCache, SCENARIOS, strict_json

REQUEST_ID = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
TOKEN = re.compile(r"[A-Za-z0-9_-]{32,128}\Z")
UNAVAILABLE = ["/blower_on", "/waterfall_on", "/program", "/lock_mode", "/pack_type", "/pump_timer_minutes",
               "/spa_clock/local_datetime", "/spa_clock/local_time", "/spa_clock/timezone"] + [
    f"/reminders/{name}/due_date" for name in ("rinse_filter", "clean_filter", "change_water", "spa_checkup")
]


def age_ms(value, elapsed):
    return None if value is None else value // 1000 + elapsed


def saturate(value):
    return min(value, 2**32 - 1)


class App:
    def __init__(self, cache, token, device_id="gecko-host-mock-001", scenario="healthy",
                 rate=5, burst=10, clock=time.monotonic):
        if not TOKEN.fullmatch(token) or not REQUEST_ID.fullmatch(device_id) or scenario not in SCENARIOS:
            raise ValueError("invalid local configuration")
        self.cache, self.token, self.device_id, self.scenario = cache, token.encode("ascii"), device_id, scenario
        self.boot_id = uuid.uuid4().hex
        self.clock, self.started_at = clock, clock()
        self.lock = threading.Lock()
        self.rate, self.burst, self.allowance, self.refilled_at = rate, burst, float(burst), clock()
        self.unauthorized = self.denied_commands = self.rate_limited = 0

    def authorized(self, headers):
        values = headers.get_all("Authorization", [])
        try:
            scheme, supplied = values[0].split(" ", 1) if len(values) == 1 else ("", "")
            ok = scheme.lower() == "bearer" and hmac.compare_digest(supplied.encode("ascii"), self.token)
        except (UnicodeError, ValueError):
            ok = False
        if not ok:
            with self.lock:
                self.unauthorized += 1
        return ok

    def allow_request(self):
        with self.lock:
            now = self.clock()
            self.allowance = min(float(self.burst), self.allowance + max(0, now - self.refilled_at) * self.rate)
            self.refilled_at = now
            if self.allowance < 1:
                self.rate_limited += 1
                return False
            self.allowance -= 1
            return True

    def health(self):
        return dict(api_version="1.0", service="gecko-pico-bridge", boot_id=self.boot_id,
                    uptime_ms=max(0, int((self.clock() - self.started_at) * 1000)), live=True)

    def capabilities(self, raw):
        bound = raw["config_version"] == 82 and raw["status_version"] == 81
        return dict(
            api_version="1.0", device_id=self.device_id, boot_id=self.boot_id, firmware_version="0.1.0-host",
            transport="simulated", mode="simulation", read_only=True, physical_control_enabled=False,
            profile=dict(target_model="IN.YE-3-H3.0 / YE-3-CE V2", config_file="inYT_C82.xml" if bound else None,
                         status_file="inYT_S81.xml" if bound else None,
                         layout="inyt-v51-candidate" if bound else None, validation="synthetic" if bound else "unknown"),
            read_fields=["temperatures", "heater", "standby", "light_demand", "circulation", "pumps"],
            commands={name: dict(supported=False, enabled=False, reason="read_only") for name in
                      ("light.set", "temperature.set", "program.set", "pump.set")},
            equipment={f"pump{i}": None for i in range(1, 5)},
            limits=dict(command_queue=0, command_history=0, idempotency_retention_ms=0,
                        max_body_bytes=512, max_header_bytes=2048, max_uri_bytes=128,
                        max_response_bytes=8192, max_connections=2, requests_per_second=self.rate,
                        request_burst=self.burst),
        )

    def status(self, raw, elapsed):
        data = raw["observed"]
        state = {key: None if data is None else data[key] for key in
                 ("temperature_c", "target_temperature_c", "heater_on", "standby", "light_demand_on", "circulation_on")}
        state.update({key: None for key in ("blower_on", "waterfall_on", "program", "lock_mode", "pack_type", "pump_timer_minutes")})
        outputs = ("off", "high", "low", "unknown")
        demands = ("off", "low", "high", "unknown")
        state["pumps"] = [] if data is None else [dict(id=f"pump{i+1}", state=outputs[p["output"]],
                                                    user_demand=demands[p["demand"]]) for i, p in enumerate(data["pumps"])]
        state["spa_clock"] = dict(local_datetime=None, local_time=None, timezone=None)
        state["reminders"] = {name: dict(due_date=None, source="unknown", age_ms=None) for name in
                              ("rinse_filter", "clean_filter", "change_water", "spa_checkup")}
        age = age_ms(raw["status_age_us"], elapsed)
        stale = raw["stale"] or age is None or age >= raw["stale_after_ms"]
        ready = raw["ready"] and not stale
        bus = "healthy" if ready else "fault" if raw["session_state"] == "fault" else \
              "disabled" if raw["session_state"] == "disabled" else "degraded" if data is not None else "unknown"
        return dict(api_version="1.0", device_id=self.device_id, boot_id=self.boot_id,
                    state_revision=raw["revision"], transport="simulated", mode="simulation",
                    connection=dict(bus_state=bus, session_state=raw["session_state"],
                                    last_valid_frame_age_ms=age_ms(raw["frame_age_us"], elapsed),
                                    last_status_age_ms=age, status_completed_age_ms=age_ms(raw["completed_age_us"], elapsed),
                                    stale_after_ms=raw["stale_after_ms"], ready=ready),
                    quality=dict(valid=data is not None, stale=stale, source="synthetic" if data is not None else "unknown",
                                 invalid_fields=[] if data is None else [f"/{key}" for key in
                                     ("temperature_c", "target_temperature_c", "standby") if data[key] is None],
                                 unavailable_fields=list(UNAVAILABLE), cached_fields=[]), state=state)

    def diagnostics(self, raw):
        s = raw["stats"]
        with self.lock:
            http = dict(unauthorized=saturate(self.unauthorized), denied_commands=saturate(self.denied_commands),
                        rate_limited=saturate(self.rate_limited))
        return dict(api_version="1.0", boot_id=self.boot_id, transport="simulated", mode="simulation",
                    scenario=self.scenario, simulation_time_ms=raw["now_ms"],
                    counters={key: saturate(value) for key, value in s.items()}, http=http,
                    timing_us=dict(max_irq_service=None, max_role_switch=None, max_handshake_ack=None),
                    memory=dict(min_free_bytes=None, stack_headroom_bytes=None),
                    last_error=None if raw["last_error"] is None else dict(code=raw["last_error"], age_ms=None))


class DeadlineReader:
    def __init__(self, raw, connection, timeout):
        self.raw, self.connection = raw, connection
        self.deadline = time.monotonic() + timeout
        self.header_bytes = 0

    def timeout(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError()
        self.connection.settimeout(remaining)

    def readline(self, limit=-1):
        line = bytearray()
        limit = min(2049, limit) if limit >= 0 else 2049
        while len(line) < limit:
            self.timeout()
            byte = self.raw.read(1)
            if not byte:
                break
            line += byte
            self.header_bytes += 1
            if self.header_bytes > 2048:
                raise http.client.LineTooLong("headers")
            if byte == b"\n":
                break
        return bytes(line)

    def read_exact(self, count):
        data = bytearray()
        while len(data) < count:
            self.timeout()
            part = self.raw.read1(count - len(data))
            if not part:
                raise ValueError("incomplete body")
            data += part
        return bytes(data)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    default_request_version = "HTTP/1.1"
    server_version = "GeckoHostMock/1.0"
    sys_version = ""

    def log_message(self, *_):
        pass  # No request targets, body, auth headers or token logging.

    def respond(self, status, document, extra=None):
        data = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
        if len(data) > 8192:
            status = 500
            data = b'{"api_version":"1.0","error":{"code":"internal_error","message":"Response unavailable","request_id":null,"retryable":false}}'
        self.request_version = "HTTP/1.1"
        self.close_connection = True
        self.send_response(status)
        for key, value in {"Content-Type": "application/json", "Content-Length": str(len(data)),
                           "Cache-Control": "no-store", "Connection": "close", **(extra or {})}.items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)
        self.wfile.flush()

    def error(self, status, code, request_id=None, extra=None):
        self.respond(status, dict(api_version="1.0", error=dict(code=code, message=code.replace("_", " "),
                      request_id=request_id, retryable=status in (429, 503))), extra)

    def send_error(self, code, message=None, explain=None):
        self.error(code, "headers_too_large" if code == 431 else "invalid_http")

    def handle_expect_100(self):
        self.error(400, "invalid_http")
        return False  # Do not send an interim response before auth/framing checks.

    def handle_one_request(self):
        self.request_version, self.command, self.requestline = "HTTP/1.1", None, ""
        original = self.rfile
        reader = DeadlineReader(original, self.connection, self.server.request_timeout)
        self.close_connection = True
        try:
            self.raw_requestline = reader.readline(257)
            if not self.raw_requestline:
                return
            if len(self.raw_requestline) > 256:
                self.error(414, "uri_too_long")
                return
            words = self.raw_requestline.rstrip(b"\r\n").split(b" ")
            if len(words) != 3 or any(not word or b"\t" in word for word in words) or words[1].startswith(b"//"):
                # BaseHTTPRequestHandler normalizes // paths; reject before parsing.
                self.error(400, "invalid_http")
                return
            self.rfile = reader
            if not self.parse_request():
                return
            self.rfile = original
            self.close_connection = True
            self.dispatch(reader)
        except (TimeoutError, socket.timeout):
            self.error(408, "request_timeout")
        except http.client.LineTooLong:
            self.error(431, "headers_too_large")
        except (BrokenPipeError, ConnectionError):
            pass
        finally:
            self.rfile = original
            self.close_connection = True

    def dispatch(self, reader):
        app = self.server.app
        if self.request_version != "HTTP/1.1" or len(self.headers.get_all("Host", [])) != 1 or not self.headers["Host"]:
            self.error(400, "invalid_http"); return
        try:
            target = self.path.encode("ascii")
        except UnicodeError:
            self.error(400, "invalid_http"); return
        if len(target) > 128:
            self.error(414, "uri_too_long"); return
        if not self.path.startswith("/") or self.path.startswith("//") or any(c in self.path for c in "?#%"):
            self.error(400, "invalid_http"); return
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get_all("Transfer-Encoding", []) or self.headers.get_all("Expect", []) or len(lengths) > 1:
            self.error(400, "invalid_http"); return
        if lengths and not re.fullmatch(r"[0-9]{1,10}", lengths[0]):
            self.error(400, "invalid_http"); return
        size = int(lengths[0]) if lengths else 0
        if size > 512:
            self.error(413, "body_too_large"); return
        if not (self.path == "/api/v1/health" and self.command == "GET") and not app.authorized(self.headers):
            self.error(401, "unauthorized", extra={"WWW-Authenticate": "Bearer"}); return
        routes = {"/api/v1/health": "GET", "/api/v1/capabilities": "GET", "/api/v1/status": "GET",
                  "/api/v1/diagnostics": "GET", "/api/v1/commands": "POST"}
        history = self.path.startswith("/api/v1/commands/") and REQUEST_ID.fullmatch(self.path.rsplit("/", 1)[-1])
        method = "GET" if history else routes.get(self.path)
        if method is None:
            self.error(404, "not_found"); return
        if self.command != method:
            self.error(405, "method_not_allowed", extra={"Allow": method}); return
        if self.command == "GET" and size:
            self.error(400, "body_not_allowed"); return
        if self.path != "/api/v1/health" and not app.allow_request():
            self.error(429, "rate_limited", extra={"Retry-After": "1"}); return
        if self.path == "/api/v1/health":
            self.respond(200, app.health()); return
        if history:
            self.error(404, "unknown_request"); return
        if self.command == "POST":
            if not lengths:
                self.error(400, "invalid_http"); return
            types = self.headers.get_all("Content-Type", [])
            if len(types) != 1 or types[0].lower() not in ("application/json", "application/json; charset=utf-8") or \
                    self.headers.get_all("Content-Encoding", []):
                self.error(415, "unsupported_media_type"); return
            try:
                obj = strict_json(reader.read_exact(size).decode("utf-8"))
            except (ValueError, UnicodeError, RecursionError):
                self.error(400, "invalid_json"); return
            if not isinstance(obj, dict):
                self.error(422, "invalid_parameters"); return
            request_id = obj.get("request_id")
            request_id = request_id if isinstance(request_id, str) and REQUEST_ID.fullmatch(request_id) else None
            with app.lock:
                app.denied_commands += 1
            self.error(403, "read_only", request_id); return
        try:
            raw, elapsed = app.cache.current()
            if self.path == "/api/v1/status":
                document = app.status(raw, elapsed)
            elif self.path == "/api/v1/capabilities":
                document = app.capabilities(raw)
            else:
                document = app.diagnostics(raw)
        except BackendUnavailable:
            self.error(503, "bridge_unavailable", extra={"Retry-After": "1"}); return
        self.respond(200, document)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = True

    def __init__(self, address, app, request_timeout=2.0):
        if address[0] != "127.0.0.1":
            raise ValueError("host mock binds only to IPv4 loopback")
        self.app, self.request_timeout = app, request_timeout
        self.slots = threading.BoundedSemaphore(2)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request); return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()

    def handle_error(self, request, client_address):
        pass  # Never dump request-associated exceptions or secrets.


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True)
    parser.add_argument("--token-file", type=Path)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--scenario", choices=SCENARIOS, default="healthy")
    parser.add_argument("--device-id", default="gecko-host-mock-001")
    args = parser.parse_args(argv)
    token = args.token_file.read_text().strip() if args.token_file else os.environ.get("GECKO_MOCK_TOKEN", "")
    if not TOKEN.fullmatch(token) or not 0 <= args.port <= 65535:
        parser.error("provide a 32–128 character URL-safe token through a local file or GECKO_MOCK_TOKEN")
    core = CoreProcess(args.core, args.scenario)
    maintenance = None
    try:
        cache = ModelCache()
        maintenance = Maintenance(core, cache)
        app = App(cache, token, args.device_id, args.scenario)
        with Server(("127.0.0.1", args.port), app) as server:
            maintenance.start()
            print(f"Gecko host mock: http://127.0.0.1:{server.server_port}/api/v1/health (synthetic, read-only)", flush=True)
            try:
                server.serve_forever(poll_interval=0.1)
            except KeyboardInterrupt:
                pass
    finally:
        if maintenance:
            maintenance.close()
        else:
            core.close()


if __name__ == "__main__":
    main()
