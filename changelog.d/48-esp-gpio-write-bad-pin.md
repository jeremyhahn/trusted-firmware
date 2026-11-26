### Fixed

- The ESP32 tfw_hal_gpio_write refuses a pin the part lacks as TFW_ERR_BAD_PIN before calling the driver, as MODE and READ already do (Refs #48).
