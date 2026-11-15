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
- This milestone is host-only: no physical bus communication, hardware deployment,
  GPIO/I2C adapters or actuator execution. Byte encoding is offline data only.
  Broader plan milestones do not authorize hardware work.
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
  v1 operations read-only; HTTP reads must not advance the session. Stop before T09
  unless a later user instruction authorizes more work.
- Keep scratch/build files in `work/` and out of commits. Check the final diff,
  original-tree preservation and tests before committing. Keep review, instructions
  and implementation commits distinct. Never claim tests or pushes that did not run.
