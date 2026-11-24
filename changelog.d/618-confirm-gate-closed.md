### Added

- **An ESP32 OTA_CONFIRM from a host that has not authenticated is refused at
  the gate** (row `ota`, #618). `test/unit/test_esp_wire_auth.c` pins that a
  host holding an OTA_AUTH signature but no operator session gets
  `TFW_ERR_AUTH_REQUIRED`, spends its signature, marks nothing and leaves the
  trial on, and that once it authenticates and sends a new OTA_AUTH the
  confirm ends the trial.
