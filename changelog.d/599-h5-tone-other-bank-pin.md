### Added

- **Host tests for the STM32H5 tone's pin on another bank** (#599).
  `test_stm32h5_tone` now pins that the tone refuses pin 5 on any bank but
  GPIOB (PA5, PC5) with `TFW_ERR_BAD_PIN`, touching no register, and leaves a
  sounding tone as it was (`src/hal/cortex-m/stm32h5/hal_tone.c`).
  Test-only: no firmware changes.
