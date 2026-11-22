### Added

- **Host tests for the Ethernet link's source port and redial deadline**
  (#55): `test/unit/test_link_eth_mutate.c` checks that each attempt dials
  from a port in the ephemeral range, masked so it never wraps to 0, and
  that a redial uses a different port from the last attempt. It also checks
  that a redial whose deadline falls past the 32-bit wrap of
  `tfw_hal_millis()` waits the full 1000 ms.
