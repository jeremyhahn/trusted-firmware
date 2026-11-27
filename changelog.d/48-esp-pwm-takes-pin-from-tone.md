### Fixed

- The ESP32 tfw_hal_pwm_write takes its pin from the tone once LEDC has accepted the channel, as MODE does, so a later tone on another pin no longer resets the pad and stops the PWM the write started (Refs #48).
