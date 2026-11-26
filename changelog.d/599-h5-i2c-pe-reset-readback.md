### Fixed

- **The STM32H5 I2C reset holds PE low as RM0481 asks** (#599).
  `tfw_hal_i2c_init` on a running bus and the reset after a STOP that never
  completes now write PE=0, read it back clear, and only then set PE,
  RM0481's software-reset sequence, instead of two back-to-back stores that
  need not keep PE low for the three APB cycles a reset takes.
