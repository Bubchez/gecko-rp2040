# Portable core validation

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
