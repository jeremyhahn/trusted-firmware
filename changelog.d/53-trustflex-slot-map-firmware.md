### Changed

- **The ATECC608 slot map is board-profile data** (row 53, option (a1) of the
  TrustFLEX slot-map ADR, owner ruling 1243). `TFW_ATECC_SLOT_UDS` and
  `TFW_ATECC_SLOT_IMAGE` default to 1 and 2 in `tfw/config.h`. The atmega328p
  and atmega2560 profiles (TFLXTLS click element) set them to 6 and 15, and
  tfw-boot verifies the image against the profile's slot. `PROV_WRITE_PUBKEY`
  takes the profile's image slot and slot 15. The `tfwsim-prov` jig keeps the
  default map until tfwprov carries the TrustFLEX one.

### Added

- **`PROV_SLOT_LOCK` (0x7D) and `PROV_CONFIG_READ` (0x7E)**, provisioning
  builds only. `PROV_SLOT_LOCK [slot][crc LE]` locks the UDS or image slot
  and nothing else; any other slot is `TFW_ERR_RANGE` and nothing is sent. A
  slot that is already slot-locked is `TFW_ERR_PROTECTED_REGION`, the status
  `PROV_WRITE_PUBKEY` returns for the same state. Before it locks the UDS
  slot, the firmware runs one HMAC keyed by that slot; the output is scrubbed
  and never sent, and if the HMAC fails no Lock is sent.
  `PROV_CONFIG_READ [block]` returns 32-byte config block 0..3. The soft-UDS
  backend answers both `TFW_ERR_UNSUPPORTED`.
