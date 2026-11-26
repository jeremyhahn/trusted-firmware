### Added

- **A host test drives the STM32H5 console's transmit ring past its wrap**
  (row `stm32h5`, #599). Seventy bytes go through the 64-slot ring one at a
  time and each is written to TDR in order after the head and tail wrap past
  slot 63; with the ring empty again the service routine turns TXEIE off.
