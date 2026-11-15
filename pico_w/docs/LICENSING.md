# Licensing and reuse decision

Decision date: 2026-11-15. Applies to the new portable core milestone.

The upstream README at baseline `3baacda4c0e3ae58e8b014692cf5f146db2b3318`
claims MIT and references a LICENSE file, but no such file or complete grant was
found in the checkout/history. The upstream root listing at
<https://api.github.com/repos/zteifel/esphome-gecko/contents/> was checked again
on this date and still has no license file. This is not a conclusion that the
authors refused permission; the complete permission notice remains unresolved.
No upstream contact was sent and no new grant was obtained.

Bundled AVR Wire carries LGPL-2.1-or-later notices. The exact external Geckolib
source/provenance of existing status offsets remains unidentified. Those facts
are unchanged from [the review](REPOSITORY_REVIEW.md).

## Decision before implementing the new core

**Do not reuse upstream implementation source.** Independently author the new
C++17 core and host tests using documented byte layouts, numeric protocol facts
and explicitly synthetic fixtures. Do not copy functions, class declarations,
algorithms expressed as source, comments, AVR Wire or Geckolib source/offset-table
initializers into production. Trace each supported layout to a documented fact;
leave undocumented candidate profiles unsupported until their provenance is
adequate. This avoids depending on an unresolved permission grant for this work.

This is independent implementation, not a claim of a formal clean-room process:
the repository was previously reviewed. Protocol interoperability facts are
recorded with attribution and uncertainty; this does not establish legal rights
to redistribute somebody else's implementation or assets.

Original implementation files and their notices remain untouched. The historical
review script committed separately extracts upstream functions at runtime; it
is not imported, executed, vendored or linked by the new-core build or CI. Do not
redistribute its generated upstream-source files. Historical review results are
not acceptance tests for the independently implemented core.

New implementation, tests and build scripts in `pico_w/include/`, `pico_w/src/`,
`pico_w/tests/` and `pico_w/CMakeLists.txt` carry SPDX MIT identifiers and are
covered by [the scoped license](../LICENSE). That license covers only the listed
new work, never the repository's original implementation or pre-existing review
tool. No third-party runtime/test library is vendored into the core; it depends
on the C++17 standard library. CMake/compiler/Python are build tools, not copied
implementation dependencies. Future SDK/network libraries need their own review.

## Attribution and remaining restrictions

- Protocol research/documentation: `zteifel/esphome-gecko` and its credits to
  agittins; preserve these references and the baseline source links in the evidence
  register. No pictures, logos or implementation source are copied into the core.
- The missing upstream MIT grant, original assets and Geckolib provenance still
  block redistribution of modified/reused upstream source. Do not describe this
  entire inherited repository as newly licensed MIT.
- Physical behavior, electrical compatibility and target-pack layouts remain
  unverified regardless of source-license status.

The task's licensing prerequisite is satisfied for the new milestone by this
no-reuse decision and scoped notice. It is **not** satisfied for future reuse of
upstream code; obtain the actual grant before changing that decision.

T06–T07 update: `src/transport/`, `src/session/`, their public headers and scenario
tests are independently authored within the same scoped MIT coverage. Test helpers
reuse this repository's newly authored MIT core-test conventions, not upstream
Arduino/ESPHome code. No upstream source extraction, permission waiver, third-party
source import or new license grant was used for this milestone.

T08 update: independently authored `src/host/`, `tools/start_mock.py`, API definitions
under `api/` and HTTP tests use the expanded scoped MIT notice. Runtime consumes the
installed C++/Python standard libraries; no upstream Arduino/ESPHome source, HTTP
framework, JSON parser source, SDK, assets or third-party implementation is vendored.
Test-only packages are pinned in tests/requirements.txt: jsonschema 4.23.0 (metadata
license MIT) and openapi-spec-validator 0.7.2 (Apache-2.0), installed as external
development tools with their dependencies. They are not needed by the runnable
mock. New API examples are synthetic outputs of this new core with normalized boot
IDs, not upstream captures. The unresolved permissions for inherited source/assets
remain unchanged; no upstream grant is inferred from completing T08.

## T09 dependency and binary decision

New firmware, shared simulation/runtime source and build/provisioning tools are
independently authored and fall within the expanded scoped MIT notice. No inherited
Arduino/ESPHome, AVR Wire, Geckolib or historical extracted source is copied or linked.
Their unresolved permissions still block any future reuse.

The Pico image necessarily links external SDK/radio/runtime code. SDK 2.2.0 and
three used submodules are pinned to immutable commits in
`firmware/build-lock.json`; the xPack compiler distribution is pinned by SHA256.
The SDK uses BSD-3-Clause, lwIP BSD-3-Clause, TinyUSB MIT, and CMSIS headers Apache-2.0.
For CYW43 driver/resources we rely specifically on its **LICENSE.RP**, granting use
and redistribution only with RP2040 or another Raspberry Pi semiconductor; this
UF2 is restricted to Pico W/RP2040. We do not rely on the alternate noncommercial
grant or infer permission for other chips. GCC runtime/header code retains GPLv3
plus Runtime Library Exception 3.1; the eligible unmodified GCC compilation of
independent modules is covered by that exception. Newlib/libgloss notices are
preserved in full from the verified distribution. No compiler executables or
third-party implementation source are vendored/modified here.

Exact sources, applicable terms and verbatim notices accompany the binary under
[third_party/README.md](../third_party/README.md) and its notices directory.
The UF2 is a combined binary under those respective terms, **not blanket MIT**.
Keep the notices with redistributed copies. This resolves dependency reuse for
this RP2040 simulation build only; it supplies no missing legacy grant and makes
no legal, hardware compatibility, deployment or release-readiness claim.


## Isolated bench preparation follow-up

New bench PowerShell/Python tools, USB formatter, identity and reconnect-policy
changes are independently authored within the existing scoped MIT coverage.
The regression reproducer reads only the T09 newly authored MIT provision_usb.py,
not legacy implementation/source-extraction artifacts. No inherited implementation
is copied. USB/radio doubles are authored tests, not licensed device recordings.

The user-run venv installs external pyserial 3.5 (BSD) and jsonschema 4.23.0 (MIT),
pinned in tools/requirements-bench.txt; their installed dependencies are not vendored
or bundled with UF2. PowerShell 7.4.6 is an external offline-test executable, not
redistributed. Native SDK/radio/runtime terms and accompanying notices still apply
to the refreshed 0.2.1 binary. No new legacy grant, physical compatibility claim or
permission to redistribute modified inherited source follows from this work.
