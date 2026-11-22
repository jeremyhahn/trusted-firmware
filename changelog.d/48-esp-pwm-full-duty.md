### Fixed

- **ESP32 PWM_WRITE duty 255 now holds the pin high** (#48), as on the AVR
  and every other port, instead of 255/256 of the 8-bit LEDC period (low for
  about 3.9 us each 1 ms).
