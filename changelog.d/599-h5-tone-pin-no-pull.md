### Fixed

- **The STM32H5 tone start takes the pull off its pin** (#599).
  `tfw_hal_tone_start` now clears the tone pin's pull-up or pull-down when it
  hands the pin to TIM3, as PWM already does, so a pull left by INPUT_PULLUP
  does not stay on the timer output.
