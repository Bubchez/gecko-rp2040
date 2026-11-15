# Proposed HTTP JSON API v1

Contract draft **0.1**, 2026-11-15; first release will use `/api/v1`.
No endpoint is implemented by this planning deliverable. The user confirmed that
the separate Pi Zero W application has no existing bridge contract. This proposal
is for that developer to review; no changes to the Pi application are required
or made here. Freeze a machine-readable OpenAPI/JSON Schema with contract tests
before either side treats it as implemented compatibility.

## Network roles, versioning and security

Pico W is a Wi-Fi station and HTTP server; Pi Zero W is the client, pulling status
and issuing future explicit commands. No cloud, MQTT, ESPHome native API or
Home Assistant dependency. Use configurable host/IP and port (proposed port 80);
optional mDNS `gecko-<device-id>.local` is convenience, not the only discovery path.
Both devices must reach each other on the LAN. The Pi app's server can proxy this
connection for its browser clients; browser CORS is disabled by default. A read-only
capability endpoint does not require browser access to firmware internals.

Paths carry the major API version. All JSON success envelopes include
`api_version: "1.0"`. Additive optional response fields and new capability names
may be added within v1; clients ignore unknown response fields and handle unknown
enum strings as unsupported. Rename/remove fields, change units/types or change
existing meanings only in v2. Command schemas reject unknown fields to catch typos.
Firmware version and Gecko C/S filenames are independent of API version.

All routes except minimal `/api/v1/health` require
`Authorization: Bearer <provisioned-device-token>`. Missing/invalid token returns
401 plus `WWW-Authenticate: Bearer`. Provision via USB/local configuration; no
default token, no token in URLs, logs, diagnostics or example files. Compare tokens
without early-exit timing differences. Configuration/arming/reboot/firmware upload
are not exposed by this API. Use separate deployments for simulation and physical
hardware; clients must inspect transport mode on every boot.

Bearer authentication over plain HTTP does **not** provide confidentiality or
protect against a LAN observer replaying a captured token. The proposed initial
deployment is a restricted trusted network with no Internet port forwarding and
physical writes disabled. Before control, review the actual network threat model:
use a measured TLS configuration on Pico if untrusted peers can reach/observe it,
or keep the bridge on a controlled isolated link with the Pi terminating user TLS.
A Pi browser-facing TLS proxy alone does not encrypt the Pi-to-Pico hop. Do not
claim ESPHome's encrypted API is preserved by this HTTP design. TLS implementation
and SRAM/latency cost need a separate measured decision, not a silent omission.

## Shared transport and representation rules

- HTTP/1.1, UTF-8 JSON, `Content-Type: application/json`; responses include
  `Cache-Control: no-store`. Only one request per connection at a time; no pipelining.
- Proposed hard caps: two concurrent connections, request headers 2,048 bytes,
  JSON body 512 bytes, URI 128 bytes, response 8,192 bytes. Header/body receive
  deadline 2 s, idle keepalive 5 s. Bodies require Content-Length; unsupported
  chunked requests return 400. No bodies on GET; no query parameters in v1.
  Return 413 for oversized body, 414 for URI, 431 for headers; 408 on receive timeout
  when a response can still be sent. Exceeding connection capacity can close/refuse
  the new connection; an HTTP 503 is not guaranteed before a request is parsed.
- Reject duplicate JSON keys, invalid UTF-8, trailing non-whitespace, null for required
  fields, wrong types, NaN/Infinity, and numeric strings in numeric fields. Invalid
  JSON is 400; well-formed JSON with invalid schema/values is 422.
- All temperatures are Celsius, finite JSON numbers. Device conversion preserves
  1/18 °C precision internally. Display rounding belongs to the client.
- Unknown/unobserved fields are JSON null, never fabricated zero, false or NaN.
  Fields are always present in the status schema; unsupported equipment is listed
  by capabilities. Retain valid old observations with explicit stale/cache metadata.
- IDs are ASCII strings. `device_id` persists across boots; `boot_id` is a new random
  opaque ID each boot. `state_revision` is an integer 0..2^32−1 increasing only on a
  newly validated status snapshot; reboot before wrapping. Clients compare revisions
  only within the same boot. Cached reminders do not make a current state revision fresh.
- All ages/intervals are nonnegative integer milliseconds from monotonic time.
  `uptime_ms` is monotonic per boot. Wall-clock values are ISO 8601 strings or null;
  the spa clock is local time without a fabricated UTC suffix or host timezone.
- Start Pi polling at 2 s, back off to 10–30 s on faults, and use `Retry-After` when
  supplied. Proposed aggregate device limit: 5 authenticated requests/s with burst 10;
  command creation further limited to 1/s with burst 2. Excess returns 429.
- GET routes have no I²C side effects. They never start GO, refresh the pack, reset
  hardware or acknowledge a protocol message. Session maintenance is independent.

## Endpoints

| Method/path | Purpose | Success |
|---|---|---|
| `GET /api/v1/health` | Minimal firmware liveness; no token, no configuration/secrets | 200 |
| `GET /api/v1/capabilities` | Identity, transport mode, current read/write permissions and validation | 200 |
| `GET /api/v1/status` | Latest observed state plus connection/freshness | 200, even if spa is offline |
| `GET /api/v1/diagnostics` | Bounded counters and last classified errors | 200 |
| `POST /api/v1/commands` | Future typed, asynchronous desired-state action | 202 when accepted |
| `GET /api/v1/commands/{request_id}` | Retrieve one retained command outcome | 200 or 404 |

Unknown path is 404; unsupported method is 405 with Allow. Write routes remain
specified for future compatibility, but a read-only deployment returns 403 for
every otherwise valid command. No PUT/PATCH/raw-write/toggle/refresh/reset endpoint.
Do not infer bridge-to-pack health from HTTP 200 or Wi-Fi connectivity.

### Health

```json
{
  "api_version": "1.0",
  "service": "gecko-pico-bridge",
  "boot_id": "sim-boot-001",
  "uptime_ms": 120000,
  "live": true
}
```

`live` means the application loop is responsive. Spa readiness appears on status.
Boot and uptime are diagnostic only; no credentials, SSID or network configuration
are exposed on this unauthenticated route.

### Capabilities

```json
{
  "api_version": "1.0",
  "device_id": "pico-bridge-001",
  "boot_id": "sim-boot-001",
  "firmware_version": "0.1.0-sim",
  "transport": "simulated",
  "mode": "simulation",
  "read_only": true,
  "physical_control_enabled": false,
  "profile": {
    "target_model": "IN.YE-3-H3.0 / YE-3-CE V2",
    "config_file": "inYT_C82.xml",
    "status_file": "inYT_S81.xml",
    "layout": "inyt-v51-candidate",
    "validation": "synthetic"
  },
  "read_fields": ["temperatures", "heater", "light_demand", "circulation", "pumps", "program", "spa_clock", "reminders"],
  "commands": {
    "light.set": {"supported": false, "enabled": false, "reason": "unvalidated_hardware"},
    "temperature.set": {"supported": false, "enabled": false, "reason": "unvalidated_hardware", "min_c": 26, "max_c": 40, "step_c": 0.5},
    "program.set": {"supported": false, "enabled": false, "reason": "unvalidated_hardware"},
    "pump.set": {"supported": false, "enabled": false, "reason": "unvalidated_hardware", "pump_ids": []}
  },
  "limits": {"command_queue": 8, "command_history": 32, "idempotency_retention_ms": 900000}
}
```

`transport` = `simulated | rp2040_i2c`; `mode` = `simulation | passive |
active_read_only | control`. `read_only` reflects effective API write policy;
`physical_control_enabled` is always false for simulation. Simulated write tests
can set read_only false only in a clearly labeled simulator configuration, and
cannot authorize a physical driver. No route switches modes or arms hardware.

`profile` filename/layout values can be null. Validation is `unknown | synthetic |
bench | pack_read_only | pack_control`; this records provenance, not permission by
itself. `supported` means this firmware/profile implements an evidenced command;
`enabled` additionally reflects current policy/readiness/local arm. Freshness can
change between capability fetch and command submission. Candidate layouts with
only synthetic evidence cannot enable physical commands. The hardware revision
V2 must never be converted to an assumed C/S version.

Every command name above must be present; extra names may be added. Pump IDs
advertise only validated fitted pumps, never all four by default. `pump.set` may
later include a `states_by_pump` map of supported semantic states; until this is
validated, leave pump_ids empty. Program values, when supported, are `away`,
`standard`, `energy`, `super_energy`, `weekend`. No direct heater command is defined.

### Status and freshness

```json
{
  "api_version": "1.0",
  "device_id": "pico-bridge-001",
  "boot_id": "sim-boot-001",
  "state_revision": 12,
  "transport": "simulated",
  "mode": "simulation",
  "connection": {
    "bus_state": "healthy",
    "session_state": "ready",
    "last_valid_frame_age_ms": 300,
    "last_status_age_ms": 300,
    "stale_after_ms": 60000,
    "ready": true
  },
  "quality": {
    "valid": true,
    "stale": false,
    "source": "synthetic",
    "invalid_fields": [],
    "cached_fields": []
  },
  "state": {
    "temperature_c": 36.5,
    "target_temperature_c": 37.0,
    "heater_on": false,
    "standby": false,
    "light_demand_on": true,
    "circulation_on": false,
    "blower_on": null,
    "waterfall_on": null,
    "pumps": [
      {"id": "pump1", "state": "off", "user_demand": "off"}
    ],
    "program": "standard",
    "lock_mode": "unlocked",
    "pack_type": "inYT",
    "pump_timer_minutes": 0,
    "spa_clock": {"local_datetime": null, "local_time": "15:17:25", "timezone": null},
    "reminders": {
      "rinse_filter": {"due_date": null, "source": "unknown", "age_ms": null},
      "clean_filter": {"due_date": null, "source": "unknown", "age_ms": null},
      "change_water": {"due_date": null, "source": "unknown", "age_ms": null},
      "spa_checkup": {"due_date": null, "source": "unknown", "age_ms": null}
    }
  }
}
```

This is an illustrative **synthetic** snapshot, not a claim about actual fitted
equipment. Unknown equipment does not become absent simply because its value is null.

Normative types and meaning:

| Field/group | Type and semantics |
|---|---|
| `state_revision` | Integer; 0 until a complete validated status snapshot exists. |
| `connection.bus_state` | `disabled | unknown | healthy | degraded | fault`; indicates transport observations, not a physical safety certification. |
| `connection.session_state` | Session enum from implementation plan: disabled/disconnected/go_pending/awaiting_files/awaiting_clock/awaiting_lo/ready/backoff/fault. |
| Frame/status ages | Integer or null; status age changes only on accepted complete status, never on random I²C traffic or GET. Null if no observation in this boot. |
| `connection.ready` | True only with a known read profile, completed session and fresh valid status. Does not imply actuator permission. In passive mode it stays false if handshake was not established. |
| `quality.valid` | A successfully decoded complete state exists for the selected profile. It can remain true when stale; a bus fault still prevents readiness. |
| `quality.stale` | True if no valid status yet, or status age ≥ stale_after_ms. Loss of session/fault separately forces ready false immediately. |
| `quality.source` | `unknown | synthetic | replay | live`; replay is previously captured hardware data, never current state. |
| Invalid/cached field arrays | JSON Pointer strings relative to state, e.g. `/temperature_c` or `/reminders/rinse_filter`. Invalid values are null; cached values are explicitly historical. |
| Temperatures | Number or null; independent validity per field; target range as observation can exceed permitted API write range without changing write policy. |
| Heater/standby/light-demand/circulation/blower/waterfall | Boolean or null. Heater is observed output; light_demand is demand, not guaranteed lamp feedback. |
| Pumps | Array of known equipment, empty until known; each id `pump1`..`pump4`; `state` and `user_demand` each `off | low | high | unknown` or null. Map wire status/demand enums separately; never expose raw 1/2 as speed semantics. |
| Program | Program enum above or null. |
| Lock mode | `unlocked | partial | full | unknown` or null. |
| Pack type | Observed name string or null; unrecognized code can be exposed in diagnostics, not guessed. |
| Pump timer | Integer 0..255 minutes or null for unverified/missing units. |
| Spa clock | Always object; local_datetime `YYYY-MM-DDTHH:MM:SS` or null; local_time `HH:MM:SS` or null; timezone IANA name only if explicitly configured/known, otherwise null. No automatic clock writes. |
| Reminder | Always four entries; due_date `YYYY-MM-DD` or null; source `unknown | observed | cached`; age_ms integer or null if not measurable across boot. Cache is not fresh pack evidence. |

On boot, return all state scalars null, pumps empty, quality.valid false, stale
true, source unknown, revision 0 and null ages. Simulator source becomes synthetic
or replay when its data arrives. A corrupt frame increments diagnostics and does
not update observed state/revision/age. Previous valid values may remain visible
but their age keeps increasing; per-field corruption is reported through invalid_fields.
On reboot no actuator state is restored as current. Reminder cache may be displayed
with source cached and listed in cached_fields. GET status returns 200 with these
quality fields even if the pack is unavailable; 503 means the bridge cannot serve
the request, not merely that the spa is offline.

### Diagnostics

```json
{
  "api_version": "1.0",
  "boot_id": "sim-boot-001",
  "uptime_ms": 120000,
  "counters": {
    "rx_frames": 42,
    "valid_frames": 40,
    "invalid_frames": 2,
    "checksum_errors": 1,
    "rx_dropped": 0,
    "assembly_aborts": 1,
    "tx_failures": 0,
    "arbitration_losses": 0,
    "timeouts": 0,
    "reconnects": 0
  },
  "high_water": {"rx_queue": 2, "command_queue": 0},
  "timing_us": {"max_irq_service": null, "max_role_switch": null, "max_handshake_ack": null},
  "memory": {"min_free_bytes": null, "stack_headroom_bytes": null},
  "last_error": {"code": "assembly_timeout", "age_ms": 4000}
}
```

Counters are boot-scoped nonnegative integers, saturating at 2^32−1. Timing/memory
measurements are nonnegative numbers or null if not measured. `last_error` is null
until an error occurs. Do not leak auth tokens, Wi-Fi credentials, unbounded frame
dumps or network clients. Raw capture export, if later needed, is separate reviewed
local tooling and not a raw control route.

## Future command contract (disabled in initial physical releases)

```json
{
  "request_id": "pi-zero-000123",
  "expected_boot_id": "sim-boot-001",
  "expected_state_revision": 12,
  "type": "temperature.set",
  "parameters": {"temperature_c": 37.0},
  "ttl_ms": 5000
}
```

All six fields are required. request_id matches `[A-Za-z0-9_-]{1,64}`. Parameters
must match exactly one supported schema below, with no additional fields.
ttl_ms is integer 1,000..10,000 (no default) counted from server acceptance using
monotonic time. The boot ID/revision must match the current accepted snapshot at
enqueue or return 409. Recheck freshness/profile and shared-demand generation when
executing; do not blindly apply an old revision's shared pump byte.

| Type | Exact parameters | Validation |
|---|---|---|
| `light.set` | `{"on":true}` | Boolean, no toggle semantics. |
| `temperature.set` | `{"temperature_c":37.0}` | Finite number, 26..40 inclusive, 0.5 °C steps; capability can narrow range. Reject instead of silently clamping. |
| `program.set` | `{"program":"standard"}` | Exact lowercase program enum. |
| `pump.set` | `{"pump_id":"pump1","state":"off"}` | Only advertised fitted/validated pump and advertised semantic state; shared-demand serialization required. |

No command controls heating elements directly. No unknown/experimental functions,
schedule programming, reminder reset, or clock-setting command is part of v1.0.
Schema presence alone never enables an unsupported command.

Acceptance 202, `Location: /api/v1/commands/pi-zero-000123`:

```json
{
  "api_version": "1.0",
  "boot_id": "sim-boot-001",
  "request_id": "pi-zero-000123",
  "type": "temperature.set",
  "parameters": {"temperature_c": 37.0},
  "status": "queued",
  "accepted_uptime_ms": 120000,
  "expires_uptime_ms": 125000,
  "confirmation": null,
  "error": null
}
```

The same object schema is returned by GET command. Status transitions:
`queued -> transmitting -> awaiting_observation -> confirmed | failed | expired |
indeterminate`. Already-observed no-op requests can go directly to confirmed
without writing, if freshness/preconditions still hold. Distinguish failures:

- `failed`: definitely not applied/sent or rejected before bus transmission.
- `expired`: TTL elapsed before transmission; it must never subsequently be sent.
- `indeterminate`: data may have reached pack but result/readback is unknown,
  including TTL expiry after possible send. Stop automatic retry.
- `confirmed`: fresh status after the attempt matches the desired value, or fresh
  preexisting state already matched. This is observed convergence, not causal proof.

For confirmed, `confirmation` is
`{"kind":"observed_state","state_revision":13,"observed_uptime_ms":120500}`
or the same shape with kind `already_satisfied`. Otherwise it is null. Terminal
failure `error` is `{"code":"transport_timeout","message":"Outcome unknown after write"}`
or another documented code. Do not expose raw exception strings. A TX ACK alone
never produces confirmed. Response does not overwrite `/status` requested fields.

Idempotency: retain accepted IDs and normalized full request bodies for at least
900,000 ms after acceptance in the same boot; never evict a nonterminal record.
Retrying an identical accepted request returns the existing object with HTTP 200,
without another write or renewed TTL. Reusing its ID with any differing request
field returns 409. Deduplicate after auth/schema validation but before rechecking
current revision/TTL/preconditions so retries can retrieve an earlier result.
GET returns 404 once evicted or if unknown; client must not infer it was unsent.
History capacity 32; when all records are retained/nonterminal reject new work
with 429 and Retry-After instead of silently forgetting deduplication entries.
Queue capacity is eight and queue full returns 503 before accepting the request.

No deduplication survives a reboot. If an HTTP response is lost, query the same ID
and boot first. If boot changed or result is unknown/indeterminate, fetch current
state and reconcile with the user/application policy; never blindly resend an old
command under a fresh ID. Wi-Fi reconnect must not replay actuator requests.

## Errors and request processing order

```json
{
  "api_version": "1.0",
  "error": {
    "code": "read_only",
    "message": "Actuator commands are disabled",
    "request_id": "pi-zero-000123",
    "retryable": false
  }
}
```

error.request_id is null if none was validated. Minimal non-JSON errors are allowed
only before a valid HTTP request can be parsed (e.g. broken connection). Process:
HTTP bounds/method -> auth -> JSON/schema -> dedup -> effective read-only policy ->
capability -> boot/revision -> session/freshness/armed -> rate/capacity -> enqueue.
No error rejection in these checks may send a bus transaction. GET unknown/method
checks do not reveal protected data.

| HTTP | Code examples | Client handling |
|---|---|---|
| 400 | `invalid_json`, `duplicate_key`, `invalid_http` | Correct request; no automatic retry. |
| 401 | `unauthorized` | Correct credentials. |
| 403 | `read_only`, `control_not_armed` | No automatic retry; cannot arm through API. |
| 404 | `not_found`, `unknown_request` | Correct path or reconcile command outcome. |
| 405 | `method_not_allowed` | Use Allow method. |
| 408/413/414/431 | `request_timeout`, `body_too_large`, `uri_too_long`, `headers_too_large` | Fix size/timing. |
| 409 | `boot_changed`, `state_changed`, `request_id_conflict` | Refetch status or inspect prior command; avoid blind resend. |
| 415 | `unsupported_media_type` | Send application/json. |
| 422 | `invalid_parameters`, `unsupported_command`, `unsupported_capability` | Read capabilities; correct request. |
| 429 | `rate_limited`, `command_history_full` | Retry-After; same ID when acceptance might be ambiguous. |
| 503 | `spa_not_ready`, `stale_state`, `transport_fault`, `queue_full`, `bridge_busy` | Back off; refresh status; obey Retry-After. |
| 500 | `internal_error` | Report fault; reconcile any potentially accepted command. |

retryable is true only for temporary 429/503 conditions and is not permission to
resend an indeterminate actuator write. A profile that cannot be identified returns
spa_not_ready for commands; hardware-independent GET status remains available.

## Contract acceptance before use by the Pi application

1. Publish an agreed OpenAPI/JSON Schema 1.0 and exact example fixtures for boot,
   simulation, live, stale, unknown profile, authentication and read-only rejection.
2. Run a host mock using the same core/schemas; Pi developer tests against it
   independently. Keep the Pi application's code outside this repository's changes.
3. Validate unit/null/enum semantics, polling behavior, reboot/ID conflict and
   request-lost-after-acceptance cases. Confirm that stale data is visibly stale.
4. Enable only status routes initially; require integration tests proving every
   physical command is denied. Later command routes need the separate physical
   validation and control gates in the implementation plan.
