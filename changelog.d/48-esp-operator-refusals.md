### Added

- **Host tests for the ESP32-P4 operator check's two refusals** (#48):
  `test_esp_crypto_hmac_p256` now shows that `tfw_crypto_verify_operator`
  refuses the operator key when the platform block cannot be read (no MAC is
  computed) and when the peripheral refuses the bind HMAC, even if it wrote
  the true tag. A third test shows that a bound key and its signature verify.
