### Added

- **A host test pins the STM32H5 console's full receive ring** (row `stm32h5`,
  #599). With the 64-slot ring holding 63, the next byte is dropped and the 63
  queued stay in order; the ring takes bytes again once one is read and wraps
  past 64; and an overrun beside a byte both takes the byte and clears ORE.
