### Fixed

- **An EEPROM write on the FRDM-MCXN947 no longer loses SysTick time** (#426).
  The sector update masks interrupts for longer than a millisecond, and
  SysTick keeps only one pending interrupt across its reloads. The EEPROM HAL
  now records SysTick and the DWT cycle count when it masks, takes any tick
  that comes due while it masks, and before it unmasks waits until SysTick is
  clear of a reload and adds the reloads the pending interrupt does not count
  (`tfw_cm_tick_catch_up`, `tfw_cm_tick_wait_clear`). `millis` and scheduler
  deadlines keep the time a write takes. Host-tested over a SysTick/DWT model
  that expires SysTick at every point of the lock and unlock
  (`test/unit/test_mcxn94x_eeprom.c`).
