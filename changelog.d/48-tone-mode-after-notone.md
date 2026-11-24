### Added

- **ESP32 host tests for MODE after NOTONE on the tone pin** (#48, #561).
  MODE on the stopped tone's pin takes it back with no second `ledc_stop`,
  so the RMT capture no longer loops back; MODE on another pin leaves it
  owned.
