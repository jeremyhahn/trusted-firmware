### Added

- **A host test pins the MCXN947's early clock and its watchdog** (#620).
  `test_mcxn_hal_soc` runs src/hal/cortex-m/mcxn94x/hal_soc.c, which both
  images link, over a register model of SYSCON, SCG and WWDT0: RAM ECC goes
  off first; the switch to SIRC keeps the other SIRCCSR and RCCR bits and
  waits on SCG's CSR until SIRC shows, not on the clock; the AHB divider
  restarts at 1; every PORT and GPIO bank is clocked and released. Arming
  WWDT0 writes the watchdog mark, sets TC before MOD and feeds once, and every
  feed writes its two words with interrupts masked. `make boot-test` runs it.
