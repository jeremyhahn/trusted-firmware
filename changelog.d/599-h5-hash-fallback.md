### Fixed

- STM32H573I-DK, `CRYPTO=stm32h5`: a HASH peripheral that times out once now
  switches SHA-256 to software for the rest of the boot (#599), as the PKA
  path already did. A hung HASH no longer costs the status-poll timeout on
  every digest; `tfw_crypto_init` tries the peripheral again.
