### Added

- **PWM HAL fault test** (#627). The host mock can fail `tfw_hal_pwm_write`
  and `tfw_hal_pwm_stop` (`tfw_mock_pwm_fails`), and `test_pwm` proves
  PWM_WRITE returns the HAL's refusal of a connect or a disconnect with no
  reply bytes and the compare unit as it was.
