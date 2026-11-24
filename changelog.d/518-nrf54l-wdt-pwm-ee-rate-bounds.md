### Added

- **The nRF54L15 tests reach five checks no test reached before** (#518).
  An early watchdog that does stop has both events cleared and is not
  reloaded; a PWM release drops a stale STOPPED, so the next start waits for
  the stop to land; a write at the first byte past the boot EEPROM span
  lands; one below each TWIM rate cut-off falls to the next rate down and
  99999 Hz is `TFW_ERR_RANGE`; a tone of 0x80000000 or 0xFFFFFFFF Hz is
  `TFW_ERR_RANGE` with the outputs as they were.
