### Added

- **A test that an RP2350 read failing on CS.ERR gives the borrowed pin back** (#621).
  `tfw_hal_adc_read()` writes GPIO26-28's pad and IO_CTRL back on every
  outcome; the host test now proves it after a conversion that sets CS.ERR,
  and that the next read on the same channel converts and returns its value.
