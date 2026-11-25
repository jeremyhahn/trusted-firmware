### Added

- **The bench self-check's host test holds the pin each check uses** (#48,
  row `esp32-p4`). `test_esp_selfcheck` answers the board lookups with the PoE
  board's own pin rows and asserts that GPIO, tone, pulse, SPI chip select and
  1-Wire drive the free header pins `hal_selfcheck.c` names, none reserved or
  in use, that the ADC check reads its pin's own channel, and that PWM runs
  LEDC channel 0.
