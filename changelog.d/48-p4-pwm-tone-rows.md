### Added

- **The ESP32-P4 PoE board's host test checks that no PWM row names the
  tone's LEDC timer or channel** (#48). The tone takes timer 3 and channel 7
  on the low-speed mode; a PWM row on either, or on any mode but 0, now fails
  `make board-sweep`.
