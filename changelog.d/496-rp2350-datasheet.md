### Changed

- **RP2350 ADC and PWM register literals checked against the datasheet** (row
  `rp2350`). Every `TODO(datasheet)` row in
  `src/hal/cortex-m/rp2350/soc_regs.h` (the ADC, its reset bit, clk_adc and the
  PLL_USB that feeds it) and the PWM register numbers now match [RP-DS]
  release 8, each beside its table. None differed, so no literal changed. The
  ADC reset bit's citation moved from table 566 (the register list) to 567.
