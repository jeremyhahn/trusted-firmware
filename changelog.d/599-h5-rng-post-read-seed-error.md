### Fixed

- **The STM32H5 RNG read no longer returns a word taken while a seed error
  latches** (`test_stm32h5_rng`, #599). The read checks RNG_SR again after it
  takes a word from RNG_DR and drops a word read while a seed error latches
  (SECS or SEIS), recovering once and reading again, as ST's HAL does, so a
  word without enough entropy is never returned. A clock error after the read
  keeps the word and is left to the next read's check.
