### Changed

- **The i.MX 95 M7 console is LPUART7** (#474, owner ruling 1243). It moves
  from LPUART3 to LPUART7 at `0x42690000` on interrupt 68, on GPIO_IO36/37 at
  ALT2. Before the LPUART reset, `tfw_hal_uart_init` asks the System Manager
  for the pads with an SCMI pin-control request (PINCTRL_SETTINGS_CONFIGURE)
  over MU5, and the shared memory carries the CRC-32 that imx-sm checks. Host
  tests pin the base, the interrupt, the pads and the mux. When the System
  Manager refuses either pad or the exchange fails, `tfw_hal_uart_init`
  returns the routing failure and leaves the LPUART off, and the CRC-32 is
  pinned against imx-sm's `CRC_Crc32` known-answer vectors.
