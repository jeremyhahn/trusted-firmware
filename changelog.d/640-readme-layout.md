### Fixed

- **README.md's Layout block names the per-architecture HAL and `boot/`**
  (#640). It said `src/hal/avr/` was the only code that touches a register;
  the build picks `src/hal/<arch>/` (avr, cortex-m, esp32) for the firmware
  and `boot/hal/<arch>/` for tfw-boot, which the block now lists.
