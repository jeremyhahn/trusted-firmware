### Added

- **A host test pins the STM32H5 RNG read's own error recovery**
  (`test_stm32h5_rng`, #599): the read recovers one latched error and returns
  the word, and reports an error that comes straight back without recovering
  it again.
