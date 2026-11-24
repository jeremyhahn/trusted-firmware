### Added

- **The nRF54L15 UART host test covers a full receive ring.** With 63 bytes
  unread, the 64th is dropped, the receive is still restarted, and the ring
  takes bytes again once it is read (test/unit/test_nrf54l_uart.c).
