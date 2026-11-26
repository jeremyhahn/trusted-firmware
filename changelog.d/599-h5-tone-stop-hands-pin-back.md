### Fixed

- **The STM32H5 tone stop hands its pin back** (#599).
  `tfw_hal_tone_stop` now returns D3 (PB5) to a GPIO output driven low, as the
  PWM stop does, so a digital write on the pin after a tone stops drives it
  again instead of being ignored.
