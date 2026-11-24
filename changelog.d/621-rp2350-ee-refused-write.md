### Added

- **A host test pins the RP2350 flash EEPROM's refused sector write** (#621).
  `test_rp2350_eeprom` has the NOR model refuse the next sector write as the
  ROM refuses one it lacks a function for: the user write returns
  `TFW_ERR_UNSUPPORTED`, not `TFW_ERR_INTERNAL`, with interrupts unmasked
  again, nothing erased, the sector's bytes and the platform sector kept, and
  the same write then lands, so the early return in
  `src/hal/cortex-m/rp2350/hal_eeprom.c` and its place after the interrupt
  restore are both held.
