### Fixed

- **The STM32H5 TCP wire's receive ring is checked at build time** (#599).
  Its size must be a power of two (the ring mask needs it) and the ring must
  hold a full lwIP receive window: it holds one byte less than its size, which
  `hal_eth_wire.h` now says. `test_h5_wire_connect` delivers a full `TCP_WND`
  window in a two-pbuf chain and sees every byte taken and the request at its
  end answered.
