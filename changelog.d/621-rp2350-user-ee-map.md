### Added

- **The Pico 2 W's flash-map test pins the application's EEPROM sector**
  (#621). `test_boot_rp2350_map` under `make boot-test` now holds
  `TFW_CM_USER_EE_BASE` at `0x103FE000` and `TFW_CM_USER_EE_SIZE` at one
  4 KiB sector, on a sector boundary, ending at the platform block and above
  the application, so a change that moves what the EEPROM family erases
  fails a host test.
