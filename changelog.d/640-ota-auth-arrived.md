### Added

- **Host tests for OTA_AUTH's "arrived" flag** (#640, m1117 (1)).
  `test_ota_auth_mutate` now checks that an OTA_AUTH of every length, whole
  or refused, counts as arrived, that a take leaves it set, and that only
  `tfw_ota_auth_settle` clears it. Tests only: no wire format, status code or
  EEPROM layout changed.
