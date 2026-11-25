### Fixed

- **ESP32 GPIO read refuses a pin the part lacks** (#48). `hal_gpio.c`
  returns `TFW_ERR_BAD_PIN` without a driver call and leaves the level
  untouched, as the mode and every other HAL do; before, it read the pin and
  returned `TFW_OK`.
