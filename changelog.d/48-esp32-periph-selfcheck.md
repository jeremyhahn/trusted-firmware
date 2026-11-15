### Added

- **A bench self-check of the ESP32 pin peripherals** (#48, row `esp32-p4`).
  A build that sets `CONFIG_TFW_PERIPH_SELFCHECK` (default off) runs
  `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_pulse_rmt.c` and
  `hal_eeprom.c` on unconnected pins once at boot, reads the C6 enable, and
  prints one `tfw-selfcheck:` line per check; `test_esp_sys` pins that it runs
  only after a good board init. The C6 enable is read at the pad with its input
  buffer on. The EEPROM test saves the whole 4 KB flash sector it writes in
  first, writes all of it back after any failed step (an erase that is not
  followed by a good program included), compares all of it, and reports the
  restore on its own line (`test_esp_selfcheck`, over the real `hal_eeprom.c`).
