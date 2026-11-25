### Added

- **`tfw_crc16_from` is tested across a split and from a non-initial seed** (#629).
  A CRC over the first k bytes of `123456789`, carried into `tfw_crc16_from`
  for the rest, equals 0x29B1 at every k from 0 to 9, the carry tfw-boot's
  handoff CRC depends on; and from seed 0x1D0F it folds to 0xE5CC, the same
  as `tfw_crc16_update` byte by byte, not `tfw_crc16` of the same bytes.
