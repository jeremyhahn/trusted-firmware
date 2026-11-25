### Added

- **`make scripts-test` holds the ESP32 QEMU suite's firmware constants to
  their headers** (#23). `scripts/test_esp32_qemu_constants.py` reads
  `test/integration/esp32/qemu_test.py` without importing it and compares its
  hand-copied wire port, board id, crypto tiers, status codes, trial timeout,
  OTA_INFO states and flags, app-descriptor magic and HMAC_UP key purpose with
  the `#define`s and enumerators they copy, one test per source file. A
  renumber in C now fails in seconds on every host, naming both files, instead
  of only in the ESP32 QEMU job as a missing log line. No wire format, status
  code or firmware changed.
