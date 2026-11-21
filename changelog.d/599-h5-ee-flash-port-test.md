### Added

- **A host test for the STM32H5 EEPROM-emulation flash port**
  (`test_stm32h5_ee_flash_port`, #599). `ee_flash_port.c` is built unchanged
  with a stand-in for `cm_regs.h`. The test checks that `tfw_h5_ee_flash()`
  returns RM0481's FLASH interface and bank 1's flash, that the wait ends only
  once BSY, WBNE and DBNE are all clear, and that the interrupt restore puts
  back the saved mask after a DSB.
