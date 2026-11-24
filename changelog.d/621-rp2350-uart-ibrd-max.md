### Fixed

- **The RP2350's PL011 divisor helper no longer hands out IBRD 65535 with a
  nonzero FBRD** (#621). `tfw_rp_uart_divisors()` refused only an integer part
  above 65535, so a divisor between 65535 + 1/64 and 65535 + 63/64 came back
  as IBRD 65535 with FBRD 1..63, which [RP-DS] §12.1.7.1's baud routine never
  programs; it is now refused. No integer baud from the board's 12 MHz
  clk_peri reaches that band.
