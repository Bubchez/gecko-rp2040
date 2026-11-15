# Roadmap after T09 offline firmware

Updated 2026-11-15. The user's follow-up authorizes T09 firmware implementation,
compilation and offline validation only, following T08. Gecko GPIO stays inactive;
physical I2C transport and actuator execution are excluded. Flashing, deployment,
T10, Agile Alert/Pi Zero changes and later hardware/control gates remain out of scope.

## Delivered

- Separate commit `0179c72`: initial architecture, API proposal and baseline review.
- Separate commit `7ae07f4`: persistent repository, concise-output, licensing,
  host-only safety and validation instructions in root AGENTS.md.
- Independent C++17 static library with no AVR, ESPHome, SDK or network dependency.
- Bounded views/endian helpers/copy, strict compact-hex and legacy-record importer,
  typed frame/error results, exact documented literals, XOR-checked command families.
- Offline temperature/light/circulation/program encoders and pure pump bit-packing.
- Synthetic-only XML/clock/notification decoding, Gregorian reminders, explicit
  candidate-v51 status parser with independent optional temperatures.
- Bounded synthetic reassembly, injected time/session/loss/order metadata, expiry
  and whole-message invalidation; no invented on-wire sequence counter.
- Host CMake/CTest, strict compiler warnings, ASan/UBSan, optimized tests, complete
  attributed fixtures and deterministic malformed/mutation coverage.
- Scoped new-code MIT notice and no-reuse decision. Upstream grant remains missing;
  upstream implementation copying is not authorized by this milestone.
- T06 deterministic transport: exact byte/time expectations; write/read/STOP/restore
  phases, scripted outcomes and RX; bounded event/trace queues with loss reporting.
- T07 synthetic session: ACK-correlated handshake, explicit profile/order selection,
  deadlines/duplicates, fresh snapshots, periodic renewal, bounded seeded backoff,
  reconnection, old-epoch rejection and terminal participation shutdown.
- Independent read-only gates in session and transport; physical communication is
  permanently absent. Acquisition-time, interrupted-assembly and fault-state
  regressions are fixed and tested; see [SIMULATION.md](SIMULATION.md).
- T08 finalized read-only API v1/OpenAPI 3.1, native-session-backed loopback HTTP
  mock, independent maintenance/cache, strict authentication/JSON/framing, explicit
  null/unavailable/freshness semantics and rejection of every actuator operation.
- Windows-compatible single-command launcher and local secret provisioning,
  compiler/executable path handling, integration handoff for Agile Alert, and
  complete regression/HTTP schema/contract/startup tests. Native Windows execution
  was not available; portable startup handling and a full Linux build/run were tested.
- T09 shared no-exception synthetic model, bounded native v1 parser/serializer,
  independent maintenance and stale-cache recovery, raw lwIP adapter, USB credential
  records and Wi-Fi reconnection/diagnostics implementation. Native code compiles
  for RP2040/Pico W; all observations remain synthetic.
- Immutable Pico SDK/submodule and compiler SHA256 pins, reproducible UF2 tooling,
  static flash/RAM/heap/stack and forbidden-symbol gates, dependency licence notices,
  additional host contract/fault/record/callback tests and Windows bench handoff.
  USB/Wi-Fi/flash/GPIO/ARM runtime tests are explicitly unverified, not waived.

## Original task mapping

| Plan task | Status | Remaining acceptance work |
|---|---|---|
| T00 Review | Host-core follow-up authorized | Future scope changes remain reviewed. |
| T01 Licensing | No-reuse path resolved for new implementation | Original source/asset grant and Geckolib provenance remain unresolved; do not copy or relicense those files. |
| T02 Build/CI | Host tests plus pinned Pico cross-build delivered | Host build stays SDK-independent; separate firmware tool downloads verified SDK/compiler inputs. Hosted CI is deferred. |
| T03 Corpus | Complete documented/derived command fixtures + synthetic helpers delivered | Full recorded inbound corpus and capture tooling remain unavailable. |
| T04 Safe codec | Delivered within documented command/candidate-frame scope | Promote inbound integrity rules only with evidence; no generic/pump raw-control interface. |
| T05 Status/reassembly | Synthetic implementation + acquisition metadata delivered | v50, combined tails, other lengths and real profile validation remain blocked/deferred. |
| T06 Simulated transport | Complete for host scope | Scripted transport, phase faults, exact TX matching, bounded queues, deterministic time/trace and recovery tests delivered. Real bus fidelity remains unverified. |
| T07 Session/policy | Complete for synthetic read-only scope | Handshake, profile/session invalidation, snapshot freshness, reconnection/backoff, passive/default modes and defense-in-depth read-only tests delivered. No physical readiness claim. |
| T08 Host HTTP | Complete for host read-only scope | Frozen v1/OpenAPI, runnable mock using existing core, authentication/freshness/null/fault/denial tests, Windows procedure and Agile Alert handoff delivered. No application changes. |
| T09 Pico simulation | Implementation, compilation and offline validation complete | Reproducible UF2; shared session/native v1 compatibility; Wi-Fi/USB/configuration code; bounded resources. Real-device tests unverified. Stop before flashing/deployment. |
| T10–T14 Physical transport/hardware | Not authorized | Physical I2C, electrical review, bench/capture/read-only pack gates remain separate; no hardware adapter is compiled or implemented. |
| T15–T17 Control/release | Deferred | Command lifecycle, supervised validation, release permissions and reviewed merge. |

## Next development slice

1. Review T09 source, UF2/manifest, native contract comparisons, memory evidence and
   [integration findings](T09_REVIEW.md). The handoff changes no external application.
2. **Stop before flashing, deployment or T10.** A later explicit task must authorize
   isolated-device USB/Wi-Fi/flash/GPIO/resource validation. No Gecko wiring/control
   is authorized by this firmware delivery or its example procedure.
3. Keep protocol/electrical/recording gates separate. Missing Gecko hardware and
   upstream source permissions remain external blockers for physical transport or
   legacy code reuse; they do not block this independent simulation build.

See [VALIDATION.md](VALIDATION.md) for actual commands/results and
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for the original architecture/gates.
