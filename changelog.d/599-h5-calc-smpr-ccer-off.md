### Added

- **Host tests for the STM32H5 ADC sample-time field and PWM CCER off branch**
  (#599, `src/hal/cortex-m/stm32h5/h5_calc.h`). `test_stm32h5_calc_spins` now
  pins that tfw_h5_adc_smpr masks a sample-time code to its three bits (0xF
  is 7, 8 is 0, nothing spills into the next channel) and puts channel 19 at
  SMPR2's top field, bits 29:27, and that tfw_h5_pwm_ccer with the channel
  off clears that channel's CCxE and CCxP and keeps every other bit.
  Test-only: no firmware changes.
