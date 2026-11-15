### Added

- **The i.MX 95 M7 asks the System Manager for its console pads** (SPEC row
  `imx95`). Before LPUART3 starts, `src/hal/cortex-m/imx95/console_pins.c`
  sends SCMI PINCTRL_SETTINGS_CONFIGURE for GPIO_IO14 and GPIO_IO15 with
  mux ALT1, over the M7's channel on MU5. The SMT transport now writes and
  checks the CRC-32 that the SM's M7 channel requires. The pads'
  configuration and RX's input select are not sent yet; they are still to
  be confirmed. `test/unit/test_imx95_console_pins.c` runs the request
  against the register model. It checks the layout, TX before RX, the CRC
  both ways, and that a refusal stops the sequence.
