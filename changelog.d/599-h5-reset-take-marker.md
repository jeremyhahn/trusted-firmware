### Added

- **Host tests for the STM32H5 reset-cause take's reset-request marker**
  (#599, `src/hal/cortex-m/stm32h5/soc_reset.c`). `test_stm32h5_reset` now
  pins that the take decodes with the caller's marker (a CPU reset request
  taken without it is unknown, RCC_RSR still cleared with RMVF), and that
  any nonzero marker value counts as asked. Test-only: no firmware changes.
