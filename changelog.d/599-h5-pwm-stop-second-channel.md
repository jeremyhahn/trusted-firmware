### Added

- **Host tests for the STM32H5 PWM stop of channel 2 or 4** (#599).
  `test_stm32h5_pwm` now pins that stopping channel 2 or 4 takes only its
  own OCxPE preload bit off and leaves channel 1 or 3 preloaded in the same
  CCMR register (`src/hal/cortex-m/stm32h5/hal_pwm.c`).
  Test-only: no firmware changes.
