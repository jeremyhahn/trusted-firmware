### Added

- **A host test pins the Cortex-M tick's "SysTick not enabled" guards** (#640). `test_mcxn94x_eeprom` now writes an MCXN947 EEPROM byte with SysTick off, as before `tfw_hal_tick_init`: the write must not spin in `tfw_cm_tick_wait_clear` on a CVR that stays 0, nor credit milliseconds in `tfw_cm_tick_catch_up` from DWT cycles SysTick never counted. Deleting either enable test in `src/hal/cortex-m/hal_tick.c` now fails the suite (a wrong `tfw_hal_millis`, or a hang).
