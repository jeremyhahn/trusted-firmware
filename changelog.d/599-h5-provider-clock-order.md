### Added

- **STM32H5 crypto provider init clock test** (#599): a host test now checks
  that `tfw_crypto_init` turns on the HASH, RNG and PKA bus clocks (RCC
  AHB2ENR bits 17-19) by OR, keeping the clocks of other peripherals, before
  it starts the HSI48 and the PKA.
