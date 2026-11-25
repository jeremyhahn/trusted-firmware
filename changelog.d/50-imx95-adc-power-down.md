### Fixed

- **The i.MX 95 ADC calibration's two power-down waits are pinned by tests**
  (#50). test_imx95_adc's model can now hold the block out of power-down,
  always or once calibrated: init then answers TFW_ERR_TIMEOUT without
  starting a calibration, or without switching AD_CLK to the bus clock, and
  every read answers the same. Test coverage only; the driver is unchanged.
