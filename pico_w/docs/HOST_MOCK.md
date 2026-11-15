# Host mock startup and operation (T08)

This runs the independently authored C++ session core behind a local Python HTTP
adapter. It never loads an SDK, I²C/serial/GPIO adapter or physical transport. It
does not modify Agile Alert. Stop before T09: no Pico or Wi-Fi build/deployment.

## Windows quick start

Prerequisites: Python **3.10+** (with the `py` launcher), CMake **3.20+** on PATH,
and a C++17 compiler. Visual Studio 2022 Build Tools with **Desktop development with
C++** and a Windows SDK is a suitable compiler installation. Runtime needs no pip
packages; the Python HTTP adapter uses only the standard library.

From the repository root on `feature/pico-w-bridge`, in PowerShell:

```powershell
py -3 .\pico_w\tools\start_mock.py
```

The launcher builds a native Release executable into `work/gecko-http/build`,
finds either a single-configuration executable or `Release/gecko_mock_core.exe`,
creates/reuses `work/gecko-http/token.txt`, then serves **127.0.0.1:8765**. It prints
the URL and token file path, not the token value. It does not overwrite an existing
secret. `work/.gitignore` is created only when absent so local build/token files
stay out of source control. Treat the token file as a local secret; do not copy it
into Agile Alert source or commit it. No global environment or execution policy
change is needed.

In a second PowerShell terminal at the same repository root:

```powershell
$token = (Get-Content -Raw .\work\gecko-http\token.txt).Trim()
$headers = @{ Authorization = "Bearer $token" }
Invoke-RestMethod http://127.0.0.1:8765/api/v1/health
Invoke-RestMethod http://127.0.0.1:8765/api/v1/capabilities -Headers $headers
Invoke-RestMethod http://127.0.0.1:8765/api/v1/status -Headers $headers
```

Stop with **Ctrl+C**. To change the fixture, stop and restart:

```powershell
py -3 .\pico_w\tools\start_mock.py --scenario recovery
```

Each restart creates a new API boot ID. The token file and configured default
device ID remain stable. There is no HTTP scenario/arming/reset endpoint.

## Other hosts and existing builds

With Python/CMake/compiler on PATH:

```sh
python3 pico_w/tools/start_mock.py
```

Options include `--scenario`, `--port`, `--cmake`, `--core` and `--token-file`.
`--core` skips rebuilding and accepts an already built `gecko_mock_core` executable.
The launcher uses argument lists, not shell command concatenation, including paths
with spaces. The server itself can also be invoked directly:

```sh
python3 pico_w/src/host/http_mock.py --core work/pico-core-release/gecko_mock_core --token-file work/gecko-http/token.txt
```

Direct server startup requires an existing token file or GECKO_MOCK_TOKEN with a
32–128 character URL-safe token. Tokens are never command-line arguments and are
removed from the native child's environment. A non-default device ID can be set
with direct `--device-id`. The mock rejects non-loopback binding; browser CORS,
LAN hosting, TLS, discovery and physical communication are not part of this tool.

## Scenarios and clocks

| CLI scenario | Behavior from the real C++ host session core |
|---|---|
| healthy (default) | Synthetic C82/S81 handshake, whole-status updates roughly every 0.5 s, periodic GO renewal. |
| unavailable | Inactive session: revision 0, unknown provenance, null observations and no simulated TX. |
| stale | One whole status, no further status; becomes stale after 2 s and exhausts bounded retries into a fault. Old values/revision remain visibly stale. |
| recovery | A periodic GO gets address NACK at about 1 s; profile/state become unready, then full new handshake/status recovers without changing boot ID. |
| invalid_temperature | Actual temperature is an invalid raw value and remains null; target temperature is 37 °C. It is listed as invalid, not converted to zero. |

These are explicit synthetic fixtures, not recordings or measured pack behavior.
The 2 s freshness threshold and fixture timing are host policy. Clock/reminder and
other unimplemented snapshot fields remain unavailable rather than populated from
documentation. A worker advances the native model independently of HTTP polling;
GETs only read a cache. Ages include elapsed cache time conservatively.

The native simulator retains bounded queues/traces. The host driver explicitly
retires only completed scripts, acknowledging trace retirement through counters;
it never resets session IDs, revisions, clock or policy to keep the mock running.
The 120-second acceptance test crosses the old finite-script/trace limits. A single
catch-up is capped at 60 seconds to bound work. If the host sleeps/suspends longer
or the backend dies, protected reads return 503 instead of frozen ready data.
Restart the mock after that failure; health may still report the HTTP loop live.

## Tests

HTTP schema tests additionally require the pinned development validators in
`pico_w/tests/requirements.txt`. They are not host runtime dependencies.

Example Windows Release test setup:

```powershell
py -3 -m venv work/http-tests-env
.\work\http-tests-env\Scripts\python -m pip install -r pico_w/tests/requirements.txt
cmake -S pico_w -B work/pico-tests -DGECKO_SANITIZERS=OFF -DPython3_EXECUTABLE="$PWD/work/http-tests-env/Scripts/python.exe"
cmake --build work/pico-tests --config Release
ctest --test-dir work/pico-tests -C Release --output-on-failure
```

GCC/Clang sanitizer builds on Linux use the commands in [VALIDATION.md](VALIDATION.md).
Windows executable discovery/path conventions are covered by tests; native Windows
execution was not available in the Linux validation environment. The documented
procedure uses portable C++17, standard Python and CMake multi-config handling,
not an unverified claim that Windows binaries were built or run here.

See [HTTP_API_V1.md](HTTP_API_V1.md), [OpenAPI](../api/openapi-v1.json) and the
[Agile Alert handoff](AGILE_ALERT_HANDOFF.md) for integration details.
