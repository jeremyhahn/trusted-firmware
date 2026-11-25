### Fixed

- **An ESP32 PWM write no longer restarts the other PWM pins on its timer** (#23).
  `tfw_hal_pwm_write` (`src/hal/esp32/hal_pwm.c`) configures a (speed mode,
  timer) once, on its first good write, and from then on only the channel;
  before, every write reconfigured (and, in ESP-IDF's `ledc_timer_config`,
  reset) the LEDC timer that every other PWM pin on it runs from, and both
  ESP32 boards put all their PWM pins on one timer. A refused timer is tried
  again on the next write. No wire format, status code or call site changed.
