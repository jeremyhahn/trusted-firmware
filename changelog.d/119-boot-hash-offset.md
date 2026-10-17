### Fixed

- **`tfw_boot_hash` keeps a `uint16_t` offset** (#119). On the element build
  (`boot/measure/measure_atecc608a.c`) the offset was a `uint8_t` against a
  `uint16_t` length and wrapped from 192 to 0 for any input over 256 bytes,
  hashing the first block again. Unreachable today: the callers pass 32 and 48
  bytes. The commands sent for 256 bytes or fewer are unchanged. The software
  element model now logs every command it accepts, and
  `test/unit/boot/test_boot_element.c` pins the Start/Update/End sequence and a
  literal digest for 0, 32, 48, 64, 65 and 300 bytes. No wire format, status
  code, vector or EEPROM layout changed.
