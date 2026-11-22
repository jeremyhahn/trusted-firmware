### Added

- **The FRDM-MCXN947's GPIO test covers the input modes on port 1 and a pull-up
  over another function** (#628). `test/unit/test_mcxn94x_gpio.c` ran both
  input modes only on port 0 and the pull-up only over a cleared PCR, so a
  pull-up that kept a pin's peripheral function, or an input mode that wrote
  PORT0/GPIO0 for a port 1 pin, passed. Two tests now pin the pull-up over a
  PCR left at function 4 and both input modes on P1_2.
