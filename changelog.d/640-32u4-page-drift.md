### Fixed

- **The ATmega32U4 board page matches its board files** (#640).
  `docs/boards/atmega32u4.md` now says flashing tfw-boot replaces Caterina,
  so recovery needs an ISP, names D17 (PB0 SS) among the pins `TFW_PIN_ETH`
  flags, and records the `ETH=1` image as measured over the 14 336-byte
  budget (issue #315) instead of not measured.
