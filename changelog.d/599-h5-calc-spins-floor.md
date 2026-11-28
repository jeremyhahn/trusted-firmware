### Added

- **Host tests for the STM32H5 busy-wait spins' whole-MHz divide** (#599,
  `src/hal/cortex-m/stm32h5/h5_calc.h`). `test_stm32h5_calc_spins` now pins
  that tfw_h5_spins_us divides the core clock to whole MHz before it
  multiplies (64999999 Hz for 10 ms is 640000 spins, not 649999), that 1 MHz
  gives one spin per microsecond, and that a zero-microsecond wait gives 0
  spins. Test-only: no firmware changes.
