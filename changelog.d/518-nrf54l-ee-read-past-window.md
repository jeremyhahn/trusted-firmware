### Added

- **The nRF54L15 EEPROM test reads past the platform window's end** (#518).
  A read at `TFW_CM_PLATFORM_SIZE`, one that straddles the end, or one at
  0xFFFF is `TFW_ERR_RANGE` with the caller's buffer as it was, and a read of
  the window's last four bytes still returns them.
