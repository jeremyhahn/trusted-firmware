### Fixed

- **The Pin row expects the led flag only where the board has an LED**
  (#560, #574). On the ESP32-P4, which has no LED on the header, the row's stand-in
  pin 11 (GPIO32) was expected to carry the led flag and the row failed on
  that expectation. Each board's pin facts now say whether it has an LED; the
  AVR and MPS2 boards keep their led expectation, and on the P4 the row
  checks that the pin is digital with no led flag. The board page marks the
  row's facts corrected; its hardware re-run waits on #561.
