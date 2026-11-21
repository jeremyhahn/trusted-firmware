### Added

- **A refused STM32H5 ADC read is tested to write no register** (#599).
  `test/unit/test_stm32h5_adc.c` compares the whole register model before and
  after a channel past the board (before and after init) and a read while the
  converter is not up: the pin's mode and pull, SMPR1/SMPR2, SQR1, ISR and CR
  stay as they were. Test only; the driver is unchanged.
