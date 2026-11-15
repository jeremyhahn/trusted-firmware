### Added

- **A bench self-check of the ESP32 pin peripherals** (#48, row `esp32-p4`).
  A build that sets `CONFIG_TFW_PERIPH_SELFCHECK` (default off) runs
  `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_pulse_rmt.c` and
  `hal_eeprom.c` on unconnected pins once at boot, reads the C6 enable, and
  prints one `tfw-selfcheck:` line per check; `test_esp_sys` pins that it runs
  only after a good board init. The C6 enable is read at the pad with its input
  buffer on, and the EEPROM's old bytes are written back after any failed
  step and reported on their own line (`test_esp_selfcheck`).
