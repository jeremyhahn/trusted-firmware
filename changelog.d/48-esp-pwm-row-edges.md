### Added

- **The ESP32 PWM output's out-of-range rows are tested at the unit** (#48,
  row `esp32-p4`). `test_esp_pwm_mutate` holds that a row whose LEDC speed
  mode is outside the timer record configures its timer on every write
  without indexing the record, and that the driven-pin bitmap marks, reports
  and releases pin 63, its last bit, and marks nothing for pins 64 and 255.
