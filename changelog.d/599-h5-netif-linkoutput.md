### Added

- **A host test pins the STM32H5 netif's link output** (`test_h5_netif`, #599):
  an oversized frame is refused unsent, a pbuf chain goes out as one frame,
  and a full TX ring ends in an error instead of a hang.
