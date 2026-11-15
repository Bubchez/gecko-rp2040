# Current T08 validation

Run date: 2026-11-15. Branch `feature/pico-w-bridge`, base commit
`33fdede7ce23363d3294c030dd3b6bfca1ef8868`. T08 is complete for the local,
read-only host scope. Stop before T09. No physical communication, Pico target,
hardware deployment or Agile Alert/Pi Zero application modifications occurred.

| Check | Actual result | Evidence |
|---|---|---|
| Clean Debug build with ASan/UBSan and strict warnings | 35/35 CTest groups pass; original core/scenario regressions included | [T08_DEBUG.txt](validation/T08_DEBUG.txt) |
| Clean optimized Release build | 35/35 CTest groups pass; test checks remain active under NDEBUG | [T08_RELEASE.txt](validation/T08_RELEASE.txt) |
| OpenAPI 3.1, response JSON Schema and examples | Schema validates; actual responses across all five fixtures and seven committed synthetic examples validate | http.schema in both transcripts |
| HTTP/IPC/startup acceptance | 23 Python test methods across five HTTP groups, including real loopback requests and native core process | Verbose test names/results in both transcripts |
| Source-build launcher smoke on Linux | Launcher builds Release without test dependencies, serves authenticated fresh synthetic state, logs no token value and exits cleanly on SIGINT | [T08_STARTUP.txt](validation/T08_STARTUP.txt) |
| Windows-compatible launcher | Portable command/path handling and multi-config .exe discovery tested; **native Windows execution not available** | http.startup and HOST_MOCK.md |
| Preservation/scope | Original Arduino/ESPHome/tools/examples/assets remain unchanged; all new work is under pico_w plus instruction updates | Final diff audit against original baseline |

Toolchain: GCC 14.2.0, CMake/CTest 3.31.6, Python 3.12.14. Development validators:
jsonschema 4.23.0 and openapi-spec-validator 0.7.2. Runtime uses only Python/C++
standard libraries. Exact tool metadata, build arguments, per-case output and exit
statuses are committed in the new transcripts (trailing whitespace trimmed only). These are local host results,
not a claim of GitHub Actions, Windows execution, Pico networking or hardware tests.
The original 60,000 deterministic malformed/mutation cases remain included.

## Reproduce all tests

Install the test-only validators into a local venv; configure CMake with its Python.
The current workspace uses work/review/venv, as shown in the actual transcripts.
A fresh Linux/macOS setup can use:

```sh
python3 -m venv work/http-tests-env
work/http-tests-env/bin/python -m pip install -r pico_w/tests/requirements.txt
cmake -S pico_w -B work/pico-core -DGECKO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug -DPython3_EXECUTABLE="$PWD/work/http-tests-env/bin/python"
cmake --build work/pico-core --clean-first -j 2
ctest --test-dir work/pico-core -V --output-on-failure
cmake -S pico_w -B work/pico-core-release -DGECKO_SANITIZERS=OFF -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$PWD/work/http-tests-env/bin/python"
cmake --build work/pico-core-release --clean-first -j 2
ctest --test-dir work/pico-core-release -V --output-on-failure
```

Windows startup and Release test commands are in [HOST_MOCK.md](HOST_MOCK.md).
MSVC sanitizer support is not claimed by this configuration; validated sanitizer
runs use GCC on Linux. Native subprocesses exit through EOF in the test harness,
allowing normal sanitizer termination. The launcher smoke uses a normal Release
build, not a Pico cross-compilation.

## Integration boundaries reviewed and fixed

- Finite T06 scripts/trace cannot support an indefinitely running host by implicit
  reset. An explicit retire_completed_script method accepts only completed/drained
  scenarios. It preserves clock, role, epoch, revision and counters and acknowledges
  retired trace counts. A two-minute native-model test exceeds the original lifetime
  capacities without resetting session state or hitting an unexpected transaction.
- HTTP handlers never drive/tick the core. A maintenance owner advances the native
  model; GETs read a bounded immutable cache. Tests compare complete native snapshots
  before/after HTTP GETs and denied commands. Request rate cannot alter the protocol.
- Oldest-fragment freshness is preserved in milliseconds, with completion age
  separate. Expired cache/dead process gives 503, not frozen ready data; modeled
  session fault/staleness gives status 200 with stale/ready false and retained state.
- Unsupported fields are null with explicit unavailable paths; equipment presence
  is unknown, not inferred from four candidate pump channels. Invalid actual
  temperature can be null while target stays valid. Every output remains synthetic.
- Authentication precedes protected data; malformed/ambiguous framing is rejected.
  No CORS, non-loopback binding, command acceptance or network/physical adapter exists.
- Two boundary regressions were reproduced before fixes: embedded NUL caused the
  bounded native stdin parser to accept a truncated command; and the standard HTTP
  parser normalized //api/v1/status into a valid route. Native parsing now uses the
  extracted byte count and rejects NUL; raw double-slash targets reject before
  standard parsing. Final suites cover both. Expect/100-continue is also rejected
  before the library can send an unintended interim response.

## HTTP acceptance coverage

Authentication/missing/wrong/duplicate headers; constant-time token comparison;
minimal public health; no token logging; no CORS; route/method denials; all nine
representative actuator/unknown intent families forbidden without a core change;
read-only rejection even when backend unavailable; no command acceptance/history;
boot unknowns and later observations; source/identity/revision semantics; null and
invalid fields; retained stale data; core fault recovery without boot change;
rate limits and Retry-After; bounded URI/header/body sizes; duplicate lengths,
Transfer-Encoding/Expect, Host, body/media rules; strict UTF-8/JSON including duplicate
keys, NaN/Infinity/exponent overflow/surrogates; absolute receive deadline and close;
backend loss/cache expiry and validation; native IPC malformed input; long running
model; source/fixture/schema agreement; multi-config executable discovery, secret
file reuse, live launcher HTTP and graceful source-build smoke shutdown.

No new upstream implementation is reused. The scoped MIT notice now covers the
new host tools and API definitions; validators remain external development packages.
See [LICENSING.md](LICENSING.md), [HTTP_API_V1.md](HTTP_API_V1.md),
[HOST_MOCK.md](HOST_MOCK.md) and [AGILE_ALERT_HANDOFF.md](AGILE_ALERT_HANDOFF.md).

Remaining limitations: real protocol/electrical/Pico behavior remains unverified;
no native Windows run was possible; suspend/catch-up longer than 60 s fails the
mock backend closed and needs restart. The Agile Alert handoff is a specification,
not application implementation work. Next action: review the handoff and contract.
**Stop before T09.**

---

# Historical T06–T07 validation (33fdede)

Run date: 2026-11-15. Branch `feature/pico-w-bridge`; implementation base
`d7d300beb9e368b66c69a76ce01009b317c66ad5`. T06–T07 are completed for the host,
synthetic, read-only scope. T08 has not started. No physical communication,
hardware deployment, upstream extraction, SDK/network imports or Agile Alert
changes occurred. The original implementation remains unchanged.

| Check | Result | Committed evidence |
|---|---|---|
| Debug, ASan/UBSan, strict warnings, clean rebuild | **30/30 CTest groups passed**, no sanitizer diagnostics | [T06–T07 Debug transcript](validation/T06_T07_DEBUG.txt) |
| Optimized Release, strict warnings, clean rebuild | **30/30 CTest groups passed**; checks remain active under NDEBUG | [T06–T07 Release transcript](validation/T06_T07_RELEASE.txt) |
| Original 16 core groups | Still pass, including 60,000 deterministic malformed/mutated inputs and 15 attributed command fixtures | Included in both new transcripts |
| New 14 scenario groups | Pass; 32 handshake-write/outcome failure cases and eight complete recovery cases are named in the logs | `scenario.*` output in both new transcripts |
| Regression reproduction | Three initial integration regressions failed before fixes; a separate delayed-activation regression also failed before its fix. All pass in final suites | Findings and remedies in [SIMULATION.md](SIMULATION.md) |

Tool versions remain GCC 14.2.0, CMake/CTest 3.31.6 and Python 3.12.14. The existing
C++17 strict warning and sanitizer settings below apply to the new modules/tests.
The exact clean-build and verbose CTest commands, phase/outcome case names and exit
statuses are in the transcripts. These are locally executed host results committed
to GitHub, not GitHub Actions or hardware validation claims.

Reproduce with the existing root instructions:

```sh
cmake -S pico_w -B work/pico-core -DGECKO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build work/pico-core --clean-first -j 2
ctest --test-dir work/pico-core -V --output-on-failure
cmake -S pico_w -B work/pico-core-release -DGECKO_SANITIZERS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build work/pico-core-release --clean-first -j 2
ctest --test-dir work/pico-core-release -V --output-on-failure
```

## Added acceptance coverage

- Exact expected TX bytes and time windows, correlated completion, write/read/STOP/
  restore roles, read length, explicit NACK/arbitration/short-read/timeouts/stuck
  lines/restore failure, all four arbitration and deadline phases. An unknown
  partial write outcome remains possibly applied rather than definitely unsent.
- Two-layer read-only enforcement; no control request can enqueue a transaction.
  Passive/default modes do not respond or become ready. Only explicit synthetic
  C82/S81 candidate selection can start the host handshake.
- GO/ACK completion gating, file order and unknown-version rejection, bounded
  duplicates, progress deadlines that duplicates cannot extend, missing clock/LO/
  status, no overlapping GO, and periodic epoch renewal.
- Freshness based on the oldest fragment; completion time separately reported.
  Snapshot queries, echoes/noise/commands/malformed frames, old sessions and partial
  or already-stale assemblies cannot refresh current state.
- Expiry, interleaving, bad synthetic index, bus collision and event overflow reset
  partial state/profile readiness. Prior observations remain stale on disconnect;
  only a full new handshake plus fresh status restores readiness.
- Capped seeded jitter/backoff and retry exhaustion; complete recovery for each
  outcome; explicit local restart after terminal restoration failure. Terminal
  state deactivates participation, including implicit simulated read replies.
- Fixed-seed role-trace reproducibility, bounded script/event/trace capacities,
  complete drop accounting, uint64 boundaries and clock-regression rejection;
  delayed activation cannot retroactively enable listening.

No new raw-parser memory defect was reproduced. The integration blockers, their
failed-before/fixed-after regression observations and interface hardening are
recorded in SIMULATION.md. Original historical 16-group records follow unchanged
and remain available in the old HOST_DEBUG/HOST_RELEASE transcripts; their limits
refer to the earlier core-only milestone, not the current T06–T07 implementation.

All evidence classifications in [PROTOCOL_EVIDENCE.md](PROTOCOL_EVIDENCE.md) remain
in force. No wire timing/integrity, true pack compatibility or physical readiness
has been established. Upstream implementation reuse is still prohibited by the
unresolved grant; the new code is independently authored under the scoped license.
Next action: review these results and interfaces. **Stop before T08.**

---

# Historical portable core validation (d7d300b)

Run date: 2026-11-15. Repository `Bubchez/gecko-rp2040`, branch
`feature/pico-w-bridge`. Original implementation baseline:
`3baacda4c0e3ae58e8b014692cf5f146db2b3318`. Initial review was committed
separately as `0179c72`; persistent instructions as `7ae07f4`. This document and
the transcripts belong to the subsequent portable-core implementation commit.

## Results

| Build/check | Actual result | Evidence |
|---|---|---|
| Debug + AddressSanitizer/UndefinedBehaviorSanitizer | 16/16 CTest groups passed; no sanitizer diagnostics; warnings treated as errors | [Full Debug transcript](validation/HOST_DEBUG.txt) |
| Optimized Release, sanitizers OFF | 16/16 CTest groups passed; checks remain active with NDEBUG | [Full Release transcript](validation/HOST_RELEASE.txt) |
| Library-only Release (`BUILD_TESTING=OFF`) | Static library built without locating Python or building tests | Command below; no firmware/SDK target exists. |
| Complete fixture validation | 15 attributed examples validated and parsed by compiled core; malformed/mislabeled fixture rejection passed | `core.fixtures` in both transcripts |
| Original-tree preservation | `git diff --exit-code` against baseline for protected paths returned 0 | Preservation command below |

Toolchain: GNU C++ 14.2.0 on Linux, CMake/CTest 3.31.6, Python 3.12.14. New core
uses C++17. No external test framework, copied upstream functions, AVR/ESPHome
runtime or Pico SDK was linked. CMake was installed into the existing local review
venv under `work/`; install CMake separately or put it on PATH on a fresh machine.
These logs are **local host evidence committed to GitHub**, not a claim that a
GitHub Actions workflow ran. Hosted CI and Pico builds remain roadmap items.

Compiler options shared by library and tests:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
-std=c++17
```

Debug sanitizer options additionally include `-fsanitize=address,undefined`,
`-fno-sanitize-recover=all` and `-fno-omit-frame-pointer`; Linux uses `-fno-pie` /
`-no-pie` to avoid ASan shadow mapping collisions. Sanitizer errors terminate the
test instead of allowing an apparently successful exit. Release uses the CMake
Release optimization settings; tests use explicit checks/exceptions rather than
assert macros that would disappear under NDEBUG.

## Reproduction

From the repository root with CMake, a GCC/Clang C++17 compiler and Python present:

```sh
cmake -S pico_w -B work/pico-core -DGECKO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build work/pico-core --clean-first -j 2
ctest --test-dir work/pico-core -V --output-on-failure

cmake -S pico_w -B work/pico-core-release -DGECKO_SANITIZERS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build work/pico-core-release --clean-first -j 2
ctest --test-dir work/pico-core-release -V --output-on-failure

cmake -S pico_w -B work/pico-core-library -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build work/pico-core-library -j 2

git diff --exit-code 3baacda4c0e3ae58e8b014692cf5f146db2b3318 -- \
  arduino components esphome tools README.md example.yaml pictures docs \
  logo.png logo_small.png .gitignore
```

The committed transcripts show the actual CMake/CTest executable paths used in
this workspace. Do not run `pico_w/review/run_characterization.py` as an acceptance
test: that historical script extracts upstream functions, characterizes defects
in the baseline and is excluded from this implementation's build/tests.

## Coverage and assertions

The suite contains 15 C++ groups and one Python fixture validation group. It covers:

- Null/nonzero views, empty buffers, endian reads, SIZE_MAX offsets/counts,
  exact/undersized output capacity, unchanged destination on failure and overlap.
- Hex digits across all 256 input byte values, uppercase/lowercase, embedded NUL,
  odd lengths, incomplete examples, whitespace and 128/129-byte boundaries.
- RX recording import at all 1–128 lengths, negative/overflow/missing/incorrect
  declared lengths, trailing bytes/newline and invalid hex. Both historical
  out-of-bounds-read shapes now reject; no source-extraction probe is involved.
- Golden temperature packets for 26/36.5/37/40 °C; all 29 half-degree request
  steps; exact big-endian raw values and XOR; NaN/Inf/huge/out-of-range/invalid
  increment and zero-version rejection. No floating conversion occurs before
  finite/range/quantization checks.
- Light/circulation/program exact bytes and invalid states/enums; exhaustive
  256 previous-byte × 4 pump-field × 3 demand-value mask checks. This proves
  packing, not supported pump speeds or safe shared-demand command execution.
- Every truncation and every single-bit mutation of representative documented
  command/literal packets, trailing data, wrong routing/header, corrected-XOR
  invalid values, and an XOR-zero malformed GO envelope.
- Explicit candidate-only filename/clock/notification handling; bounded filename
  termination/version checks; checksum-checked clock with invalid hour/min/sec,
  leap dates, partial/all FF date, invalid/duplicate reminder IDs and little-endian
  intervals through 65,535 days. Large-date reference results were cross-checked
  with Python's standard `datetime.date + timedelta`: 2026-11-08 + 65535 =
  2206-04-14; 2000-02-28 + 65535 = 2179-08-03.
- Candidate status requires explicit profile + synthetic opt-in + 162 bytes.
  v50/unknown profiles, wrong length/marker/light enum reject. Independent invalid
  temperatures yield absent Celsius fields, not fabricated values. Output states
  and user-demand bitfields remain distinct; hardware_verified stays false.
- Synthetic 78/78/54-byte fragmentation; full assemblies of 120/162/170/390/405/512
  bytes and rejection at 513; 512 one-byte fragments; missing/duplicate/reordered
  synthetic indices; timeout exactly at deadline, total deadline, clock regression,
  near-uint64 maximum, session/loss-generation change, malformed/nonfragment input,
  drop-whole-message and recovery starting only from an explicit new index zero.
- 50,000 deterministic arbitrary malformed raw frames (sizes 0–129) plus 10,000
  structured candidate-status byte mutations. The seed is fixed in test source.
  The raw corpus exercises parser/assembler rejection without stale buffered data;
  the status corpus reaches field validation and never upgrades hardware confidence.
- Fixture schema metadata, complete bytes, matching lengths, provenance, checksum
  policy, typed frame acceptance and rejection of ellipses or hardware-confidence
  claims. Fixtures are documentation/synthetic examples, not recorded transport.

Mutation counts are input counts, not independent proof of correctness or an
exhaustive fuzz campaign. Group/check counts and every test process exit status
are recorded in the transcripts. CTest has a 30 s per-group deadline.

## Development finding corrected during validation

The first test run passed 14/15 groups and failed one **test assertion** that
assumed `xor_bytes(GO) != 0`. Calculating the documented GO literal showed that it
does XOR to zero. The test was corrected to the known numeric fact and augmented
with an XOR-zero malformed-header counterexample. The decoder continues to match
the fixed literal exactly; no general inbound checksum rule was inferred from
this observation. Final Debug/Release transcripts are for the corrected suite.

## Limits and open blockers

- No physical bus communication, Pico flashing/deployment, Wi-Fi, HTTP server,
  transport simulator or handshake/session execution was performed or implemented.
  No Pi Zero application or Agile Alert changes were made.
- No hardware captures exist. Inbound routing, multipart trailers/order, status
  layout/temperature plausibility, timeout choices and electrical timing remain
  unverified. Candidate mode cannot be used as permission for physical work.
- The v50/combined-status parsers and actual pack capability discovery remain
  deferred rather than guessed. A known packet's XOR is weak error detection,
  not authentication or proof that its payload is a real observation.
- Independently authored source avoids the unresolved upstream license grant;
  it does not resolve redistribution rights for inherited source/assets. See
  [LICENSING.md](LICENSING.md).
- Host builds do not establish RP2040 SRAM/stack use, ISR latency, SDK integration,
  arbitration, safe role switching or compatibility with the target YE-3 V2.
  The standard-library/exceptions configuration of a future Pico target needs
  a separate build review; current tests are explicitly a host target.

Next action: review the core/evidence boundaries, then build the deterministic
host transport simulator and read-only session tests (T06/T07) before the HTTP mock.
