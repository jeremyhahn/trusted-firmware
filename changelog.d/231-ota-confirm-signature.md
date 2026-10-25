### Fixed

- **tfw-boot verifies `OTA_CONFIRM`'s signature** (#231). `OTA_CONFIRM` used
  to drop the row key's signature `OTA_AUTH` held, so tfw-boot settled a trial
  on a confirm request's magic, CRC and measurement alone. The request is now
  75 bytes at EEPROM `0x0EA`–`0x134` on the 2560:
  `[magic 0xC9:1][measurement:8][signature:64][crc:2]`, with the CRC over the
  first 73 bytes. Before it settles, tfw-boot rebuilds the confirm
  Sig_structure from the primary's trailer, its measurement and
  `image_version`, and checks the stored signature with the key and verifier
  that check the image. A wrong, zeroed, erased or truncated signature, or an
  11-byte request in the old layout, confirms nothing, and the trial rolls
  back.

### Changed

- **tfw-boot's EEPROM ends at `0x140` on every board** (#125). Host
  `EEPROM_WRITE` refuses `[0x000, 0x140)`, and an Ethernet board's network
  block moves from `0x0C0` to `0x140`–`0x15F`. A device upgraded from the old
  layout no longer reads its network block at `0x0C0`, so it has to be
  re-provisioned (`tfwctl netcfg set`).
