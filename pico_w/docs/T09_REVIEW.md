# T09 integration review

Reviewed T08 at `177d2ca6f6815c5655442b747a82a0102a5298d6`. All 35 original
regression groups passed after extracting the shared model. No original Arduino,
ESPHome, examples/assets/tools or external web application implementation changed.
This review concerns firmware compilation/offline validation only.

## Blocking port boundaries and resolutions

| Finding | Resolution / evidence |
|---|---|
| T08 scheduler is embedded in a host executable, uses exceptions, and API rendering is Python-only. A second handwritten Pico fixture would drift from the session core. | Extract `simulation::Model` with bounded explicit error state; both host IPC and Pico use it. A separate allocation-free native HTTP adapter publishes the same core snapshots. Differential tests compare all v1 response fields across five scenarios and multiple epochs. |
| Pausing or rebasing synthetic time after blocking SDK/configuration work could make retained state appear fresh. | Shared maintenance advances at most 100 ms per invocation, preserves elapsed wall time, and invalidates HTTP cache until caught up. Backward time rejects. GET cannot invoke maintenance; ownership and stalled/catch-up tests exercise 503 and recovery. |
| RP2040 uses 32-bit size_t/uint32_t. A 10-digit Content-Length can overflow; host compiler type assumptions are insufficient. | Saturate parsed length before overflow; reject >512. Cross compilation also caught/fixed mixed `unsigned`/`uint32_t` template assumptions. Native tests include `9999999999`; no buffer truncation. |
| SDK default stack is only a 4 KiB scratch bank; SDK 2.2.0 default `__HeapLimit` reaches the end of main RAM. Moving stack without reserving heap space would overlap. | Independently authored linker extension reserves 16 KiB stack, explicit 8 KiB libc heap and >=64 KiB unused gap. Initial incorrect reservation failed the link assertion; final symbols/budget are checked by the build tool. SDK source remains unmodified. |
| Pico printf defaults have different floating formatting limits from the host adapter. | Select compiler/newlib printf explicitly for finite Celsius JSON serialization; project build has no exceptions/RTTI. Actual ARM execution/float output remains a hardware test. |
| HTTP response storage and lwIP PCB lifetimes differ from the threaded host mock. | Fixed slots, copied writes, bounded send windows/deadlines, ACK-driven closure and callback detachment before close/abort. Actual firmware adapter is host-tested using independently authored callback doubles for partial RX/TX, ERR_MEM, half-close, remote error, timeout, capacity and teardown. This does not simulate radio/lwIP internals. |
| Reusing a main-loop timestamp after SDK callbacks can make a newly accepted/replied connection appear to have backward time or an unsigned timeout. | TCP polling samples the injected/SDK clock per connection, and Wi-Fi policy samples after SDK polling. Callback tests advance time between loop entry and RX/preparation; a new response remains active. |
| Parallel cross-build/test load exposed a T08 test-fixture race: reading a complete Content-Length body can precede worker-slot release, so rapid sequential tests unintentionally hit the documented two-connection refusal. | Test fixture waits for both previous workers to release slots before its next sequential request. No retry, weakened assertion, production limit or server behaviour change. Failed load-run transcripts are retained separately; final suites run after cross-builds finish. |
| Credentials must not become build constants, logs or network configuration endpoints. Interrupted persistence must not silently acknowledge an unpersisted secret. | Local no-echo USB parser, strict fixed records, CRC/version/sequence, alternate-sector write and readback. Tests mutate every record byte and every torn-write cut. No response contains SSID/password/token. Hardware flash behaviour remains unverified. |
| Source-only MIT attribution is insufficient for an SDK-linked UF2. | Pin dependencies, use RP2040-specific CYW43 grant, preserve BSD/MIT/Apache/runtime notices alongside UF2. Do not reuse unresolved legacy source. See LICENSING.md and third_party/README.md. |

These are integration constraints and defects found while implementing the native
target, not new evidence that the original Gecko interpretation is correct.
The previously frozen API field names/units/nulls, synthetic quality labels,
authentication boundary, read-only policy and unavailable semantics are retained.
Firmware adds Wi-Fi/safety diagnostics and a target-specific firmware_version;
both are permitted by v1's additive compatibility rules. OpenAPI remains 1.0.0.

## Boundaries deliberately retained

- Candidate-v51 profile and C82/S81 binding stay explicit synthetic fixtures. There
  are still zero hardware captures and no verified inbound generic integrity rule.
- No raw transport substitution, I2C source/link dependency, actuator dispatcher,
  control-enabled API variant, HTTP provisioning/OTA or fitted-equipment discovery.
- Native response schema/differential/strict-JSON tests are host execution of the
  same project parser/serializer compiled into ARM firmware. Cross-build success
  is not proof of ARM runtime, SDK allocator, USB, Wi-Fi or electrical timing.
- Device HTTP is an isolated-LAN simulation fixture with plaintext bearer auth;
  existing host mock remains loopback only. External applications are unchanged.
- No hardware action, flashing, deployment, T10 work or default-branch merge.
