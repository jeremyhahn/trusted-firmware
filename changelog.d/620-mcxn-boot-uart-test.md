### Added

- **A host test pins tfw-boot's MCXN947 UART bring-up** (#620).
  `test_mcxn_boot_uart` runs boot/hal/cortex-m/mcxn94x/boot_uart.c over a
  register model of SYSCON and PORT: init clocks and releases PORT1 and
  LP_FLEXCOMM4, selects the 12 MHz FRO and runs its divider undivided before
  the LPUART starts (the model seeds the divider halted, so a divider left
  unset or set only after the start fails); a start
  that fails muxes no pin and leaves rx and tx doing nothing; a good init muxes
  both pins, RX with its input buffer; and `tfw_cm_boot_uart_off` stops the
  UART and returns the pins. `make boot-test` and `make host-test` run it.
