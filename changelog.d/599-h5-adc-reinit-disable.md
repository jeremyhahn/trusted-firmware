### Fixed

- **The STM32H5 ADC init disables a converter it finds enabled first**
  (`test_stm32h5_adc`, #599). An init run over an enabled ADC changed the
  ADC clock, dropped the regulator and set ADCAL with ADEN still set, all
  forbidden by RM0481, so the calibration never ran and init answered
  `TFW_OK` on an uncalibrated converter. Init now stops a running conversion
  (ADSTP), disables the ADC (ADDIS, ADEN reading 0) before it touches the
  clock, the regulator or the calibration, as ST's
  `HAL_ADCEx_Calibration_Start` does, and answers `TFW_ERR_TIMEOUT` if the
  ADC will not disable.
