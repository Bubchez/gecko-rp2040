# SPDX-License-Identifier: MIT
"""Bounded host IPC and immutable cache; HTTP requests never drive the session."""
import json
import math
import os
from pathlib import Path
import queue
import subprocess
import threading
import time

SCENARIOS = ("healthy", "unavailable", "stale", "recovery", "invalid_temperature")


class BackendUnavailable(Exception):
    pass


def reject_constant(_):
    raise ValueError("nonfinite JSON")


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError("duplicate JSON key")
        out[key] = value
    return out


def strict_json(data):
    def finite_float(text):
        number = float(text)
        if not math.isfinite(number):
            raise ValueError("nonfinite JSON")
        return number

    def validate_strings(value):
        if isinstance(value, str):
            value.encode("utf-8")
        elif isinstance(value, list):
            for item in value:
                validate_strings(item)
        elif isinstance(value, dict):
            for key, item in value.items():
                key.encode("utf-8")
                validate_strings(item)
    result = json.loads(data, parse_constant=reject_constant, parse_float=finite_float,
                        object_pairs_hook=unique_object)
    validate_strings(result)
    return result


class CoreProcess:
    def __init__(self, executable, scenario="healthy", timeout=2.0):
        if scenario not in SCENARIOS:
            raise ValueError("unknown scenario")
        self.timeout = timeout
        self.responses = queue.Queue(maxsize=1)
        self.lock = threading.Lock()
        self.failed = threading.Event()
        env = {k: v for k, v in os.environ.items() if k != "GECKO_MOCK_TOKEN"}
        self.process = subprocess.Popen(
            [str(Path(executable).resolve()), "--scenario", scenario],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None,
            text=True, encoding="utf-8", errors="strict", bufsize=1, env=env,
        )
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            while True:
                line = self.process.stdout.readline(16385)
                if not line or len(line) > 16384 or not line.endswith("\n"):
                    break
                self.responses.put_nowait(line)
        except (ValueError, OSError, UnicodeError, queue.Full):
            pass
        finally:
            self.failed.set()

    def exchange(self, command):
        with self.lock:
            if self.failed.is_set() or self.process.poll() is not None:
                raise BackendUnavailable()
            try:
                self.process.stdin.write(command + "\n")
                self.process.stdin.flush()
                line = self.responses.get(timeout=self.timeout)
                data = strict_json(line)
                if not isinstance(data, dict) or type(data.get("schema_version")) is not int or data["schema_version"] != 1:
                    raise ValueError("invalid backend envelope")
                return data
            except (OSError, UnicodeError, ValueError, queue.Empty) as exc:
                self.failed.set()
                raise BackendUnavailable() from exc

    def advance(self, milliseconds):
        if type(milliseconds) is not int or milliseconds < 0:
            raise ValueError("invalid simulation time")
        return self.exchange(f"ADVANCE {milliseconds}")

    def snapshot(self):
        return self.exchange("SNAPSHOT")

    def close(self):
        self.failed.set()
        self.process.stdin.close()  # EOF permits a normal native/sanitizer exit.
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2)
        self.reader.join(timeout=2)
        self.process.stdout.close()


def validate_snapshot(raw):
    required = {"schema_version", "now_ms", "session_state", "session", "config_version", "status_version",
                "stale_after_ms", "revision", "data_session", "ready", "stale", "hardware_verified",
                "status_age_us", "completed_age_us", "frame_age_us", "observed", "stats", "last_error"}
    if not isinstance(raw, dict) or not required.issubset(raw) or type(raw.get("schema_version")) is not int or raw["schema_version"] != 1 or raw.get("hardware_verified") is not False:
        raise ValueError("invalid model provenance")
    for key in ("now_ms", "revision", "session", "data_session", "stale_after_ms"):
        if type(raw.get(key)) is not int or raw[key] < 0:
            raise ValueError("invalid model counter")
    if raw["revision"] > 2**32 - 1 or raw["stale_after_ms"] <= 0:
        raise ValueError("model range exceeded")
    for key in ("ready", "stale"):
        if type(raw.get(key)) is not bool:
            raise ValueError("invalid model flag")
    if raw["ready"] and (raw["stale"] or raw.get("observed") is None):
        raise ValueError("inconsistent model readiness")
    for key in ("status_age_us", "completed_age_us", "frame_age_us"):
        value = raw.get(key)
        if value is not None and (type(value) is not int or value < 0):
            raise ValueError("invalid model age")
    if raw.get("session_state") not in (
        "disabled", "disconnected", "go_pending", "awaiting_files", "awaiting_clock",
        "awaiting_lo", "ready", "backoff", "fault",
    ):
        raise ValueError("invalid session state")
    if (raw.get("config_version"), raw.get("status_version")) not in ((0, 0), (82, 81)):
        raise ValueError("unrecognized model binding")
    observed = raw.get("observed")
    if raw["ready"] and (raw["session_state"] != "ready" or raw["config_version"] != 82 or
                         raw["data_session"] != raw["session"] or raw["status_age_us"] is None or
                         raw["status_age_us"] >= raw["stale_after_ms"] * 1000):
        raise ValueError("unproven model readiness")
    if observed is None and (raw["revision"] != 0 or raw["data_session"] != 0 or raw["status_age_us"] is not None):
        raise ValueError("inconsistent missing state")
    if observed is not None:
        if raw["revision"] == 0 or raw["data_session"] == 0 or raw["status_age_us"] is None or raw["completed_age_us"] is None:
            raise ValueError("missing observation provenance")
        if not isinstance(observed, dict):
            raise ValueError("invalid model state")
        for key in ("temperature_c", "target_temperature_c"):
            if key not in observed:
                raise ValueError("missing model temperature")
            value = observed.get(key)
            if value is not None and (type(value) not in (float, int) or not math.isfinite(value)):
                raise ValueError("invalid model temperature")
        for key in ("heater_on", "circulation_on", "light_demand_on", "standby"):
            if key not in observed or (observed[key] is not None and type(observed[key]) is not bool):
                raise ValueError("invalid model state flag")
        if not isinstance(observed.get("pumps"), list) or len(observed["pumps"]) != 4:
            raise ValueError("invalid model channels")
        for pump in observed["pumps"]:
            if not isinstance(pump, dict):
                raise ValueError("invalid model channel")
            for key in ("output", "demand"):
                if type(pump.get(key)) is not int or not 0 <= pump[key] <= 3:
                    raise ValueError("invalid model channel enum")
    if not isinstance(raw.get("stats"), dict):
        raise ValueError("missing model counters")
    required_stats = {"attempts", "failures", "invalid_rx", "ignored_rx", "stale_events", "assembly_aborts",
                      "statuses", "tx_submitted", "tx_completed", "rx_dropped", "queue_overflows",
                      "checkpoints", "retired_trace_events"}
    if set(raw["stats"]) != required_stats or "last_error" not in raw or \
            (raw["last_error"] is not None and not isinstance(raw["last_error"], str)):
        raise ValueError("invalid model diagnostics")
    for value in raw["stats"].values():
        if type(value) is not int or value < 0:
            raise ValueError("invalid model counters")


class ModelCache:
    def __init__(self, max_age=1.0, clock=time.monotonic):
        self.lock = threading.Lock()
        self.clock, self.max_age = clock, max_age
        self.raw = None
        self.published_at = 0.0
        self.failed = False

    def publish(self, raw):
        validate_snapshot(raw)
        with self.lock:
            if self.raw is not None and (raw["now_ms"] < self.raw["now_ms"] or raw["revision"] < self.raw["revision"]):
                raise ValueError("model went backwards")
            self.raw = raw
            self.published_at = self.clock()
            self.failed = False

    def unavailable(self):
        with self.lock:
            self.failed = True

    def current(self):
        with self.lock:
            age = self.clock() - self.published_at
            if self.failed or self.raw is None or age < 0 or age >= self.max_age:
                raise BackendUnavailable()
            # Published dictionaries are immutable to callers; API renders new objects.
            return self.raw, int(age * 1000)


class Maintenance:
    def __init__(self, core, cache):
        self.core, self.cache = core, cache
        self.stop_event = threading.Event()
        self.started_at = time.monotonic()
        self.cache.publish(core.snapshot())
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        try:
            while not self.stop_event.wait(0.05):
                milliseconds = int((time.monotonic() - self.started_at) * 1000)
                self.cache.publish(self.core.advance(milliseconds))
        except (BackendUnavailable, ValueError, OSError):
            self.cache.unavailable()

    def start(self):
        self.thread.start()

    def close(self):
        self.stop_event.set()
        self.thread.join(timeout=3)
        self.core.close()
