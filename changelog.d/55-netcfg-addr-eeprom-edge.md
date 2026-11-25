### Added

- **Tests for both edges of the tfwsim Ethernet harness's network block
  address check** (#55). The harness takes 0x0FE0, the last block the 4 KiB
  EEPROM holds, and refuses 0x0FE1 (one byte past the EEPROM) and 0x00BF (one
  byte inside the platform block).
