### Added

- **Host tests for an STM32H5 RNG seed error after the read once the word's
  one recovery is spent** (#599, `src/hal/cortex-m/stm32h5/hal_rng.c`).
  `test_stm32h5_rng` now pins that a word whose read latches a seed error
  after an earlier recovery (a clock error before the word, or a seed error
  after an earlier read) is reported as an error, not recovered again and
  returned. Test-only: no firmware changes.
