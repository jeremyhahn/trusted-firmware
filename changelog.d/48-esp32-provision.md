### Added

- **`PROVISION=1` on the ESP32's `hmac-p256` tier** (#48). The jig image
  answers `PROV_GENKEY` by drawing a UDS with the SAR ADC noise source on for
  exactly the draw, burning it as a read- and write-protected `HMAC_UP` eFuse
  key, and returning the P-256 DeviceID point the part's next normal boot
  attests with. A second `PROV_GENKEY` is `PROTECTED_REGION`; `PROV_PUBKEY`
  reads the point back. The other `PROV_*` commands answer `UNSUPPORTED`.
  `make esp32-qemu-test` runs the provisioning boot on the esp32c3.

### Changed

- **`src/caps/provision.c` is the wire alone** (#48). The ATECC608A
  transactions moved to `src/caps/provision_atecc.c`, chosen by the object
  list per crypto tier; the wire is unchanged.
