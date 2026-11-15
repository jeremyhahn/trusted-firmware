### Fixed

- **i.MX 95 M7 console no longer waits on the interrupt alone** (row
  `imx95`). LPUART3's transmitter is now fed from the main loop's polls as
  well as from NVIC line 64, so a line that never fires slows the console
  instead of silencing it. The console's 115200 divisor at 24 MHz is pinned
  by a host test. The Cortex-M link no longer warns "dot moved backwards
  before '.faultframe'". The bench record lists what the next device run
  must show on port 2.
