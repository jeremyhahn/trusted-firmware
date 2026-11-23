### Added

- **The ESP32-P4 operator session** (row #606, carries #563). The `hmac-p256`
  image on `esp32p4-poe` links the auth session (issue #4), so OTAActivate and
  OTAConfirm need an operator signature. The operator key in the platform block
  is bound to the eFuse `HMAC_UP` UDS: `verify_operator` recomputes
  `HMAC-SHA256(UDS, "tfw-opsess-v1" || pubkey)` through the HMAC peripheral and
  compares it with the tag at platform-block offset 132 before it checks the
  signature. Software never reads the key block, and no new eFuse key is
  burned. Until the tag is provisioned (the next slice), a P4 image refuses
  every operator session. The QEMU C3 keeps the open gate.
