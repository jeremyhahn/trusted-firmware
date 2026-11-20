### Added

- **A bench self-check of the ESP32 pin peripherals** (#48, row `esp32-p4`).
  A build that sets `CONFIG_TFW_PERIPH_SELFCHECK` (default off) runs
  `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_pulse_rmt.c`,
  `hal_eeprom.c` and `hal_bus.c` on unconnected pins once at boot, reads the
  C6 enable, and prints one `tfw-selfcheck:` line per check; `test_esp_sys`
  pins that it runs only after a good board init. The buses are checked
  against no device: an I2C probe of an empty address must NACK, a two-byte
  SPI transfer must complete, and a 1-Wire reset on a free pin must see no
  presence and read 0xff. The C6 enable is read at the pad with its input
  buffer on. The EEPROM test saves the whole 4 KB flash sector it writes in
  first, writes all of it back after any failed step (an erase that is not
  followed by a good program included), compares all of it, and reports the
  restore on its own line (`test_esp_selfcheck`, over the real `hal_eeprom.c`).
  On the board so far: GPIO, ADC, PWM, tone, pulse, the 16-byte EEPROM check
  and the C6 enable. The bus lines and the whole-sector restore are pinned on
  the host only and have had no bench run.
