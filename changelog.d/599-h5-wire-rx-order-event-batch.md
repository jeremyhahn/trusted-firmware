### Added

- **The STM32H5 tfw-wire is tested for its read order between frames and an
  event's end during a TCP reply** (#599). `test/unit/test_h5_eth_wire.c`
  pins that, with TCP current, a waiting UART byte is read before a waiting
  TCP byte between frames, so serial control commands are admitted again and
  the TCP byte is read next; and that an event's end on the UART leaves a
  part-built TCP reply batched, sent as one send when its own frame ends
  (`src/hal/cortex-m/stm32h5/hal_eth_wire.c`). Test only; the driver is
  unchanged.
