# Pico W bridge: portable host core

Status: **independently authored portable core and host tests implemented**.
No physical bus communication, SDK/hardware target, network server or control
execution exists in this milestone. All candidate inbound behavior is synthetic.

Target: Gecko IN.YE-3-H3.0 / YE-3-CE, V2; replace the Nano and ESP32 with one
Raspberry Pi Pico W (RP2040), serving an independently developed Pi Zero W app.

- [Implementation plan, architecture, and prioritized backlog](docs/IMPLEMENTATION_PLAN.md)
- [Proposed HTTP JSON API v1](docs/HTTP_API_V1.md)
- [Repository review and verification evidence](docs/REPOSITORY_REVIEW.md)
- [Offline source characterization](review/run_characterization.py)
- [Current roadmap](docs/ROADMAP.md)
- [Protocol facts and assumptions](docs/PROTOCOL_EVIDENCE.md)
- [Licensing and no-reuse decision](docs/LICENSING.md)
- [Current validation and committed test evidence](docs/VALIDATION.md)

The original `arduino/`, `components/`, `esphome/`, tools, examples/assets and Pi
Zero application remain unchanged. Agile Alert is outside this repository's work.

Build and run the new host acceptance suite from the repository root (CMake
3.20+, a C++17 compiler, Python 3.8+, and host sanitizer support):

```sh
cmake -S pico_w -B work/pico-core -DGECKO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build work/pico-core
ctest --test-dir work/pico-core --output-on-failure
```

Core entry points are in `include/gecko/protocol.hpp` and `assembly.hpp`. Inputs
are explicit byte views; results carry errors and owned bounded values. The caller
must inspect a Result before using its value. Views must describe actual live
memory of the stated size; a C++ byte view cannot authenticate dangling pointers.
Malformed input is rejected without physical side effects or partially published
state. Candidate XML/clock/status/reassembly require explicit
`DecodeMode::SyntheticCandidate`; default decoding fails closed.

The historical source-extraction probe is preserved as an initial-review artifact;
it is not part of the new core or acceptance suite and should not be run as new
core validation. Never distribute its generated upstream-source files.

The new scoped MIT license does not relicense inherited source. Upstream source
permissions remain unresolved, so no upstream implementation code is reused.
