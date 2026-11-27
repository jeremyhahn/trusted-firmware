### Added

- Host tests pin that the STM32H5 crypto provider (`CRYPTO=stm32h5`,
  `src/crypto/stm32h5/provider.c`) hands image verify, sign, pubkey, HMAC,
  counter and random to the soft tier with their arguments and returns its
  answer unchanged, and reports the soft tier's tier and algorithms (#599). A
  provider whose image verify answered `TFW_OK` without the soft tier's
  check passed every test before.
