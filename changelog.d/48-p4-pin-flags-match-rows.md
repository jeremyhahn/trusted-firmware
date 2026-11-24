### Added

- **The ESP32-P4 PoE board's host test checks every pin's flags byte against
  its row** (#48). Each pin must read DIGITAL, plus ADC and PWM exactly where
  its row resolves an ADC channel or a PWM unit; a PWM or ADC flag on a plain
  pin, or a pin without DIGITAL, now fails `make board-sweep`.
