### Added

- **Host tests for the STM32H5 wire's TCP refusal paths** (#640).
  `test_h5_wire_connect` now drives every refusal and bound branch of
  `hal_eth_wire_tcp.c`: lwIP out of pcbs, a connect refused at once, the
  redial wait held at its 16 s cap, a connected callback with an error or for
  another pcb, data on another pcb, an error with no data, a full send window,
  a refused write and a close with no memory for the FIN. Test-only: no
  firmware changes.
