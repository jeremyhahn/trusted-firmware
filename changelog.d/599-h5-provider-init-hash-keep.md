### Added

- **The STM32H5 crypto provider is tested to keep a working HASH and to report
  a failed soft-tier start** (#599). `test/unit/test_h5_crypto_provider.c` runs
  two digests on a working HASH: both answer from the peripheral; and a soft
  tier whose init fails makes tfw_crypto_init return that status while the HASH
  still answers (`src/crypto/stm32h5/provider.c`). Test only; the provider is
  unchanged.
