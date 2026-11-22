### Added

- **A test that tfwsim's Ethernet stand-in reads BUSY while its connect is
  still in flight** (#55). A loopback listener with a full accept queue holds
  the stand-in's non-blocking connect pending, and `tfw_eth_state()` must
  report BUSY, not UP, until the socket is writable.
