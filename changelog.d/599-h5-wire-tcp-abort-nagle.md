### Added

- **The STM32H5 tfw-wire TCP abort and send options are tested** (#599).
  `test/unit/test_h5_wire_connect.c` aborts the open connection: one RST, none
  for a second abort, callbacks unhooked, no window returned and no bytes sent
  to the gone pcb, and the redial waits out its backoff; and checks the
  connection has Nagle off and returns exactly the window the link read
  (`src/hal/cortex-m/stm32h5/hal_eth_wire_tcp.c`). Test only; the driver is
  unchanged.
