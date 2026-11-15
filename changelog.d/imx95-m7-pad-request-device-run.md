### Changed

- **i.MX 95 M7 pad request run on the board** (row `imx95`). The bench
  record holds the device run of the SCMI pad-request slice: the System
  Manager now muxes the M7 console pads (GPIO_IO14/15) to LPUART3 at boot,
  but the M7 console still sends no frame, so the transmit path is next.
