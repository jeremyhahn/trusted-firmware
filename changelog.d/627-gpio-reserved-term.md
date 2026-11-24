### Added

- **`test_pin_mode_and_digital_write_on_a_reserved_pin_are_refused_and_it_stays` in `test/unit/test_caps_reserved.c`** (#627): PIN_MODE OUTPUT and DIGITAL_WRITE 1 on the mock table's RESERVED D3 return `TFW_ERR_RESERVED_PIN`, the pin stays an input at level 0 and nothing is written back, so dropping the RESERVED term from `resolve_writable` in `src/caps/gpio.c` (which would let the ATtiny85's /RESET leg be driven) now fails the host suite.
