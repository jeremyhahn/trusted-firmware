### Added

- **The STM32H5 flash writer's top of flash is pinned on the accepted side**
  (#599). test_boot_stm32h5_flash now checks that a write ending on the last
  byte of bank 2, and a zero-length write at the end of flash, are accepted,
  and that a rewrite in bank 2's last sector erases sector 127 through BKSEL,
  so an off-by-one in the range check or a sector field narrower than 7 bits
  fails the suite.
