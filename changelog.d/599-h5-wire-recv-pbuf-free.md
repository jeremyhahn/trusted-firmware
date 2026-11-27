### Added

- **Host tests for the STM32H5 wire TCP receive callback's pbuf frees and
  stale pcbs** (#599, `src/hal/cortex-m/stm32h5/hal_eth_wire_tcp.c`).
  `test_h5_wire_connect` now pins that data on the connection, data on
  another pcb and data with an error each free their pbuf exactly once, and
  that a FIN or an error on a stale pcb closes only that pcb while the open
  connection keeps answering. Test-only: no firmware changes.
