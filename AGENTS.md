# Repository instructions

- Work only in `Bubchez/gecko-rp2040` on `feature/pico-w-bridge`. Verify branch,
  remote and working tree before edits. Preserve existing user changes.
- Start with repository context: read `pico_w/README.md`,
  `pico_w/docs/IMPLEMENTATION_PLAN.md`, `ROADMAP.md`, `PROTOCOL_EVIDENCE.md`,
  `LICENSING.md` and `VALIDATION.md` under `pico_w/docs/` when present.
- Keep chat updates concise. Keep detailed findings, assumptions and test evidence
  in repository documents and commits; push authorized work to the feature branch.
  Final responses should normally be at most 100 words with outcome, tests,
  blockers, commit references and next action.
- Put new firmware/core/tests under `pico_w/`. Do not change `arduino/`,
  `components/`, `esphome/`, original examples/assets/tools, the separate Pi Zero
  application, or Agile Alert. Do not merge into the default branch.
- T09 is authorized only for simulation firmware implementation, compilation and
  offline validation. Keep Gecko GPIO inactive. Exclude physical I2C transport and
  actuator execution entirely. No flashing, deployment, T10 or real bus work.
  Byte encoding is offline data only. Later roadmap gates require authorization.
- The current follow-up authorizes isolated Pico W bench preparation: a Windows
  launcher, local USB provisioning/Wi-Fi reconnect diagnostics, automated read-only
  checks and redacted reports. Do not access hardware in this workspace or flash
  automatically. The user-run launcher must require an isolation declaration before
  USB access. No Gecko connection, physical I2C, actuator execution or T10.
- Before copying implementation code, establish its exact license and provenance.
  Missing upstream permissions block reuse, not independently authored code from
  documented protocol facts. Preserve attribution; never blanket-relicense legacy
  files. Do not run the historical source-extraction probe as new-core acceptance.
- Distinguish host-verified behavior from documented examples, synthetic models
  and unverified hardware assumptions. Never call synthetic data a bus recording.
- Use bounded byte views and explicit errors. Validate lengths, routing, types and
  evidenced integrity rules before indexing or accepting state. Unknown profiles
  and ambiguous multipart integrity must fail closed by default. No silent
  truncation, implicit resync or guessed frame trailers/sequence numbers.
- Add meaningful host tests for core changes, including malformed/truncated input,
  boundary sizes and state after errors. Build with strict warnings and ASan/UBSan:
  `cmake -S pico_w -B work/pico-core -DGECKO_SANITIZERS=ON`,
  `cmake --build work/pico-core`, `ctest --test-dir work/pico-core --output-on-failure`.
  Record exact commands, versions, results and limitations in `VALIDATION.md`.
- T08 HTTP/schema tests require Python 3.10+ and the development validators in
  `pico_w/tests/requirements.txt`. Install them in a local venv and configure
  `-DPython3_EXECUTABLE=<absolute venv Python path>` when CMake selects Python.
  The runnable host mock needs only standard Python, CMake and a C++17 compiler.
  Read `HOST_MOCK.md` and `HTTP_API_V1.md` before changing HTTP behavior. Keep all
  v1 operations read-only; HTTP reads must not advance the session. T09 firmware
  must share the existing protocol/session model and preserve API semantics.
  Pin SDK/toolchain inputs; run host sanitizer/Release and offline cross-build
  checks. Mark all hardware, USB, Wi-Fi, flash and timing tests unverified until
  performed under separate authorization. Stop before flashing, deployment or T10.
- Keep scratch/build files in `work/` and out of commits. Check the final diff,
  original-tree preservation and tests before committing. Keep review, instructions
  and implementation commits distinct. Never claim tests or pushes that did not run.
