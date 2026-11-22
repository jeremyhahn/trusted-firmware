### Added

- **A host test for the STM32H5 netif's bound line** (`test_h5_netif`, #599).
  The suite's console stub now records what is said: after DHCP binds the
  offered address, the test checks that exactly one bound line was said and
  that it reads "eth: dhcp bound 192.0.2.10\r\n", and that five more seconds
  of polls say no second one.
