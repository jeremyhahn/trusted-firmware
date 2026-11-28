### Added

- **The STM32H5 RNG read is tested on live seed and clock errors** (#599).
  `test/unit/test_stm32h5_rng.c` scripts a seed error that is set but not yet
  latched (SECS, no SEIS) after the DR read: the word is dropped and the next
  one taken; and a clock error (CECS) still set when the recovery polls: the
  read waits for it to clear before it takes the word
  (`src/hal/cortex-m/stm32h5/hal_rng.c`). Test only; the driver is unchanged.
