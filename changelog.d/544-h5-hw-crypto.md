### Added

- STM32H573I-DK: a hardware crypto tier, `CRYPTO=stm32h5` (#544). It is the
  soft root with the part's HASH computing SHA-256 and its PKA verifying
  ECDSA P-256 operator signatures; each falls back to software when its
  peripheral does not answer. Keys stay in flash and signing stays in
  software, so the tier reported is the soft one's. The board's default is
  still `soft` until the hardware slice confirms the register facts. On the
  kit the PKA reaches INITOK only with the HSI48 running, so the provider
  starts it before the PKA; `make firmware CRYPTO=stm32h5` now builds.
