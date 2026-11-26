### Fixed

- **The STM32H5 GPIO mode sets its pin push-pull** (#599).
  `tfw_hal_gpio_mode` clears the pin's OTYPER bit, so a pin the I2C driver
  left open-drain drives high again after MODE OUTPUT; the bank's other pins
  keep theirs, and a refused mode still writes nothing.
