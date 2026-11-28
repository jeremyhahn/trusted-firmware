### Added

- **Host tests for the STM32H5 ADC's 10 ms poll bound in spins** (#599,
  `src/hal/cortex-m/stm32h5/h5_calc.h`). A new `test_stm32h5_calc_spins`
  pins that tfw_h5_spins_us gives the ADC's 10 ms poll bound as 640000 spins
  at 64 MHz and 2500000 at 250 MHz, and that its widest products do not wrap
  32 bits. Test-only: no firmware changes.
