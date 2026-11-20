- hw-test: the `Caps`, `EEPROM`, `I2CConfig` and `AttestRefusedUnprovisioned`
  rows expect what a `CRYPTO=none` board answers (no algorithms, ATTEST
  UNSUPPORTED, the reserved read and the I2C clock answered), and a host test
  runs each against a none-tier and a soft-tier peer (#549).
