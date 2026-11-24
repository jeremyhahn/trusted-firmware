### Fixed

- **The i.MX 95 M7's LPUART baud divisor no longer rounds past SBR 8191**
  (#473). When a ratio's quotient was 8191.5 or more, rounding to the nearest
  made 8192 and the ratio was dropped, so `tfw_imx95_lpuart_baud` refused a baud
  the LPUART can make within 3 % (26213600 Hz, 100 baud). It now takes SBR 8191,
  the nearest legal divisor; at the board's 24 MHz no BAUD value changes.
