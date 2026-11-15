# Roadmap after the portable-core milestone

Updated 2026-11-15. The user's follow-up authorizes host implementation after the
initial review, while keeping all physical bus work/deployment and Agile Alert
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

## Original task mapping

| Plan task | Status | Remaining acceptance work |
|---|---|---|
| T00 Review | Host-core follow-up authorized | Future scope changes remain reviewed. |
| T01 Licensing | No-reuse path resolved for new implementation | Original source/asset grant and Geckolib provenance remain unresolved; do not copy or relicense those files. |
| T02 Build/CI | Host build/tests delivered | SDK/toolchain pinning, Pico target and hosted CI workflow are deferred. No SDK is downloaded by this build. |
| T03 Corpus | Complete documented/derived command fixtures + synthetic helpers delivered | Full recorded inbound corpus and capture tooling remain unavailable. |
| T04 Safe codec | Delivered within documented command/candidate-frame scope | Promote inbound integrity rules only with evidence; no generic/pump raw-control interface. |
| T05 Status/reassembly | Bounded synthetic implementation delivered | v50, combined tails, other lengths, real profile whitelist and live freshness/state store remain blocked/deferred. |
| T06 Simulated transport | Not started | Deterministic transport event model, TX expectations and phase failures; current synthetic assembler tests are not a transport simulator. |
| T07 Session/policy | Not started | Handshake/deadline/backoff state machine and independent read-only policy; current frame decoding is not a session implementation. |
| T08 Host HTTP | Proposal only | Schema freeze, host adapter/mock and contract tests; Pi application stays separate. |
| T09–T14 Hardware/network | Not authorized now | Pico/Wi-Fi/physical I²C, electrical review, bench/capture/read-only pack gates. |
| T15–T17 Control/release | Deferred | Command lifecycle, supervised validation, release permissions and reviewed merge. |

## Next development slice

1. Review the core's public byte/result interfaces and evidence labels. Do not
   promote candidate parsers merely because host tests pass.
2. Build T06 host-only scripted transport against these types, including expected
   outbound bytes, address/data NACK, short read, arbitration/restore failure, loss
   and injected monotonic time. No SDK, GPIO or real network transport.
3. Add T07 session/read-only policy tests using synthetic scenarios, with every
   unverified wire assumption in PROTOCOL_EVIDENCE.md. Then implement T08 HTTP mock.
4. Obtain permission/capture evidence independently before considering code reuse
   or physical transport. No schedule can eliminate those external dependencies.

See [VALIDATION.md](VALIDATION.md) for actual commands/results and
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for the original architecture/gates.
