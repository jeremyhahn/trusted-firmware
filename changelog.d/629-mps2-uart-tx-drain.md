### Changed

- **The MPS2 UART TX interrupt is tested past its first byte** (#629).
  `test_tx_irqs_drain_the_ring_in_order_then_go_idle` queues three bytes and
  runs three TX interrupts: the second and third bytes go out in order, the
  last interrupt writes nothing, and the link is idle. It holds
  src/hal/cortex-m/mps2/hal_uart.c line 108, the `tx_tail` advance in
  `uart0_service`; without it every TX interrupt resent the same byte.
