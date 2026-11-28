### Added

- **Host tests for the STM32H5 PWM CCER on branch and channel wrap** (#599,
  `src/hal/cortex-m/stm32h5/h5_calc.h`). `test_stm32h5_calc_spins` now pins
  that tfw_h5_pwm_ccer with the channel on sets CCxE, clears CCxP and keeps
  every other bit (0xFFFF with channel 1 on is 0xFFFD, channel 3 on is
  0xFDFF), and that a channel number past 4 wraps to its nibble inside
  CCER's 16 bits (channel 5 is channel 1, channel 8 is channel 4).
  Test-only: no firmware changes.
