### Added

- **Host tests for tfw-boot's STM32H5 flash write barrier and interrupt
  hooks** (#599, `boot/hal/cortex-m/stm32h5/boot_flash.c`).
  `test_boot_h5_flash_entry` now pins that the flash write issues one DSB
  after the driver's update and none on a refused address, and that the
  driver's interrupt hooks mask PRIMASK and put back the mask they found.
  Test-only: no firmware changes.
