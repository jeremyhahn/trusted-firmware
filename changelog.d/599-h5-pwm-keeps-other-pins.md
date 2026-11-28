### Added

- **An STM32H5 PWM write is tested to keep the bank's other pins** (#599).
  `test/unit/test_stm32h5_pwm.c` presets AFRL, AFRH, PUPDR and MODER with
  every other pin configured, then writes and stops a channel on a high pin
  and a low pin: only that pin's alternate-function nibble, pull field and
  mode field change (`src/hal/cortex-m/stm32h5/hal_pwm.c`). Test only; the
  driver is unchanged.
