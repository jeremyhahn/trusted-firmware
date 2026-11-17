### Added

- **A hw-test row for tone on the board's own tone pin** (#548).
  `TestFirmwareTone` starts a 1 kHz tone on the board's `TFW_TONE_PIN` (11 on
  the ATmega328P, 10 on the ATmega2560, GPIO6 on the ESP32-P4), captures 30
  edges (the capture limit at a 64-byte payload is 31) on the same pin with `pulse-capture`, wants the frequency within 5%,
  then stops the tone and wants the line quiet. On tfwsim it checks the start
  and stop round-trip. The CLI tone checks take the simulator's tone pin from
  the board pin facts instead of a literal 11.
