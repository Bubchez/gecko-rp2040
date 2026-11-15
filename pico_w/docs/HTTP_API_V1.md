# Read-only HTTP JSON API v1.0

Contract **1.0.0**, finalized for T08 on 2026-11-15. Machine-readable source:
[OpenAPI 3.1 JSON](../api/openapi-v1.json). Implemented by the runnable host mock
and T09's offline-built Pico W simulation firmware. No physical communication
is included; native device networking remains unverified. This replaces
an unimplemented draft, not a previously deployed API. Agile Alert and the Pi
Zero application remain separate and unchanged.

## Scope and compatibility

The `/api/v1` namespace is read-only. Every accepted response includes
`api_version: "1.0"`. It never accepts an actuator operation, even with valid
credentials and fresh data. Adding actual control requires a separately reviewed
contract/version; the earlier draft's 202 responses, queue, command lifecycle and
idempotency promises are withdrawn from v1. No control queue/history exists.

Within v1, additive response fields and new diagnostic enum values are permitted;
clients ignore unknown response fields and handle unknown enums conservatively.
Changing units, types, names, null semantics or allowing writes requires a new
major contract. Firmware/build version, API version, marketing hardware V2 and
observed C/S filename versions are separate concepts.

The host binds only `127.0.0.1`, default port **8765**, HTTP/1.1. It is a local
integration fixture using the native C++ session core. Runtime uses Python's
standard library; validators are development dependencies only. It is not a
Pico/Wi-Fi, TLS, mDNS, cloud, MQTT or ESPHome deployment. T09's separate firmware
adapter uses DHCP IPv4 port 8765 on a trusted isolated development LAN, only after
USB credentials are provisioned during separately approved future bench work.
It is still simulated/simulation and has no physical transport. No flashing or
deployment has occurred. See [PICO_SIM_FIRMWARE.md](PICO_SIM_FIRMWARE.md).
The schema reserves
`rp2040_i2c`, `passive`, `active_read_only` and live/replay provenance for later
reviewed read-only implementations; this mock always emits simulated/simulation.

## Authentication and wire rules

- Only `GET /api/v1/health` is unauthenticated. All other routes, including command
  rejection/history and unknown paths, require one `Authorization: Bearer <token>`
  header. The scheme is case-insensitive; the token is case-sensitive and compared
  with `hmac.compare_digest`. Missing/invalid/duplicate credentials return 401 and
  `WWW-Authenticate: Bearer`. No credentials in URLs, JSON, request logs or examples.
- Local tokens are 32–128 URL-safe ASCII characters. The launcher creates a random
  local secret file without overwriting it or printing the value. No default token.
  Plain HTTP does not encrypt a bearer token; the host mock therefore cannot bind
  to a LAN address. Pico simulation firmware uses a separately configured token
  and requires an isolated trusted LAN; it provides no TLS or Internet exposure.
  No CORS headers/browser access are enabled. Agile Alert should call
  through its own developer/server-side integration, not expose this token publicly.
- UTF-8 JSON responses use `Content-Type: application/json`, `Cache-Control: no-store`,
  exact Content-Length and `Connection: close`. One request per connection; a client
  reconnects for subsequent requests. No pipelining, redirects, query parameters,
  percent-encoded routes, fragments, absolute targets or `//` path normalization.
- Request limits: 2048 aggregate header/request-line bytes, 128 URI bytes, 512 body
  bytes; response maximum 8192 bytes. Two concurrent connections. Excess connections
  may close before an HTTP response. Header/body processing has a shared absolute
  2 s receive deadline. There is no idle keepalive connection.
- A body requires exactly one decimal Content-Length. Duplicate lengths, any
  Transfer-Encoding or Expect, invalid Host, missing POST length or a GET body
  return 400. No automatic interim 100 response is sent before policy checks.
  Oversize body/URI/headers return 413/414/431; receive timeout returns 408 when the
  connection still permits a reply. JSON content type accepts `application/json`
  or `application/json; charset=utf-8`; encoded/compressed bodies return 415.
- Reject duplicate JSON keys, invalid UTF-8, unpaired Unicode surrogates, NaN/Infinity,
  overflowing float exponents and trailing data. Malformed JSON returns 400. A valid
  JSON value other than an object on the command rejection route returns 422.
- Aggregate authenticated request limit defaults to 5/s, burst 10. Excess returns
  429 with `Retry-After: 1`. Poll status initially every 2 s; use 10–30 s backoff on
  bridge failure. Read-only rejection never grants permission to retry an actuator.

## Routes

| Method/path | Result |
|---|---|
| `GET /api/v1/health` | 200 minimal HTTP/application liveness; no pack readiness claim. |
| `GET /api/v1/capabilities` | 200 identity, effective read-only policy, available field families and current synthetic profile binding. |
| `GET /api/v1/status` | 200 latest state, nulls, provenance and freshness, including when modeled pack/session is unavailable or faulted. |
| `GET /api/v1/diagnostics` | 200 bounded session/transport/HTTP counters and explicit unmeasured fields. |
| `POST /api/v1/commands` | 403 `read_only` for every authenticated well-formed JSON object; **never 202/2xx**. |
| `GET /api/v1/commands/{request_id}` | 404 `unknown_request`; nothing is accepted or retained in v1. |

Unknown routes return 404 after authentication. Wrong methods return 405 with
Allow; no reset, refresh, arming, scenario switching, raw-I2C, firmware-upload or
write-enable route exists. `request_id` is `[A-Za-z0-9_-]{1,64}`. On command denial
it is echoed only when a valid string was supplied; otherwise it is null. The
command object's other keys/parameters are deliberately not a control schema:
all objects are denied, including unknown functions and invalid target values.

GET reads an immutable cache. It cannot tick the core, start GO, ACK a frame,
refresh status, reset a session or change a scenario. Background maintenance alone
owns the native process (or advances the shared model in Pico firmware).
A dead/unresponsive backend, bad backend envelope, expired
cache (1 s) or out-of-range model revision returns **503 bridge_unavailable** with
Retry-After. Do not substitute fake state. Health may remain 200 because the HTTP
loop is live. A modeled spa fault with a healthy backend instead remains status 200
with ready false/stale metadata. Authenticated commands still return 403 if the
backend is unavailable, because execution is never possible.

## Types, freshness and unavailable data

The OpenAPI component schemas enumerate every required field and nullable type.
Response objects permit additive fields. Principal mappings:

| Field | Meaning |
|---|---|
| `device_id` | Stable configured host identity, default gecko-host-mock-001; not a discovered pack identifier. |
| `boot_id` | Opaque random 32-character identifier per mock start. Compare revisions only within the same boot. |
| `state_revision` | uint32, 0 before a complete decoded status; increases only for an accepted whole snapshot. Overflow fails closed rather than wrapping. |
| `transport`, `mode` | Host always simulated/simulation, including unavailable/stale/fault fixtures. |
| `connection.session_state` | Core session state: disabled/disconnected/go_pending/awaiting_files/awaiting_clock/awaiting_lo/ready/backoff/fault. |
| `connection.ready` | Completed synthetic handshake/profile binding plus fresh whole status in the current epoch. It does not enable control or establish hardware compatibility. |
| `last_status_age_ms` | Conservative age since the oldest fragment of the accepted status. Never refreshed by final-part arrival alone, noise, partial data or HTTP polling. |
| `status_completed_age_ms` | Separate age since the accepted assembly completed. Null until an observation exists. |
| `last_valid_frame_age_ms` | Age of last accepted candidate frame in this synthetic model, not proof of wire integrity. |
| `quality.valid` | A complete decoded state exists; may stay true when stale/faulted. |
| `quality.stale` | No current ready/fresh snapshot, expired age, lost binding or old session. Retained old values are always visibly stale during recovery. |
| `quality.source` | Host unknown before observation, then synthetic. No fixture is called a hardware capture or live data. |
| `invalid_fields` | JSON Pointer paths relative to state for supported but invalid/null observed fields, e.g. `/temperature_c`. |
| `unavailable_fields` | Paths for fields this adapter does not publish. Unavailable is not false, zero or a capability guess. |
| `cached_fields` | Empty now; no reminder flash cache exists. Reserved to label historical values in later read-only implementations. |

Ages are nonnegative integer milliseconds, rounded down from native microseconds;
HTTP adds elapsed cache time conservatively. Wall time is not used for expiry.
Mock stale threshold is **2000 ms**, a fixture policy, not a measured pack deadline.
A stale snapshot keeps its values and revision; only a new accepted status refreshes
it. Reconnection retains boot identity and requires full new handshake/status.
Restarting the mock creates a new boot ID and revision sequence.

Temperatures are finite Celsius numbers or null; rounding belongs to the client.
Heater, circulation, light demand and standby are nullable booleans. Light demand
is not lamp feedback. Pump output/demand use semantic off/low/high/unknown strings;
their wire mappings are distinct. Four entries describe synthetic protocol channels,
**not discovered installed equipment**. `capabilities.equipment.pump1..pump4` remain
null; no command capability or fitted pump is inferred from a channel value.

Program, blower, waterfall, lock mode, pack type, pump timer, published spa clock
and reminders are unavailable in the current core-to-HTTP snapshot adapter. Their
state keys/objects stay present with null values; reminder source stays unknown.
The clock packet used by the synthetic handshake is not published as a continuously
observed clock. Do not populate these fields from earlier documentation examples.
Before any status, observed scalars are null and pumps is empty.

`profile.config_file/status_file/layout` are null until the current handshake has
established its explicit candidate binding. Validation is synthetic, never a pack
validation claim; the target_model string is the project target, not observed model
identification. `read_fields` advertises only implemented read families. All command
entries have supported/enabled false and reason read_only. Command queue/history/
retention limits are zero. Measured hardware timing, stack/free RAM and last-error
age remain null. Counters saturate at uint32 maximum. Scenario/simulation time in
diagnostics are synthetic metadata, not spa time; non-mock implementations may use
null. Native firmware optionally adds `wifi` (state/attempts/reconnects) and `safety`
(inactive Gecko GPIO, physical I2C/actuator execution compiled false). These optional
properties are documented in OpenAPI without changing required v1 fields/version.
RAM/stack diagnostics remain null until measured on hardware; artifact memory
budgets are static build evidence. Firmware default fixture is healthy; it has no
HTTP scenario switch. The host-only loopback restriction remains unchanged.

## Errors

Every parsed error response follows this shape (request_id can be null):

```json
{"api_version":"1.0","error":{"code":"read_only","message":"read only","request_id":"client-1","retryable":false}}
```

Codes include invalid_http, invalid_json, invalid_parameters, unauthorized,
not_found, unknown_request, method_not_allowed, read_only, body_not_allowed,
body_too_large, uri_too_long, headers_too_large, request_timeout,
unsupported_media_type, rate_limited, bridge_unavailable and internal_error.
Only temporary 429/503 errors set retryable true. No accepted command or uncertain
actuator outcome is created by any error. Raw socket refusal/closure can precede
JSON when connection limits or a disconnected peer prevent a response.

## Draft-to-final decisions

- Freeze OpenAPI 3.1 plus nullable JSON Schema and executable contract tests.
- Finalize a strictly read-only v1; remove draft acceptance/queue/idempotency behavior.
- Use truthful supported read fields and explicit unavailable/invalid paths.
- Preserve T06–T07 oldest-fragment freshness and independent background ownership.
- Use loopback port 8765, Connection close and absolute bounded request processing
  for the host. No implication of Pico resource, networking or TLS validation.

See [HOST_MOCK.md](HOST_MOCK.md) for startup and scenarios and
[AGILE_ALERT_HANDOFF.md](AGILE_ALERT_HANDOFF.md) for the integration handoff.
