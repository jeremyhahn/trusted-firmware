### Added

- **A test that the operator key is re-read on every call** (#640).
  `test/unit/test_platform_id_mutate.c` rewrites, corrupts and fails the
  platform block without calling `tfw_platform_id_init()`: each
  `tfw_platform_id_operator_pubkey()` read follows the EEPROM, while
  `tfw_platform_id_serial()` keeps the serial init cached, as
  `src/core/platform_id.h` promises.
