### Added

- **A host test pins the RP2350 flash EEPROM's zero-length user write** (#621).
  `test_rp2350_eeprom` writes and reads zero bytes at the user span's start
  and at `TFW_EEPROM_SIZE`: each is `TFW_OK`, nothing is erased or copied and
  the platform sector is kept, where without the guard in
  `src/hal/cortex-m/rp2350/hal_eeprom.c` the empty write at the size would
  enter the masked sector write at the platform block's first byte.
