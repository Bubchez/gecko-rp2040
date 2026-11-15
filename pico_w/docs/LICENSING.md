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
