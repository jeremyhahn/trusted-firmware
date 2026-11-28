### Added

- **The STM32H5 EEPROM flash port's interrupt restore is tested for a masked
  caller** (#599). `test/unit/test_stm32h5_ee_flash_port.c` saves and restores
  from an already-masked caller, as tfw-boot does: the mask stays on and the
  DSB still comes ahead of the restore; and a saved PRIMASK word is handed back
  whole, not cut to bit 0 (`src/hal/cortex-m/stm32h5/ee_flash_port.c`). Test
  only; the driver is unchanged.
