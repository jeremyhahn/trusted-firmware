### Added

- **The nRF54L15 tone test covers a refused start while a tone plays** (#628).
  `test/unit/test_nrf54l_tone.c` starts a 1 kHz tone, then asks for three pins
  the HAL refuses with `TFW_ERR_BAD_PIN`, and checks that nothing was written
  and that a later stop still stops the tone. No source, wire format, status
  code or EEPROM layout changed.
