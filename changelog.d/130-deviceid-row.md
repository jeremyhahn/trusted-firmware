### Changed

- **The `dice-attestation` rows say which soft root signs what** (#130). The
  P-256 DeviceID row is met on the 2560's soft root (`SOFT_ID=p256`, its seed
  handed over in handoff v4) and the ESP32-P4, unmet on the 328P and 32U4,
  which keep the HMAC identity; "the algorithm names the root" holds only
  there, since the 2560's soft root signs ES256 as the element does.
