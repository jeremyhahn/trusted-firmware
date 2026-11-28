### Added

- **Host tests for the STM32H5 ADC init clearing an earlier owner's
  configuration** (#599, `src/hal/cortex-m/stm32h5/hal_adc.c`).
  `test_stm32h5_adc` now pins that `tfw_hal_adc_init` clears a clock mode,
  differential selection and resolution, alignment and continuous bits left
  by an earlier owner. Test-only: no firmware changes.
