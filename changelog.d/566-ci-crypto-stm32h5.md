### Changed

- **CI builds the STM32H573I-DK's hardware crypto tier** (#566). The cortex-m
  job builds `make firmware BOARD=stm32h573i-dk CRYPTO=stm32h5` in the
  toolchain image after the `CRYPTO=soft` image, so a build break in the PKA
  and HASH provider (#544) fails CI and not the hardware check.
