### Added

- **The STM32H5 flash writer's erase decision and sector select are pinned**
  (#599). test_boot_stm32h5_flash now checks that a write repeating an
  already-programmed quad-word programs only the new quad-word with no erase,
  and that a rewrite erases its own sector when NSCR still holds another
  sector's SNB and BKSEL, so an erase on any programmed quad-word or a sector
  field not cleared first fails the suite
  (`boot/hal/cortex-m/stm32h5/h5_flash.c`). Test only; the driver is
  unchanged.
