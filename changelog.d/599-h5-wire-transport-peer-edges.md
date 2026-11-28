### Added

- **The STM32H5 tfw-wire link's peer edges are tested through the real
  transport** (#599). `test/unit/test_h5_wire_transport.c` opens and drops a
  TCP connection while a UART frame is half read: the frame completes and is
  answered on the UART; and sends a request then FIN: the request is answered
  on TCP and the connection closes on the next poll
  (`src/hal/cortex-m/stm32h5/hal_eth_wire.c`). Test only; the driver is
  unchanged.
