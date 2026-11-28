### Added

- **The STM32H5 ADC is tested not to take a flag left set for its own** (#599).
  `test/unit/test_stm32h5_adc.c` re-inits with ADRDY left set by the first
  init and a converter that never comes ready: the init is a timeout and reads
  are refused; and it hangs a read's conversion with EOC left set by the read
  before: the read is a timeout and its value is not written
  (`src/hal/cortex-m/stm32h5/hal_adc.c`). Test only; the driver is unchanged.
