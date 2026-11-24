### Fixed

- **The ESP32-P4 ROM verifier's erased-key check is tested on every byte of the slot** (#48).
  `test_boot_p256_rom` now programs one byte at index 0, 31, 32 or 63 of an
  otherwise erased key slot and checks that the ROM is asked once and its
  answer decides (1 passes, 0 refuses), so a check that reads only one byte
  of the slot, or stops before the last, fails the suite.
