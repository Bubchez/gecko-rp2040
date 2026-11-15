# Offline review artifact — do not deploy in T09

`gecko_pico_w_SIMULATION_NO_I2C.uf2` is the reproducible RP2040/Pico W simulation
image. It has not been flashed, booted or hardware validated. Gecko GPIO is
configured inactive; physical I2C transport and actuator execution are absent.

Verify `SHA256SUMS`; see `manifest.json` for pinned inputs, implementation digest
and memory evidence. Rebuild with `pico_w/tools/build_firmware.py` as described in
`pico_w/docs/PICO_SIM_FIRMWARE.md`. The artifact is intended for review, not a
release/deployment permission. Stop before flashing, deployment or T10.

Preserve the notices under `pico_w/third_party/` with every copy. This combined
binary includes Pico SDK, CYW43, lwIP, TinyUSB and compiler runtime material under
their respective terms, including the Raspberry Pi semiconductor-only CYW43 grant.
It is not blanket MIT and grants no permission to reuse inherited Gecko source.
