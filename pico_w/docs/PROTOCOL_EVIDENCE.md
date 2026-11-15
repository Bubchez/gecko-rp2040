# Protocol evidence register

Updated 2026-11-15 for the independently authored host core. There are **zero
hardware captures**. A passing unit test verifies code behavior against its stated
input/specification; it cannot verify that a YE-3 V2 pack uses that specification.
The existing component remains a historical reference, not a copied implementation.
See [LICENSING.md](LICENSING.md) for the no-reuse decision.

Sources below refer to upstream documentation at baseline
[`3baacda4c0e3ae58e8b014692cf5f146db2b3318`](https://github.com/zteifel/esphome-gecko/tree/3baacda4c0e3ae58e8b014692cf5f146db2b3318).
Protocol research is attributed to zteifel/esphome-gecko and its credits to
agittins. Numeric layouts and command bytes are interoperability facts; no source
functions, class definitions, Wire code, Geckolib tables or original asset files
were copied into the new implementation.

| ID | Fact or hypothesis | Provenance / confidence | Current behavior |
|---|---|---|---|
| E01 | Exact 15-byte GO and ACK | [README handshake](https://github.com/zteifel/esphome-gecko/blob/3baacda4c0e3ae58e8b014692cf5f146db2b3318/README.md#L480); documentation, no capture | Encode/decode exact literals. Tests verify both happen to XOR to zero, and XOR-zero malformed frames are rejected. Literal matching does not prove a universal checksum rule. |
| E02 | Controller command envelope and XOR trailer | [README commands](https://github.com/zteifel/esphome-gecko/blob/3baacda4c0e3ae58e8b014692cf5f146db2b3318/README.md#L643); documentation | Check reserved bytes, routing, family, subtype, lengths and checksum before typed acceptance. No length-only dispatch or side effects. |
| E03 | Temperature raw = °C × 18, 16-bit big-endian | [README temperature table](https://github.com/zteifel/esphome-gecko/blob/3baacda4c0e3ae58e8b014692cf5f146db2b3318/README.md#L688); documentation | Golden 26/36.5/37/40 °C vectors and all 29 half-degree steps pass. Complete vectors with computed checksum are labeled synthetic/documentation-derived, not captured. |
| E04 | 26–40 °C and 0.5 °C request increments | Proposed [API v1](HTTP_API_V1.md), not a measured pack constraint | Offline encoder rejects nonfinite/out-of-range/non-half-degree values; does not silently clamp. These API restrictions are not claimed as the full wire value range. |
| E05 | C/S bytes are carried by function commands | README C82/S81 examples; documentation | Caller must supply nonzero version bytes. No automatic C82/S81 default; arbitrary supplied versions do not establish profile compatibility or control permission. |
| E06 | Light 0x33 / circulation 0x6B, states 0/1; program IDs 0–4 | README command tables; documentation | Offline encoders and strict decoders implemented, with exact documented program vectors. No transmission or actuation exists. |
| E07 | Two-bit pump demand fields; low/high meanings may differ between demand/output | Review facts and existing documentation are incomplete/conflicting | `replace_pump_demand` is an exhaustive bit-packing helper only. No pump wire-command encoder, fitted-pump discovery or supported-speed claim. Semantic enum is a candidate model, not validated pump support. |
| E08 | Filename at bytes 16 onward; names such as inYT_C82.xml / inYT_S81.xml | README handshake and review; documentation | Bounded candidate parser requires exact inYT_C/S + 1–3 digits 1–255 + .xml and a terminator. Full incoming routing/trailer remain unverified; default rejects this family. |
| E09 | Clock fields 14–20; all-FF date can mean time-only | README clock description; documentation | Candidate parser checks XOR, valid Gregorian date, hour/min/sec and weekday range. Assumed inbound envelope is synthetic; even checksum-checked candidate remains marked synthetic. Partially FF dates rejected. No clock setter. |
| E10 | Multipart header is 16 bytes, continuation at byte 9, RQ at 14/15; 78/78/54 yields 162 bytes | README status description; documentation, incomplete raw examples | Synthetic-only decoder/reassembler. Keeps every byte after the 16-byte header. No guessed trailer stripping or application ACK. Default rejects unknown integrity. |
| E11 | Inbound envelope used in tests: 17 09 … 17 0A, flags per helper | Explicit synthetic assumption, not complete hardware evidence | Only `DecodeMode::SyntheticCandidate` permits it. Caller-supplied ordering metadata is explicitly synthetic, never parsed as an actual wire counter. |
| E12 | v51 candidate status: byte 1 marker, 3 quiet, 5 demand, 6 device bits, 7 pump output, 21/23 temperatures, 53 light demand | README status offsets and review; candidate interpretation, not a copied Geckolib initializer | Only explicit InYtV51 + synthetic mode + exactly 162 bytes accepted. Heater bit 5, candidate circulation bit 2. README's alternative circulation descriptions remain unresolved. Hardware_verified is always false. |
| E13 | Status temperature plausibility | Target 8–42 °C from review; actual >0 through 100 °C is a new synthetic plausibility policy | Independently optional temperature fields retain raw values; 0/FFFF/implausible raw yields no Celsius value. No fabricated 0 °C from an invalid field. Limits require capture confirmation. |
| E14 | Other status lengths, v50 and combined config/status tails | Existing sources disagree about 390 vs 405 bytes and layout shifts | Unsupported by status decoder. Assembler can bound/copy a 405-byte synthetic message without claiming it knows what that payload means. No size autodetection or fallback v51 assumption. |
| E15 | Four 6-byte reminders: ID, YY/MM/DD, little-endian days | README notification description; documentation | Validates all four unique IDs, real dates, nonzero intervals; UTC/timezone-free calendar arithmetic. Explicit D/M/Y override exists; no autodetection. Full 77-byte candidate frame integrity remains unverified. |
| E16 | Repeated START, two-byte reads, 0x17, 100 kHz and multi-master timings | Legacy documentation/review, no measurements | Synthetic transaction/role model now checks address, repeated START, two-byte read count and final STOP. No physical HAL, electrical waveform, GPIO, network or timing validation exists. |
| E17 | 250 ms assembly gap, 1 s total; externally known fragment order/loss/session | Simulator policy choices, not pack deadlines/counters | Injected uint64 microseconds; exact deadline expires; regression, session/loss change, missing/duplicate/out-of-order metadata and overflow reset the entire assembly. |
| E18 | TX/RX phase durations, tie-breaking and collision/restore model | T06 synthetic policy, no hardware equivalence claim | Exact expectations and bounded queues; deadline precedes completion on a tie; RX while master owns the modeled bus is explicit loss; atomic synthetic recovery is not a hardware procedure. |
| E19 | GO, two file ACKs, clock ACK, then LO | Documented handshake outline plus synthetic inbound envelopes | T07 waits for each TX completion before advancing. Default C-then-S and explicit EitherSynthetic order have separate tests; duplicates are bounded and cannot renew progress deadlines. |
| E20 | C82/S81 binding selects candidate v51 | Explicit simulation configuration only | No automatic profile selection from marketing V2 or broad status-version range. Default is unknown/inactive; other profile pairs reject before participation. |
| E21 | Status acquisition age, status deadline and freshness | Conservative host policy, not a Gecko timestamp or measured freshness limit | A complete assembly carries first/completion synthetic arrival times; oldest-fragment age gates freshness. Old epoch, noise, unrelated frames and partial data cannot refresh status. |
| E22 | Reconnect budget, jitter and terminal release | Seeded host policy, not observed Gecko retry behavior | Capped exponential delay, bounded failed attempts, full new epoch/handshake; fresh status alone resets failure budget. Terminal fault deactivates participation until explicit local restart. |
| E23 | Read-only enforcement | Software invariant tested on host | Both layers deny actuator requests; only exact GO/fixed ACK bytes enter the simulator. Physical I/O is absent, even for GO/ACK or slave replies. |

## Fixture provenance

[`fixtures/protocol-examples.json`](../fixtures/protocol-examples.json) contains
15 complete outbound examples: two literals, five documented program vectors,
four temperature vectors and four switch vectors. Examples with calculated XOR
or selected version bytes are labeled synthetic and explain their derivation.
All have source links, explicit direction/address, null capture timestamps and
hardware_verified false. No ellipses or placeholders are accepted.

Candidate clock/XML/notification/status/fragment inputs are constructed by named
helpers in [`tests/core_tests.cpp`](../tests/core_tests.cpp). Their complete
envelopes, timing and ordering are generated models, not bus recordings. The test
fixture validator checks metadata, exact byte counts, integrity labels and typed
acceptance through the compiled core; it also rejects incomplete and falsely
hardware-labeled fixtures.

## What is host-verified

Bounded access/copy and rejection behavior, command bytes against explicit golden
vectors, calendar arithmetic, optional invalid fields, assembly capacity/state
reset, and deterministic malformed/mutation inputs pass the tests recorded in
[VALIDATION.md](VALIDATION.md). This establishes implementation behavior on the
tested compiler, not compatibility or timing on Gecko hardware.

Promotion of a candidate requires complete attributed raw captures, transaction
boundaries, electrical validation, actual C/S filenames and a reviewed checksum/
ordering policy. Until then, keep candidate mode explicit and physical work blocked.
