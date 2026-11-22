### Added

- **A host test that tfw-boot's nRF54L15 UART stays off for a baud rate the
  vendor table refuses** (#640). With the UARTE as the application left it
  (enabled, every interrupt on), `tfw_boot_uart_init` at 0, 1 or 115201 baud
  switches it off, arms no receive, and receive and send do nothing.
