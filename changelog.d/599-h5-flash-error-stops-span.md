### Added

- **Host tests for an STM32H5 boot flash error in a write that spans quad-words
  or sectors** (#599, `boot/hal/cortex-m/stm32h5/h5_flash.c`).
  `test_boot_stm32h5_flash` now pins that a program or erase error stops the
  write at the failing operation: a programming error on the first of two
  quad-words programs neither, an erase error on the first of two sectors
  erases neither, and a programming error after a sector erase programs
  nothing more in that sector. Test-only: no firmware changes.
