### Added

- **`make cortex-m-dk-image BOARD=stm32h573i-dk`** (#570): the STM32H573I-DK's
  signed bench set, built and signed by `cm_image` as `cortex-m-qemu-image`
  signs a QEMU board's (which refuses the DK), into
  `build/integration/stm32h573i-dk/bench/`: the bench boot and the
  unprovisioned boot (its UDS slot erased, for
  `TestFirmwareAttestRefusedUnprovisioned`), each packed with the application
  and the platform block into a hex for `make boot-flash BOOT_FLASH_HEX=`.
  Nothing is written to a board.

### Changed

- **The hw suite's attest timeout is per board** (#570): 45 s on the
  STM32H573I-DK, whose software P-256 attest answered in 27.7 s; the suite's
  5 s elsewhere.
