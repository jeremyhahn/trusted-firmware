### Added

- **Host tests for the STM32H5 wire's TCP send clip and pbuf chains** (#599).
  `test_h5_wire_connect` now sends more than `tcp_sndbuf` has room for and
  sees the write clipped to that room, and feeds a request split over a
  two-pbuf chain and sees it answered. Test-only: no firmware changes.
