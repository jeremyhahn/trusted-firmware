### Fixed

- **The public headers' comments match main** (#640). `include/tfw/hal.h`
  names the three device HALs (avr, cortex-m, esp32), what cannot fail,
  the secure elements' I2C waits among `tfw_hal_delay_us`'s callers, the
  handoff placement in `mk/arch/avr.mk` and ESP32's free-heap reading of
  `tfw_hal_free_ram`; `include/tfw/board.h` lists `TFW_ERR_BAD_PIN` for
  `tfw_board_adc_channel` and `tfw_board_pwm`; `include/tfw/status.h` gives
  `TFW_ERR_TIMEOUT` and `TFW_ERR_RESERVED_PIN` their real causes and says no
  build defines `tfw_status_name`; `include/tfw/types.h` names every provider
  that reports `TFW_CRYPTO_SECURE_ELEMENT`. Comments only: no code, value,
  wire format or EEPROM layout changed.
