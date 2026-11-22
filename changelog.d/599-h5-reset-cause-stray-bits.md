### Added

- **A host test that a brownout or a requested reset beside any other RCC_RSR
  bit decodes as unknown on the STM32H5** (#599). `test_stm32h5_reset` now
  sweeps every bit but the pin's beside `BORRSTF` and beside `SFTRSTF` with the
  firmware's marker, as it already did beside the watchdog flags. No code
  changed.
