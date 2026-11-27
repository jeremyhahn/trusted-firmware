### Added

- **Host tests for the STM32H5 tone timing's rounding edge** (#599).
  `test_stm32h5_calc` now pins that the tone timing takes the next prescaler
  when the rounded half-period match passes the 16-bit counter, fills the
  counter exactly at the edge, and refuses a tone above the timer clock or a
  zero clock untouched (`src/hal/cortex-m/stm32h5/h5_calc.h`).
  Test-only: no firmware changes.
