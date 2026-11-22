### Fixed

- **The RP2350 converter's init brings up a PLL_USB whose post dividers are off** (#621).
  `tfw_hal_adc_init()` left a locked PLL_USB at the 48 MHz setting running
  even with PWR.POSTDIVPD set, so clk_adc stayed at 0 Hz and no read
  converted; it now keeps the PLL only with PD, VCOPD and POSTDIVPD all clear.
