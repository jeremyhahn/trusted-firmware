### Added

- **An STM32H5 PKA verify on a PKA that cannot be stopped or restarted is
  tested to be a timeout** (#599). `test/unit/test_h5_pka.c` hangs the PKA
  with BUSY stuck, and with INITOK never rising after the abort: the verify
  returns TFW_ERR_TIMEOUT (the provider's cue to verify in software), never
  TFW_ERR_BUSY and never the result word left by the run
  (`src/crypto/stm32h5/h5_pka.c`). Test only; the driver is unchanged.
