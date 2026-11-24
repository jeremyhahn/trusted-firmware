### Security

- **The ESP32 hmac-p256 tier drains a CDI handed off for another tier** (#23).
  `tfw_crypto_init` now takes and wipes it, as every other provider does, so
  the layer-0 secret no longer stays in RAM until reset.
