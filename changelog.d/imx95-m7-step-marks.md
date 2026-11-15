### Added

- **i.MX 95 M7 bench step marks** (row `imx95`). A bench build flag,
  `STEP_MARKS=1`, makes the reset handler and the console's init send one raw
  byte per stage on LPUART3 (reset entry, .data/.bss set up, before main, pads
  requested, console up), by polling the transmitter with no queue and no
  interrupt, so the M7 console shows how far the image gets when the core's
  memory cannot be read. Release images leave it off and are unchanged. On
  the board, port 2 showed no mark (bench record section 10).
