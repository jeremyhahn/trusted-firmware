### Fixed

- **An ESP32 tone moved to another pin leaves the old pin a plain input** (#48).
  src/hal/esp32/hal_tone.c reset the pin the LEDC channel drove but left it
  marked PWM-driven when the board's PWM had written it, so a capture on it
  looped the output back against what drives it; the start now forgets the
  pin as PWM-driven before the reset, as MODE does.
