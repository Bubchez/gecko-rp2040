# Pico W bridge: review deliverable

Status: **proposal awaiting review; no Pico firmware or physical control enabled**.

Target: Gecko IN.YE-3-H3.0 / YE-3-CE, V2; replace the Nano and ESP32 with one
Raspberry Pi Pico W (RP2040), serving an independently developed Pi Zero W app.

- [Implementation plan, architecture, and prioritized backlog](docs/IMPLEMENTATION_PLAN.md)
- [Proposed HTTP JSON API v1](docs/HTTP_API_V1.md)
- [Repository review and verification evidence](docs/REPOSITORY_REVIEW.md)
- [Offline source characterization](review/run_characterization.py)

Only planning documents and a host review tool are added. The original `arduino/`,
`components/`, `esphome/`, examples, and Pi Zero application are not changed.
The proposed source layout in the plan is future work, not an implemented system.

Run the review probes from the repository root:

```sh
python3 pico_w/review/run_characterization.py
```

Requires Python 3 and a Linux `g++` with AddressSanitizer and UndefinedBehaviorSanitizer.
It extracts selected original functions into a stubbed host harness under `work/`.
Its success means the documented observations reproduced, **not that the defects
are fixed or the complete firmware passes tests**. There is no bus or network I/O.

Preserve upstream attribution. The README claims MIT, but the referenced LICENSE
file is missing; bundled AVR Wire has separate LGPL notices. Resolve provenance
and licensing before redistribution (see the review).
