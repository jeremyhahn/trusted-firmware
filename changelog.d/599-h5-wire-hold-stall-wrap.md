### Added

- **The STM32H5 tfw-wire is tested holding a UART frame and across the
  millisecond wrap** (#599). `test/unit/test_h5_eth_wire.c` reads a UART
  frame while a TCP byte is queued: the TCP byte waits for the frame's end,
  and its reply goes back on TCP; and a reply stall that begins just before
  the millisecond counter wraps resets the peer after
  TFW_H5_WIRE_TX_STALL_MS, not before
  (`src/hal/cortex-m/stm32h5/hal_eth_wire.c`). Test only; the driver is
  unchanged.
