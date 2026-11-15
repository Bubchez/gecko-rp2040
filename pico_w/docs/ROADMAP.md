# Roadmap after T06–T07

Updated 2026-11-15. The user's follow-up authorizes host implementation after the
initial review and then T06–T07, while keeping all physical bus work/deployment and Agile Alert
out of scope. This status supersedes the initial plan's blanket implementation
pause; it does not approve later hardware/control gates.

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

## Original task mapping

| Plan task | Status | Remaining acceptance work |
|---|---|---|
| T00 Review | Host-core follow-up authorized | Future scope changes remain reviewed. |
| T01 Licensing | No-reuse path resolved for new implementation | Original source/asset grant and Geckolib provenance remain unresolved; do not copy or relicense those files. |
| T02 Build/CI | Host build/tests delivered | SDK/toolchain pinning, Pico target and hosted CI workflow are deferred. No SDK is downloaded by this build. |
| T03 Corpus | Complete documented/derived command fixtures + synthetic helpers delivered | Full recorded inbound corpus and capture tooling remain unavailable. |
| T04 Safe codec | Delivered within documented command/candidate-frame scope | Promote inbound integrity rules only with evidence; no generic/pump raw-control interface. |
| T05 Status/reassembly | Synthetic implementation + acquisition metadata delivered | v50, combined tails, other lengths and real profile validation remain blocked/deferred. |
| T06 Simulated transport | Complete for host scope | Scripted transport, phase faults, exact TX matching, bounded queues, deterministic time/trace and recovery tests delivered. Real bus fidelity remains unverified. |
| T07 Session/policy | Complete for synthetic read-only scope | Handshake, profile/session invalidation, snapshot freshness, reconnection/backoff, passive/default modes and defense-in-depth read-only tests delivered. No physical readiness claim. |
| T08 Host HTTP | Not started; stop boundary | Proposal remains unchanged. Schema freeze, host adapter/mock and contract tests require a subsequent task; Pi application stays separate. |
| T09–T14 Hardware/network | Not authorized now | Pico/Wi-Fi/physical I²C, electrical review, bench/capture/read-only pack gates. |
| T15–T17 Control/release | Deferred | Command lifecycle, supervised validation, release permissions and reviewed merge. |

## Next development slice

1. Review the T06–T07 interfaces, findings, acquisition-age semantics and scenario
   evidence. Do not promote candidate behavior merely because host tests pass.
2. Stop before T08 as requested. A later task can agree the HTTP schema and build
   the host mock using these read-only session/snapshot interfaces; no HTTP or
   network implementation is included now.
3. Obtain permission/capture evidence independently before considering code reuse
   or physical transport. No schedule can eliminate those external dependencies.

See [VALIDATION.md](VALIDATION.md) for actual commands/results and
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for the original architecture/gates.
