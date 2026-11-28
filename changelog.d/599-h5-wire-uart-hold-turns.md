### Added

- **Host tests for the STM32H5 Ethernet link's UART hold and wire turns**
  (#599, `src/hal/cortex-m/stm32h5/hal_eth_wire.c`). `test_h5_wire_transport`
  now pins that a UART frame in progress is read from the UART only, a whole
  TCP request waiting until it completes even while the UART's next byte is
  late, and that with requests waiting on both wires the UART and TCP take
  turns one frame each. Test-only: no firmware changes.
