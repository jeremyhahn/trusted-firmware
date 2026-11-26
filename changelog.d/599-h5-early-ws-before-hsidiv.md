### Added

- STM32H5 SoC host test: the early hook reloads the watchdog, then writes and reads back the `FLASH_ACR` wait states before it clears the HSI divider in `RCC_CR` and polls `HSIDIVF`, as a clock raise must (Refs #599).
