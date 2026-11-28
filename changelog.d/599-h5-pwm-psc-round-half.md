### Added

- **Host tests for where the STM32H5 PWM prescaler's rounding turns**
  (#599, `src/hal/cortex-m/stm32h5/h5_calc.h`). `test_stm32h5_calc_spins` now
  pins that tfw_h5_pwm_psc rounds the timer clock over 255000 to the nearest
  divider: a divider of exactly n + 0.5 rounds up (382500 Hz is PSC 1,
  63877500 Hz is PSC 250) and one clock under it rounds down (382499 Hz is
  PSC 0, 63877499 Hz is PSC 249). Test-only: no firmware changes.
