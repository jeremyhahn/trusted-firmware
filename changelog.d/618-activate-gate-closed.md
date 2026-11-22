### Added

- **An ESP32 OTA_ACTIVATE from a host that has not authenticated is refused at
  the gate** (row `ota`, #618). `test/unit/test_esp_wire_auth.c` pins that a
  host holding an OTA_AUTH signature but no operator session gets
  `TFW_ERR_AUTH_REQUIRED` and writes neither a record nor the boot slot, and
  that once it authenticates the activate reaches the stage.
