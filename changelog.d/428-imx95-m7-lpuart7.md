### Changed

- **i.MX95 M7 console moves to LPUART7** (row `imx95`). On the
  FRDM-IMX95-PRO the M7 console port is LPUART7 on GPIO_IO36/37, not
  LPUART3 on GPIO_IO14/15 (bench record section 14). The byte link is now
  LPUART7 at `0x42690000` on M7 interrupt 68, and the console's SCMI pad
  request asks the System Manager for SM pin ids 40 and 41 at ALT2. The
  host register model, `test_imx95_hal` and `test_imx95_console_pins`
  follow; the design page names LPUART7.
