# Isolated Pico W bench runner

One Windows PowerShell entry point verifies the firmware artifact, provisions USB
credentials when needed, discovers the DHCP address, checks the read-only API and
tests a local Wi-Fi reconnect. **It never flashes, copies UF2 to a drive, enters
BOOTSEL, connects to Gecko, enables I2C or starts T10.** This task prepared and
tested the software offline; no Pico/USB/radio was accessed in Codex.

## Windows: minimal steps

Prerequisite: Windows PowerShell 5.1 or PowerShell 7 and **Python 3.12+ with the
`py` launcher**. No CMake, SDK, compiler or manual pip commands are needed. Use the
current `feature/pico-w-bridge` checkout and an isolated trusted development Wi-Fi.

1. Disconnect **all Gecko wiring**. Attach only the Pico W's USB cable. If its
   simulation firmware is absent/outdated, manually hold BOOTSEL while connecting
   and copy `pico_w\artifacts\t09\gecko_pico_w_SIMULATION_NO_I2C.uf2` to `RPI-RP2`
   in Explorer, then let USB CDC enumerate. Firmware copying remains entirely your
   action; the runner cannot do it. Retain `pico_w\third_party\` with the UF2.
2. From the repository root run:

   ```powershell
   .\pico_w\tools\bench.ps1 -IsolatedPico
   ```

   `-IsolatedPico` declares that the Pico has no Gecko connection. The launcher
   verifies SHA256 before starting Python, creates/reuses a private local venv,
   installs pinned pyserial/jsonschema if needed, and selects a single Pico CDC
   device by VID **2E8A** / PID **000A**. Enter SSID and hidden WPA2 password only
   on first provisioning. SSID (1–32) and WPA2 password (8–63) must be printable
   ASCII; enterprise/WPA3-only networks are unsupported. No passwords or tokens
   belong on command lines.
3. Return **only** the generated `work\pico-bench\reports\bench-*.json` file to
   Codex. The console prints its location and check results. Keep the private token
   and credentials local. Exit 0 means the selected mode completed; inspect
   `overall` to distinguish `verified_only` from a full device `pass`.

If execution policy blocks this reviewed local script, invoke it without changing
machine policy: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\pico_w\tools\bench.ps1 -IsolatedPico`.
The policy override lasts only for that process. Review the script first.

Optional switches:

| Invocation | Purpose |
|---|---|
| `...\bench.ps1 -VerifyOnly` (also the default without switches) | Verify artifact/input identity/UF2 structure and create a report; **no USB enumeration, dependency installation or network/device access**. |
| `...\bench.ps1 -IsolatedPico -Port COM7` | Disambiguate multiple Pico CDC devices. A different VID/PID is refused even with an explicit port. |
| `...\bench.ps1 -IsolatedPico -Reprovision` | Explicitly replace Wi-Fi/token/device configuration via USB. Prompts again; no firmware flashing. Normal reruns avoid flash writes. |
| `...\bench.ps1 -VerifyOnly -Python 'C:\Path With Spaces\python.exe'` | Select Python when `py` is unavailable. The same switch works with isolated mode. |

The first venv setup requires package-network access; validated existing packages
are reused. The actual bench talks only to the USB-selected device and its reported
private IPv4 address on port 8765. No router changes, LAN scans, public services,
DNS, environment proxies, redirects, application changes or cloud uploads occur.
Normal execution usually takes under a minute; each Wi-Fi association/recovery
wait is bounded to 180 s plus the current USB request's timeout. HTTP operations
have a shared 3 s deadline and are paced below the API's normal request rate.

## What runs automatically

| Check | Acceptance |
|---|---|
| Artifact | Manifest/SHA256SUMS/UF2 SHA agree; all UF2 blocks/address/family/bounds valid, final configuration sectors untouched; pinned inputs and inactive/read-only flags agree. Native input build ID matches current source, with LF/CRLF normalized. |
| USB identity | INFO reports reviewed 0.2.1 simulation version and expected native build ID, boot identity, safe mode and bounded metadata. Old T09 0.2.0 firmware returns CONFIG_REJECTED and stops before provisioning. |
| Provisioning | Existing credentials and token reused. New configuration requires CONFIG_SAVED and subsequent validated INFO. Partial serial replies are reassembled; short writes/oversize/flood/timeouts fail closed. |
| HTTP identity / authentication | Public health boot matches USB before sending a token; authenticated capabilities match device/build/boot and fixed read-only policy. Missing/wrong credentials give 401/Bearer. JSON Schema validates responses. |
| Read-only / malformed requests | All four command families and unknown intent give 403, history 404; duplicate keys/nonfinite JSON/media/oversize/query rules reject. Any unexpected 2xx command response stops immediately. |
| Freshness / diagnostics | Two whole-status samples progress while ready/fresh/synthetic; oldest-fragment age stays conservative; 13 unavailable fields remain null. Expected auth/denial counters advance, core overflow/invalid-RX counters stay zero, and safety declarations stay disabled/inactive. |
| Reconnection / recovery | One local USB WIFI_RECONNECT requests link leave and a 1 s backoff, observes offline state, then new association/attempt counters, same boot/device/build, fresh status and continued simulation/counters. No session reset, credential rewrite or HTTP control route. |

If the offline interval is too short to observe, the report fails with
`reconnect_not_observed`; a counter change alone does not fabricate an observed
outage. This is a **software-requested link restart**, not an AP-power-loss,
wrong-password, RF-range or router-outage test. Such radio conditions remain
unverified until deliberately tested separately. A successful report validates
only this isolated simulation workflow; all data still comes from synthetic core
fixtures, not Gecko recordings or physical readings.

The CDC connection uses **115200 baud with DTR enabled**, as required by SDK stdio;
RTS, 1200-baud reset and vendor-interface BOOTSEL reset are disabled. USB DTR is
unrelated to Gecko GPIO. INFO is formatted into at most 512 bytes; the fixed USB
TX FIFO is 1024 bytes with nonblocking output. Driver/host buffering still needs
real Windows USB validation. GPIO4/5 remain input/no pulls; electrical inactivity
requires measurement and cannot be established by self-reported software flags.

## Reports, secrets and failure recovery

Report contract: [bench-report-v1.schema.json](../api/bench-report-v1.schema.json).
The report contains allowlisted check names/codes, durations, bounded counters,
booleans, normalized platform and reviewed artifact hashes/version. It omits raw
USB/HTTP bodies, exception text, token/password/SSID, IP/MAC/COM/USB serial numbers,
device/boot IDs, local paths and usernames. Reports are not cryptographic
attestation. `hardware_accessed: null` means access could not be established after
a failed open/aborted launcher. Partial or verify-only runs cannot pass all checks.

The launcher protects the Windows `work\pico-bench\private` directory and existing
token with current-user/SYSTEM ACLs. The token is generated locally without output.
For an already provisioned device, a valid old `work\gecko-pico\token.txt` is copied
into that private directory when needed, without overwriting either file or
rewriting the Pico. The old file remains yours to protect. A mismatched/missing
token requires restoring the correct local token or explicitly using `-Reprovision`.
The Pico's persistent credentials and HTTP bearer traffic remain **unencrypted**;
use the isolated trusted LAN. Do not expose this service publicly.

| Report code | Next action |
|---|---|
| bootstrap_failed | Inspect bootstrap_step: mode, artifact_hash, python, private_acl, environment, dependencies or runner. Check Python 3.12, local write/ACL rights and first-time package access. No raw setup logs are included. |
| artifact_invalid / source_mismatch | Restore a consistent reviewed checkout/artifact or rebuild with pinned firmware tooling. Do not provision a mismatched image. |
| no_pico / wrong_port / ambiguous_pico | Check USB data cable/CDC enumeration and optional -Port. A BOOTSEL mass-storage drive is never selected or written. |
| firmware_update_required / identity_mismatch | Verify the selected isolated board and manually copy the reviewed UF2 if needed, then rerun. Build ID is reported identity, not a flash readback. |
| local_token_missing / local_token_invalid | Restore the token or use explicit -Reprovision; no silent credential replacement. |
| usb_timeout / usb_framing / usb_write_failed | Check cable/port exclusivity; close other serial terminals and return the redacted report. |
| wifi_timeout / http_unavailable | Check 2.4 GHz WPA2 credentials, client isolation/firewall, DHCP and same-LAN reachability. No router settings are changed by the runner. |
| Other failed contract/recovery checks | Return the report; stop before any Gecko connection or T10. |

Hardware/Windows execution has **not** run in this development environment. Offline
tests cover real native session/API code, USB/radio doubles, a loopback HTTP server,
redaction canaries and PowerShell 7 on Linux (including paths with spaces).
PowerShell 5.1, Windows ACL/USB, RF association, actual flash persistence and runtime
heap/stack/IRQ behaviour remain unverified. See [BENCH_REVIEW.md](BENCH_REVIEW.md)
and [VALIDATION.md](VALIDATION.md) for precise evidence and limits.
