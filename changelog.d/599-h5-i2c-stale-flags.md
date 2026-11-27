### Added

- **The STM32H5 I2C master's stale-flag clear and wait bounds are tested**
  (`test_stm32h5_i2c`, #599). A NACKF or ARLO left from an earlier event no
  longer fails a write to a device that answers; lost arbitration ends the
  wait with no delay; a held BUSY line is polled exactly 25 ms; and the
  register model ignores a TIMINGR store with PE set, so a rate change that
  did not clear PE first fails `test_init_rates`.
