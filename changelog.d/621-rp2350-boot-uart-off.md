### Added

- **The RP2350 boot UART test proves the link goes silent** (#621).
  `test_boot_rp2350_uart` now covers both places tfw-boot drops the UART: a
  second `tfw_boot_uart_init` with a baud out of reach puts a running UART0
  back in reset, and after `tfw_cm_boot_uart_off` hands the pins back; in both
  cases tx writes no byte to UARTDR and rx reads none from the FIFO.
