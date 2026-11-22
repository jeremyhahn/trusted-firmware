### Added

- **A host test pins the RP2350's GPIO mode, write and read** (#621).
  `test_rp2350_pwm` now drives every path of
  `src/hal/cortex-m/rp2350/hal_gpio.c`: a pin in bank 1 or past GPIO29 is
  refused by mode, write and read without touching a register; a mode outside
  the three is unsupported; INPUT and INPUT_PULLUP set their pads (the pull-up
  only for the second) and clear the output enable; a write of any non-zero
  level goes to SIO's set register and zero to its clear register; and a read
  returns the pin's own bit of GPIO_IN.
