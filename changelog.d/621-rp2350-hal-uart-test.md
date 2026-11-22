### Added

- **A host test for the RP2350 application UART** (row #621 follow-up).
  `test_rp2350_uart` runs `src/hal/cortex-m/rp2350/hal_uart.c` unchanged on the
  Pico 2 W's profile against its own register file and PL011 receive FIFO. A
  baud the divisors cannot make returns `TFW_ERR_RANGE` and writes nothing.
  Init pulses UART0's reset and writes the pads, the UART function and
  IBRD/FBRD. `tfw_cm_irq33` fills the 64-byte receive ring, drops bytes once it
  holds 63, and wraps both indices by the mask.
