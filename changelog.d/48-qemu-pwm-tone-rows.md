### Added

- **The esp32-qemu board's PWM rows stay off the tone's LEDC timer and channel** (#48, row `esp32`).
  `test_board_esp32-qemu` checks every PWM row is below channel 5 and timer 3
  in mode 0, the C3's tone channel and timer (the ESP32's tone is in the other
  group), and that the tone pin, GPIO19, has no PWM row.
