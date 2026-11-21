### Added

- **CI keeps a build of every crypto tier a board declares** (#599).
  `scripts/test_ci_crypto_tiers.py`, run by `make scripts-test`, fails when
  `.github/workflows/ci.yml` has no `make firmware BOARD=<board>
  CRYPTO=<tier>` step for a tier in a board profile's `ARCH_CRYPTO_TIERS`
  (a real make invocation: a comment or an echo naming the build does not
  count), so the STM32H573I-DK's `CRYPTO=stm32h5` image cannot drop out of CI and
  break only on the bench again (#566).
