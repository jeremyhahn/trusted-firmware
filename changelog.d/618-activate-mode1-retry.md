### Added

- **An ESP32 OTA_ACTIVATE retry is matched on the pending activation's mode**
  (row `ota`, #618, m1100 (3)). `test/unit/test_esp_ota.c` pins that after a
  mode 1 activate the mode 1 retry succeeds, a mode 0 one is refused, and
  neither writes a record nor sets the boot slot again.
