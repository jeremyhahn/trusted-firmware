### Added

- **Host test for an aborted STM32H5 PKA sign that will not re-initialise**
  (#599). `test_h5_pka` now pins that after a timed-out sign is aborted and
  the PKA never shows INITOK again, the driver (`src/crypto/stm32h5/h5_pka.c`)
  writes nothing to PKA RAM: d and k are left as loaded and the sign answers
  `TFW_ERR_TIMEOUT`. Test-only: no firmware changes.
