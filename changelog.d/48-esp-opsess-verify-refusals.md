### Added

- **The ESP32 operator-key check is tested at the unit** (#48, row `esp32-p4`).
  `test_esp_opsess` calls `tfw_crypto_verify_operator` directly: an unreadable
  platform block and a peripheral that refuses the `operator_bind` HMAC are
  `TFW_ERR_CRYPTO` even when the bytes look good, a tag off in its last byte or
  a signature over another digest is refused, and a part with no UDS reads
  neither the block nor the peripheral.
