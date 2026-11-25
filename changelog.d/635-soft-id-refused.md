### Fixed

- **The soft P-256 identity returns `TFW_ERR_CRYPTO` when micro-ecc refuses**
  (#635). A refused sign or DeviceID derivation was reported as `TFW_OK` with
  64 zero bytes; now it is status 14, no algorithm is set, and the seed is
  still zeroed.
