### Added

- **The nRF54L TWIM test pins init's pins and masks, a stale error and RESUME
  before STOP** (#518). The model records P1 PIN_CNF and TASKS_RESUME: init must
  clear an earlier INTEN and SHORTS and set SCL and SDA to the TWI PIN_CNF, a
  leftover ANACK must be cleared before a good write, and a data NACK must
  RESUME the suspended TWIM before its STOP.
