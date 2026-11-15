# Pico W bridge: portable host core

Status: **T08 complete: read-only API v1, OpenAPI schema and runnable host mock
backed by the portable session core**. Local loopback HTTP is implemented. No
physical bus communication, SDK/Pico target/deployment or actuator execution exists.
All model data remains synthetic. Work stops before T09.

Target: Gecko IN.YE-3-H3.0 / YE-3-CE, V2; replace the Nano and ESP32 with one
Raspberry Pi Pico W (RP2040), serving an independently developed Pi Zero W app.

- [Implementation plan, architecture, and prioritized backlog](docs/IMPLEMENTATION_PLAN.md)
- [Final read-only HTTP JSON API v1](docs/HTTP_API_V1.md)
- [OpenAPI 3.1 contract](api/openapi-v1.json)
- [Windows-compatible startup and scenarios](docs/HOST_MOCK.md)
- [Agile Alert integration handoff](docs/AGILE_ALERT_HANDOFF.md)
- [Repository review and verification evidence](docs/REPOSITORY_REVIEW.md)
- [Offline source characterization](review/run_characterization.py)
- [Current roadmap](docs/ROADMAP.md)
- [Protocol facts and assumptions](docs/PROTOCOL_EVIDENCE.md)
- [Licensing and no-reuse decision](docs/LICENSING.md)
- [Current validation and committed test evidence](docs/VALIDATION.md)
- [Transport/session interfaces, policies and regression findings](docs/SIMULATION.md)

The original `arduino/`, `components/`, `esphome/`, tools, examples/assets and Pi
Zero application remain unchanged. Agile Alert is outside this repository's work.

Start the local mock from the repository root with `python3 pico_w/tools/start_mock.py`
(Windows: `py -3 .\pico_w\tools\start_mock.py`). Requires CMake 3.20+, a C++17
compiler and Python 3.10+; runtime uses no pip packages. See HOST_MOCK.md for token
handling, PowerShell requests and compiler prerequisites.

Build and run all host acceptance tests using a local venv with the pinned test
validators installed (use `Scripts/python.exe` on Windows):

```sh
python3 -m venv work/http-tests-env
work/http-tests-env/bin/python -m pip install -r pico_w/tests/requirements.txt
cmake -S pico_w -B work/pico-core -DGECKO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug -DPython3_EXECUTABLE="$PWD/work/http-tests-env/bin/python"
cmake --build work/pico-core
ctest --test-dir work/pico-core --output-on-failure
```

Core entry points are in `include/gecko/protocol.hpp` and `assembly.hpp`; synthetic
transport/session interfaces are in `transport.hpp` and `session.hpp`. Inputs
are explicit byte views; results carry errors and owned bounded values. The caller
must inspect a Result before using its value. Views must describe actual live
memory of the stated size; a C++ byte view cannot authenticate dangling pointers.
Malformed input is rejected without physical side effects or partially published
state. Candidate XML/clock/status/reassembly require explicit
`DecodeMode::SyntheticCandidate`; default decoding fails closed.

`ReadOnlySession` defaults to inactive. Synthetic participation also requires an
explicit candidate C82/S81 profile selection; that is a fixture binding, not a
verified pack profile. Session and transport both deny actuator requests. Only
exact GO/handshake ACK packets can enter the simulated transaction engine.
HTTP requests read an immutable cache and never advance the core. Maintenance is
independent; all actuator objects receive 403. No accepted-command queue exists.

The historical source-extraction probe is preserved as an initial-review artifact;
it is not part of the new core or acceptance suite and should not be run as new
core validation. Never distribute its generated upstream-source files.

The new scoped MIT license does not relicense inherited source. Upstream source
permissions remain unresolved, so no upstream implementation code is reused.
