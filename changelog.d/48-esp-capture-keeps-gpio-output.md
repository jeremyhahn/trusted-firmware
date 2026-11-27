### Fixed

- An ESP32 pulse capture on a pin MODE made an output loops the pin back, as it does for the tone and PWM pins, so the pin keeps driving its level during and after the capture. A tone moved off such a pin resets it to an input, and a later capture there is input-only again (Refs #48).
