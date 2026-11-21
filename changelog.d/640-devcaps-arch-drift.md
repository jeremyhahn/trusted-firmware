### Fixed

- **`docs/device-capabilities/architecture.md` matches the code again** (#640):
  the read handlers (`DIGITAL_READ`, `PULSE_*`, `ANALOG_READ`) read reserved
  pins on purpose, the HAL is `src/hal/$(ARCH)` with SoC subdirectories rather
  than one AVR HAL, `boards/` is the board list, and the lint sweep covers
  `optee/`.
