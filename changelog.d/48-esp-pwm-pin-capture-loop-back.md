### Fixed

- On the ESP32-P4, a pulse capture on a PWM pin keeps the pad's LEDC output (loopback), as a capture on the tone pin does, instead of silencing it and reading a quiet line (Refs #48).
