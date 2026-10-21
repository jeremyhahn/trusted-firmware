### Added

- **The ESP32 `hmac-p256` provider runs on the host** (#48, row `esp32-p4`).
  `test_esp_crypto_hmac_p256` links `src/hal/esp32/crypto_hmac_p256.c` over a
  mock HMAC peripheral, eFuse purpose lookup and RNG, with the real handoff.
  The DeviceID point and its ES256 signature for the UDS `0x00..0x1f` are the
  literals `test_deviceid_p256` and the QEMU harness pin. A part with no
  `HMAC_UP` key, a refusing peripheral, or a boot with no CDI for the tier
  does not sign.
- **`docs/multi-arch/esp32-p4-engineering.md`**: the row's requirements, one by
  one, met or partial, with the bench or owner step each partial one waits on.
