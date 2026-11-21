### Added

- **Host tests pin the STM32H5 GPIO HAL's top pin and its write level** (#599).
  `test_stm32h5_gpio` now proves a low write to pin 15 is BSRR bit 31 alone,
  that pin 15's mode sets MODER and PUPDR bits 31:30 with pin 14 and the rest
  of the bank kept, and that any nonzero level (2, 0x80, 0xFF) drives the pin
  high.
