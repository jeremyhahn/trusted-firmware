### Added

- **The STM32H5 RNG read's status polls are tested** (#599).
  `test/unit/test_stm32h5_rng.c` scripts a latched flag that is still set when
  the recovery polls: it is cleared a second time and the word is returned;
  and a status with no DRDY yet: the read polls on and takes the word only once
  DRDY sets (`src/hal/cortex-m/stm32h5/hal_rng.c`). Test only; the driver is
  unchanged.
