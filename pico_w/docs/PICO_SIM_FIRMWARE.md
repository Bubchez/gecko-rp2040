# T09 Pico W simulation firmware

This is an **offline-built simulation image**, using the existing protocol/session
core and read-only API v1. It is not hardware validated. No flashing/deployment was
performed or authorized. Stop before flashing, deployment or T10. Disconnect all
Gecko wiring before any separately approved Pico bench work.

The review UF2, SHA256 and source/build/memory manifest are under
`pico_w/artifacts/t09/`. Preserve `pico_w/third_party/` notices with the UF2.
Reproduction commands below compile only; they never discover or access USB devices.

## Build from pinned inputs

Use Linux x86_64, or Windows **WSL2 Ubuntu x86_64**, with Python 3.12+, Git and the
ordinary native compiler/make needed to build SDK host tools. The cross compiler,
SDK and CMake/Ninja versions are pinned in `firmware/build-lock.json`. No system
Arm compiler, Arduino or ESPHome is used. Initial downloads need network access;
subsequent builds reuse verified local inputs. Allow several GB of disk space.

From the repository root on `feature/pico-w-bridge`:

```sh
python3 -m venv work/pico-build-env
work/pico-build-env/bin/python -m pip install -r pico_w/firmware/build-requirements.txt
work/pico-build-env/bin/python pico_w/tools/build_firmware.py --clean
```

Output defaults to `work/t09/artifacts/`. The tool verifies the compiler archive
and extracted files, SDK and submodule commits/cleanliness, tool versions, linked
symbols, flash reservation and memory/stack budgets. It refuses mismatched inputs.
It imports the SDK externally, disables picotool fetching, and generates UF2 from
the SDK-built binary using an independently authored RP2040 UF2 converter.

Reproduce in a separate build directory and compare:

```sh
work/pico-build-env/bin/python pico_w/tools/build_firmware.py --build-name reproduce --artifacts work/t09/reproduce-artifacts --clean
cmp work/t09/artifacts/gecko_pico_w_SIMULATION_NO_I2C.uf2 work/t09/reproduce-artifacts/gecko_pico_w_SIMULATION_NO_I2C.uf2
```

The manifest records a digest of implementation/build files rather than its own
commit (which would create a self-reference). Documentation/test-only edits do not
alter image bytes. Actual build and comparison results are in `VALIDATION.md`.
Native Windows cross-compilation is not supported by this Linux toolchain pin;
WSL2 and native Windows USB provisioning themselves remain untested here.

## Runtime architecture and boundaries

| Module | Responsibility / bound |
|---|---|
| `gecko::simulation::Model` | Shared with T08 native mock: same synthetic C82/S81 fixtures, deterministic transport, handshake/session, oldest-fragment freshness, recovery, read-only gates. No exceptions, iostream or SDK dependency. |
| `runtime::Maintenance` | Sole owner of advancement; at most 100 simulated milliseconds per invocation. Publishes copied snapshots/counters only after reaching wall time. A long stall gives 503 during bounded catch-up. |
| `runtime::Api` | Exact v1 fields, types, nulls, provenance, profile and policy; immutable cache with 1 s expiry. GET/denial has no mutable model/session reference. Firmware version and additive Wi-Fi/safety diagnostics distinguish native target. |
| Bounded HTTP/JSON engine | 2048 header bytes, 128 URI, 512 body, 8192 response; strict framing/Unicode/duplicate keys/numbers, content comparison without early token-content exit; rate 5/s, burst 10. No command acceptance/queue/history. |
| lwIP raw TCP adapter | Two static connection slots, copied TX buffers with ACK accounting; 2 s absolute receive and 5 s send deadlines; one request then close. Fixed PCB/pbuf/segment pools and 24 KiB lwIP heap. |
| USB provisioning / two flash records | ASCII SSID 1–32, WPA2 password 8–63, URL-safe token 32–128, device ID 1–64. Fixed 512-byte line/record; two reserved 4 KiB sectors, version/sequence/CRC, bounded SDK flash operation, no password/token echo. |
| Wi-Fi policy | Unprovisioned → connecting → online/backoff. Async association; 15 s attempt deadline, retry backoff 1–60 s, saturated counters. DHCP IPv4, worldwide regulatory domain, no DNS/mDNS/Bluetooth. |

Single core, cooperative polling, no RTOS. HTTP callbacks never advance the model.
The default firmware scenario is `healthy`; other fixtures remain host/offline test
inputs. There is no network scenario/configuration/reset/OTA/write-enable endpoint.
Wi-Fi reconnect preserves boot identity and the independent simulation clock.

At boot, tentative Gecko GPIO4/5 are explicitly inputs with pulls disabled. UART
stdio is disabled. Only the Pico W's internal Wi-Fi pins and USB are used. No I2C
library, bus driver, actuator executor or control encoder is linked into the image.
`GECKO_SIMULATION_ONLY=1` and the transport's fixed physical-disabled constant guard
this build; there is no runtime option to change that policy. A build-symbol check
is supporting evidence, not an electrical measurement of pin levels.

All observations stay `transport: simulated`, `mode: simulation`, source synthetic
(unknown before observations), with read-only/physical-control flags fixed. A target
model string, synthetic handshake or successful Wi-Fi association would not validate
the Gecko protocol, electrical interface or installed equipment.

## Windows flashing/configuration handoff — future approved bench use only

**Do not execute these device steps in T09.** They are the requested procedure for
later review, with no Gecko wiring attached. No USB/flash/radio test has run here.

1. After separate authorization, verify the UF2 SHA256 against `SHA256SUMS` using
   PowerShell `Get-FileHash .\pico_w\artifacts\t09\gecko_pico_w_SIMULATION_NO_I2C.uf2 -Algorithm SHA256`.
2. Hold BOOTSEL while attaching **an isolated Pico W** by USB; copy that UF2 onto
   `RPI-RP2` in Explorer. The drive should disappear and a USB CDC COM port should
   appear. This expected behaviour is unverified on hardware. No auto-flasher exists.
3. Create a local Windows provisioning environment and supply the actual COM port:

   ```powershell
   py -3 -m venv .\work\pico-usb-env
   .\work\pico-usb-env\Scripts\python.exe -m pip install pyserial==3.5
   .\work\pico-usb-env\Scripts\python.exe .\pico_w\tools\provision_usb.py --port COM5
   ```

   Enter SSID and hidden WPA2 password. The tool creates/reuses
   `work\gecko-pico\token.txt`, never prints the token/password, and reports success
   only on `CONFIG_SAVED`. Protect this local file with your Windows account ACL;
   POSIX mode 0600 does not establish a Windows ACL. No credentials are in the UF2.
4. A terminal at 115200 baud can send `STATUS` followed by Enter. The redacted
   response reports provisioning, Wi-Fi state, DHCP IP, port 8765 and inactive
   Gecko GPIO. Use that IP for server-side read-only API requests. No command may
   enable actuation. Existing host mock remains `127.0.0.1:8765`.

The device stores credentials **unencrypted** in its reserved flash sectors. Anyone
with flash access can read them. HTTP bearer tokens are also **unencrypted on Wi-Fi**:
use an isolated trusted development LAN; do not expose this service to the Internet.
No TLS, captive portal, AP, browser/CORS access or cloud provisioning is provided.
The Pi Zero/Agile Alert server-side integration must retain its own token privately.

Reprovisioning via USB closes the server and stops the radio before flash access.
It writes the alternate sector and validates the new record before acknowledging;
a CRC-valid previous record is retained if the replacement fails. Sequence rollover
refuses further writes; no automatic factory reset or write-per-boot exists.
Boot uses only validated records. Blank/corrupt flash leaves Wi-Fi unprovisioned
and the HTTP listener closed. Normal UF2 updates avoid the final 8 KiB, so credentials
persist; the procedure is **not** a secure erase. Power-loss endurance/atomicity and
USB/flash coordination require real-device tests; CRC tests alone do not establish
physical flash guarantees.

## Resource evidence and remaining tests

The link requires an explicit 8 KiB maximum libc heap, a 16 KiB core0 stack and at
least 64 KiB unused main RAM between them. lwIP uses its own fixed 24 KiB allocator;
no project model/API/HTTP object uses an unbounded heap collection. Stack usage is
checked per project function, with a 4 KiB limit; this is not a call-chain or ISR
watermark proof. The SDK's 4 KiB scratch-bank stack reservation is also counted.
The linker stops the image before `0x101fe000`; only the two configuration sectors
may be changed by local USB provisioning code. Exact final sizes are in the artifact
manifest and validation record.

Hardware tests deliberately outstanding: boot/UF2 acceptance; USB enumeration and
secret entry; Wi-Fi authentication/regulatory behaviour, DHCP/link loss and long
reconnection; actual on-device HTTP/parsing/throughput under pool pressure; GPIO4/5
electrical inactivity; runtime heap/stack/IRQ watermarks; flash power-loss/wear and
USB coordination. All real Gecko I2C/electrical/protocol tests remain excluded and
blocked by absent hardware/evidence. T10 is not authorized.
