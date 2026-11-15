# Repository review and evidence

Reviewed 2026-11-15, `Bubchez/gecko-rp2040`, branch `feature/pico-w-bridge`,
baseline commit `3baacda4c0e3ae58e8b014692cf5f146db2b3318`.
The baseline findings below are historical, not claims about the independently
implemented new core. See [VALIDATION.md](VALIDATION.md) for current acceptance
tests and [LICENSING.md](LICENSING.md) for the no-upstream-code-reuse decision.
The remote feature branch and master both pointed to this commit when inspected.
No AGENTS.md, existing tests, CI workflow, Pico build, or captured bus-trace corpus
was found in the checkout. Documentation contains examples, including incomplete
hex with ellipses; these are not complete hardware recordings.

## Existing implementation

| Area | Inspected source at baseline | Behavior and portability |
|---|---|---|
| Nano proxy | `arduino/src/main.cpp:1-164` | UART at 115200, hex forwarding, slave address 0x17, two-zero-byte slave read response; switches to master for write/repeated-start/read-two/STOP and back. Replace with RP2040 HAL, retain transaction semantics for testing. |
| AVR I²C | `arduino/lib/Wire/src/Wire.cpp`, `utility/twi.c`, `twi.h` | Patched 128-byte buffers, AVR registers and TWI ISR, 25 ms timeout selected by main. Not reusable as an RP2040 driver. |
| Frame/proxy decoder | `components/gecko_spa/gecko_spa.cpp:373-709` | UART length/hex decoding, length-based dispatch, filename versions, multipart header removal. Reuse protocol knowledge, replace unsafe validation and transport coupling. |
| Command encoder | same file, `132-233`, `301-321` | XOR trailers, function IDs, version bytes, shared pump-demand bitfield, temperature ×18 big-endian, clock layout. Portable algorithms after pure-function extraction and evidence checks. |
| Status parser | same file, `711-940`; header `26-70` | v50/v51+ offset tables (Geckolib offset minus 254), flags, temperatures, pumps. Separate parsing from publishing; exact target layout remains unverified. |
| Handshake/scheduling | same file, `37-130`, `454-555` | GO every 23 s; ACK any 33-byte frame and clock; LO logged as complete. No explicit session state machine. |
| Notifications | same file, `970-1051` | Four six-byte entries, configurable date order, days added using mktime; ESPHome preferences. Reuse validated field interpretation, replace date calculation/persistence. |
| Framework integration | header; all component Python modules | Component/UARTDevice, entities, publishing, preferences, GPIO reset, time, serial_proxy, ESPHome code generation. Keep out of portable core. |
| Deployment | `example.yaml`, `esphome/*.yaml`, `tools/arduino_ota.py` | ESP32 boards, HA native API, OTA and Nano UART bootloader bridge. Do not port these dependencies to Pico HTTP firmware. |

The README refers to `spa_protocol.h`, which does not exist: logic is currently in
`gecko_spa.cpp`. The target's marketing revision **V2 is not proof of status layout
v50/v51 or C82/S81**. Firmware must report the observed filenames separately.

## Findings, ordered by porting priority

Priority P0 blocks physical connection/control or source redistribution; P1 blocks
a reliable port; P2 affects correctness/maintainability. Confirmed means reproduced
by host probes or directly evident in source; timing consequences need hardware.

| ID | Priority / evidence | Source | Finding and required treatment |
|---|---|---|---|
| R01 | P0, sanitizer reproduced | `gecko_spa.cpp:390-406` | `atoi` length is unchecked, actual hex length is unchecked, missing delimiter is accepted. `RX:18:00` reads beyond the supplied string. Reject unless bounded decimal length, colon, exact 2×length hex, and allowed byte count all validate. |
| R02 | P0, sanitizer reproduced | `gecko_spa.cpp:401-406,425-450` | Decode caps at 128 bytes but forwards original length through uint8_t; `RX:200:` plus 200 zero bytes makes downstream code read beyond the 128-byte local array. Negative and >255 lengths also narrow/wrap. Use size_t with checked conversion and explicit bounded views. |
| R03 | P1, reproduced | proxy `25-33,95-113`; component `373-383` | Invalid hex digits map to zero. `TX:GG` forwards a zero byte despite documented INVALID_HEX behavior. Remove UART codec from physical Pico path, reject malformed imported recordings and API input strictly. |
| R04 | P0, source; false acceptance reproduced | `gecko_spa.cpp:425-577` | No inbound checksum verification; classifies config/program mainly by length, ACKs all 33-byte arrays, updates program from a corrupt all-zero-header 18-byte array. Validate family, routing/header, lengths and evidenced checksum policy before ACK/state changes. GO/LO/fixed ACK may be special cases; do not blindly XOR-check every frame. |
| R05 | P1, source; corruption reproduced | `gecko_spa.cpp:579-697` | Multipart buffer has no expiry, sequence/session correlation or loss detection. An hour-old fragment and unrelated last fragment become a status. Overflow silently skips the new payload while retaining the prefix. Abort the whole assembly on overflow, timeout, gap, restart or ambiguity; never publish a partial prefix. |
| R06 | P1, source | proxy `42-47,138-148` | Single shared volatile frame can be overwritten between receives or while the foreground prints it. Volatile does not make a frame snapshot atomic; no queue/overflow counter. Use bounded owned frames and an IRQ-safe queue. Verify burst delivery on bench. |
| R07 | P1, reproduced + source | `gecko_spa.cpp:425-434,103-129,711-718,834-917` | Zero-length traffic marks connected; arbitrary traffic resets age. Standalone status skips plausibility checking; only embedded status uses it. Temperature valid if either raw value is nonzero, admitting the other zero or absurd values. Track transport activity, handshake, validated state and per-field validity separately. |
| R08 | P0 for future control, source | `gecko_spa.cpp:132-233,264-286,413-419` | Commands allowed without established versions, fresh status, ready state or pack capability. TX:ERR just decrements pending count; no correlated outcome/queue. I²C ACK is not proof the device applied a change. Add safety policy, bounded serialized commands, explicit lifecycle, expiry and observed readback. |
| R09 | P0 for future control, source | `gecko_spa.cpp:152-205`; header `129-133` | All pump commands rewrite a shared demand byte from last status; rapid P1/P2 requests can overwrite each other. Nonzero requested speeds collapse to ON/HIGH; header says experimental separate function IDs but implementation uses common 0x03. Serialize read-modify-write against fresh demand and validate exact fitted pump capabilities. Disable experimental pumps. |
| R10 | P1, reproduced | `gecko_spa.cpp:221-225` | NaN bypasses `<26 || >40`, reaches lroundf and emits a temperature command. Reject nonfinite numeric input before conversion. Host result does not establish how AVR/ESP math libraries handle the invalid rounding. |
| R11 | P1, source | `gecko_spa.cpp:1073-1079,1113-1131` | Climate and select publish desired state before device confirmation; rejected temperature can still appear selected. Unknown select strings default to Standard and are persisted. Separate requested and observed values; reject unknown enums. |
| R12 | P1, source | proxy `57-88`; component `264-282` | Proxy ignores Wire.write count and requestFrom returned count/contents, so write success plus read NACK/short read can report TX:OK. Component sends before checking reset/boot state. Require exact write/read counts and meaningful error classification. |
| R13 | P1, source | component `466-508,626-646`; header `243-264` | Any parsed version <=50 selects old table; all newer versions use v51+ without compatibility proof. First 120-170-byte assembly with byte[1]==0 locks status length. Unknown profiles should expose raw observations and inhibit writes; whitelist evidenced layouts. Parser functions should always receive length. |
| R14 | P2, source | component `513-537,982-1049` | Date checks permit impossible calendar dates; hour/min/sec not checked; mktime normalizes invalid dates and depends on timezone/time_t. Bad reminder entries leave old dates displayed. Use calendar validation, timezone-free date addition and explicit stale/invalid metadata. |
| R15 | P1, source | component `288-319,541-547` | Automatic sync waits 10 s after LO, but manual sync sends immediately, despite comment that immediate commands may be ACKed and ignored. Schedule clock writes through the same future command policy; keep disabled initially. |
| R16 | P2, source | proxy `151-162`; component `87-100,203-209` in header | UART overflow retains a truncated prefix; no discard-until-newline. Pending TX count is uint8_t and can wrap under bursts. Replace with bounded queues and full-line discard in any legacy fixture importer. |
| R17 | P0, confirmed missing notice | README `757-759`; Wire source headers | README claims MIT and references absent LICENSE. Local history has no LICENSE/COPYING file; upstream root listing inspected on review date also has none. Bundled Wire explicitly has LGPL-2.1-or-later notices. Clarify permissions and component notices before redistributing copied/modified source. |
| R18 | P1, build measurement | `arduino/platformio.ini`; proxy globals/stack | Nano build uses 1,636/2,048 bytes static RAM (79.9%), leaving 412 bytes for stack/heap. The TX function alone has a 128-byte local array plus call/interrupt overhead. This is limited headroom, not proof of stack overflow. Measure stack high-water on any legacy maintenance; use explicit budgets in the Pico port. |

These are local/protocol input defects, not a claim of an existing remotely exploitable
HTTP interface. The current firmware uses ESPHome's native API, not the proposed HTTP API.
Original implementation is preserved; findings here are not fixes to that implementation.

## Documentation and protocol uncertainties

- GO period: README describes 60 s; code uses **23 s**. Historical notes also say
  30 s. Treat cadence/deadline as measured profile parameters, not settled facts.
- Header comment says multipart flag byte 10; implementation and README use byte 9.
- README describes a 405-byte config payload; code accepts combined messages only
  from 300 through 400 bytes, with a default 162-byte tail. Thus a documented 405-byte
  assembly is ignored. Confirm layout and distinguish config-only/combined families.
- README's 78+78+54 => 162 assembly assumes stripping only 16 bytes per part.
  Establish whether multipart trailers contain checksums, lengths, counters, or
  data before stripping any additional bytes. Do not insert checksum bytes into
  payload or remove real payload based on a general XOR claim.
- READ two zero bytes is implemented for inbound requests; outbound read bytes
  are discarded. Their exact meaning, permitted values and timing remain unknown.
- Same 7-bit address 0x17 is intentional in the existing design. It is not the
  RP2040's own transmitted address echo and does not remove the need for arbitration.
- No complete raw status/config/notification recording with timestamps or electrical
  measurements is supplied. Generated status arrays in review probes are synthetic.
- Light is inferred from user demand, not a separately verified output feedback bit.
  Report that provenance; heater/circulation observations must not imply writable actuators.

## Verification performed

The host review script extracts selected definitions directly from original source
on each run. ESPHome publishing/logging, preferences/time, and physical transport
are replaced with minimal stubs; status/notification sinks are counters/no-ops.
It compiles with g++ 14.2.0, C++17, `-O1 -g`, ASan and UBSan, without PIE.
This checks source behavior but is not a complete ESPHome or AVR execution.

| Check | Result |
|---|---|
| `proxy_short_hex` | ASan heap-buffer-overflow, read past supplied UART string. |
| `proxy_oversize` | ASan stack-buffer-overflow, read past decoded 128-byte array. |
| `invalid_hex_becomes_zero` | Invalid Arduino TX hex reaches stubbed transport as zero. |
| `zero_length_connected` | Empty RX sets connected true. |
| `arbitrary_config_ack` | Zero-filled 33-byte array triggers ACK transmission. |
| `corrupt_program_accepted` | Invalid header/trailer still publishes program 4 internally. |
| `stale_fragment_combined` | Two synthetic 62-byte payloads one hour apart parse as status. |
| `overflow_keeps_prefix` | Nine synthetic fragments retain a 496-byte prefix after overflow. |
| `nan_temperature_sent` | NaN emits a 21-byte command on this host. |
| `temperature_golden` | 37 °C encodes 0x029A and correct XOR with C82/S81. |
| `temperature_range` | Finite 25.9/40.1 °C requests emit no command. |
| Existing Python syntax | AST parsing passes for seven component modules and `tools/arduino_ota.py` (8 files). Imports/runtime dependencies not validated. |

Re-run `python3 pico_w/review/run_characterization.py`; output and per-case sanitizer
logs go to `work/review/characterization/`. The script returns zero when all 11
**expected observations** reproduce. Nine observations characterize defects;
two checks characterize valid temperature behavior. Future acceptance tests must
require rejection/correct handling, not preservation of these defects.

### Original Arduino build: passed

Built the unmodified original Nano project using the repository's PlatformIO
configuration. Installed PlatformIO 6.1.18 into a local venv under `work/review/`;
resolved Atmel AVR platform 5.3.0, toolchain-atmelavr 1.70300.191015 and
framework-arduino-avr 5.4.0. The build dependency graph selected the bundled Wire
1.0 library. Build succeeded: **RAM 1,636/2,048 bytes; flash 5,140/30,720 bytes**.
The 128-byte-buffer compile-time assertion passed. This verifies compilation,
not correct timing, stack safety or hardware operation. The existing checked-in
HEX was neither replaced nor flashed.

Reproduction in this prepared checkout:

```sh
PLATFORMIO_CORE_DIR="$PWD/work/review/platformio" \
  work/review/venv/bin/pio run --project-dir arduino
```

Build log: `work/review/arduino-build.log` (local review artifact, not source).
The upstream PlatformIO configuration is unpinned, so record the resolved versions
above when reproducing elsewhere. A future reproducible build must pin them.

No spa hardware was connected or controlled. No full ESPHome build is claimed: dependency versions
are unpinned and ESPHome is not installed in the starting environment. A host stub
cannot validate AVR ISR timing, Wi-Fi scheduling or the actual RP2040 bus behavior.

## Attribution and licensing action

Retain `zteifel/esphome-gecko` as the upstream named by the README, its credits to
agittins for research/pictures, and the Geckolib attribution already present in
source comments. Identify which exact Geckolib project/commit supplied offsets;
comments alone do not establish its license. Preserve source notices and history.

Wire files identify Nicholas Zambetti and subsequent contributors and explicitly
state LGPL 2.1 or later. Do not apply a blanket MIT notice to them. Avoid copying
AVR Wire into the Pico source, but keep its existing notices in the preserved tree.
Obtain/verify the intended MIT grant and copyright holder(s); inventory any copied
code, assets and forthcoming SDK/CYW43/lwIP/JSON libraries and their pinned licenses.
Prepare THIRD_PARTY_NOTICES and license texts once provenance is established.
Do not fabricate a missing upstream license or assume public availability grants
redistribution rights. No upstream contact/message has been sent by this review.

## RP2040 primary references consulted

1. [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf),
   downloaded 2026-11-15, PDF build-date 2025-02-20. Sections 4.3.3 and 4.3.10
   explicitly prohibit simultaneous master/slave operation of one I²C block;
   4.3.8 covers multi-master arbitration; 4.3.10.3 covers disable/ENABLE_STATUS
   and the no-STOP SCL-hold hazard; 4.3.10.4 covers abort.
2. [Pico SDK 2.2.0 hardware_i2c implementation](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/hardware_i2c/i2c.c):
   role changes disable/reconfigure/re-enable; master calls preserve repeated-start
   intent with restart_on_next; write error mapping loses some abort detail.
3. [Pico SDK 2.2.0 slave implementation](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_i2c_slave/i2c_slave.c):
   IRQ callbacks, RD_REQ and full-RX clock stretching, START/STOP/abort completion.
4. [RP2040 I²C register definitions in SDK 2.2.0](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2040/hardware_regs/include/hardware/regs/i2c.h):
   16-entry RX/TX FIFOs, master/slave controls and abort reasons.

SDK 2.2.0 was inspected as a concrete reference, not installed or verified as the
final build dependency. Select and pin a reviewed SDK/toolchain revision during P0 setup.
