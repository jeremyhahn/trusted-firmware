### Added

- STM32H5 PWM HAL host tests: a bad `tfw_pwmref_t` (wrong unit, channel 0 or 5, port or bit out of range) is refused with `TFW_ERR_BAD_PIN` by write and stop, touching no timer, clock or GPIO register, and a refused stop leaves a live channel running (Refs #599).
