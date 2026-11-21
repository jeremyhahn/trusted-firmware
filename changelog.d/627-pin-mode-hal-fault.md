### Added

- **A test pins PIN_MODE's HAL refusal** (#627). When `tfw_hal_gpio_mode`
  refuses, PIN_MODE returns the HAL's status with no reply bytes and leaves
  the pin's direction and pull-up as they were; a handler that dropped the
  status now fails `test_caps_faults`.
