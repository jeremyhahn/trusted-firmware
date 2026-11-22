### Added

- **A host test pins the RP2350 flash EEPROM's stopped-SysTick case** (#621).
  `test_rp2350_eeprom` runs a 45 ms sector write with SysTick off over the
  NOR model, whose CSR then reads without ENABLE and whose CVR holds: the
  write lands and the millisecond clock is credited nothing, where without
  the guard in `src/hal/cortex-m/rp2350/hal_eeprom.c` it would gain 44 ms it
  never lost.
