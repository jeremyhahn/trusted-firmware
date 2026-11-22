### Added

- **The RP2350 PERIPH=0 converter test pins set_ref and every channel** (#621).
  `test_rp2350_adc_default` now checks that `tfw_hal_adc_set_ref` accepts VCC
  and refuses the other references without touching a register, and that reads
  on channels 1, 2 and 255 return `TFW_ERR_RANGE`, leave the output unwritten
  and the register model as it was at reset.
