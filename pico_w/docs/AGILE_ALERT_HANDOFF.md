# Agile Alert integration handoff — read-only v1

This is an integration specification and local mock, not a change to Agile Alert.
No Agile Alert files, accounts, deployment, notifications or configuration were
modified. The separately developed Pi Zero application is also unchanged.

## Inputs for the integration developer

- Contract: [HTTP_API_V1.md](HTTP_API_V1.md).
- Machine-readable OpenAPI 3.1 / JSON Schema: [openapi-v1.json](../api/openapi-v1.json).
- Local executable mock and scenario startup: [HOST_MOCK.md](HOST_MOCK.md).
- Actual test results and limitations: [VALIDATION.md](VALIDATION.md).

Configure the developer integration with a base URL (default
`http://127.0.0.1:8765`) and bearer token loaded from the local secret file. Keep
the token in server-side/local configuration, outside source control. There are
no built-in production credentials or public URLs. The mock accepts only local
connections and emits no CORS headers. Do not send it browser credentials or
assume it is reachable from another machine; this milestone adds no LAN tunnel.

## Suggested integration flow

1. Read health for HTTP liveness, then authenticated capabilities. Require
   api_version 1.0 and explicitly inspect transport/mode/provenance. This fixture
   must be labeled **simulated**, including during recovery or missing-data cases.
2. Poll authenticated status every 2 s. A 200 response can contain unready/stale
   state; require `connection.ready`, `quality.valid` and not `quality.stale` for
   a current-state display. Health 200 is not pack connectivity.
3. Interpret nullable fields before formatting. Display unavailable/invalid values
   as an unavailable marker, not zero °C, false/off or an installed-device claim.
   Distinguish unknown fields from an actual observed false boolean.
4. Retain stale observations only with an unmistakable stale indication. Use
   oldest-fragment `last_status_age_ms`; `status_completed_age_ms` has a different
   meaning and cannot substitute for freshness. Partial updates never make data fresh.
5. Compare revisions only within the same boot ID. A higher revision means a new
   whole decoded snapshot, not an actuator acknowledgement. On boot ID change,
   invalidate local current-state assumptions and reread capabilities/status.
6. Honor Retry-After on 429/503 and back off to 10–30 s. 401 requires credential
   correction. A 503 means the bridge/cache cannot provide current state; the
   client must not replace it with invented values. Modeled pack faults instead
   remain 200 with false readiness and stale metadata.

## Controls must remain disabled

`read_only` is true and `physical_control_enabled` is false. Every command
capability is supported/enabled false. Do not enable controls because temperatures
or pump channels look plausible, or because a snapshot is ready. Pump entries are
candidate protocol channels; equipment presence is null and no supported pump
speed/control capability is inferred.

There is no accepted command, queue/history, idempotency behavior, raw write,
heater override, clock setter or scenario-switch route in v1. Every authenticated
well-formed JSON command object returns 403 read_only. Do not repeatedly retry a
denied operation. The mock's command rejection endpoint can verify that an
integration handles the denial; it never exercises real control.

## Local acceptance checklist

| Fixture/check | Expected integration behavior |
|---|---|
| Missing/wrong token | 401, no protected data; correct local credential. |
| Healthy | Label synthetic; render current temperatures/observed flags, hide controls. |
| Unavailable | Null measurements, revision 0, ready false; no fabricated defaults. |
| Invalid temperature | Show actual temperature unavailable while target remains 37 °C; inspect invalid_fields. |
| Stale | Keep prior values only with stale marker, ready false, unchanged revision. |
| Recovery | Lose readiness on modeled fault; recover only after full new session/status; boot ID stable and revision advances. |
| Mock restart | New boot ID; clear current-state assumptions and reread capabilities. |
| Command request | 403 read_only, no Location/acceptance and no core state/transport change. |
| Backend process lost/cache expired | 503; health can still be 200. Back off and await/restart the local mock. |

Code generators may use the OpenAPI file. Responses allow additive properties;
clients should ignore unknown fields and handle unknown enum values conservatively.
Current program, blower/waterfall, lock, pack type, timer, published clock and
reminders are explicitly unavailable. Target-model and candidate XML filenames
are not hardware compatibility certification.

The handoff is ready for a separately authorized Agile Alert integration task.
This repository task stops before T09 and contains no physical deployment or
Agile Alert implementation work.
