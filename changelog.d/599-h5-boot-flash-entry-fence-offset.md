### Added

- **Host tests for tfw-boot's STM32H5 flash write on a refused update and
  past the first bank** (#599, `boot/hal/cortex-m/stm32h5/boot_flash.c`).
  `test_boot_h5_flash_entry` now pins that a write the driver refuses is
  still fenced by one DSB after the update and returns the driver's status,
  and that an address in bank 2 or above the flash reaches the driver with
  its full offset, the range check left to the driver.
  Test-only: no firmware changes.
