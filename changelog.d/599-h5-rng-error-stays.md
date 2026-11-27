### Added

- **A host test for the STM32H5 RNG driver's own error paths**
  (`test_h5_netif`, #599). On its own fake registers, the test checks that
  `tfw_h5_rng_init()` fails when a clock (CECS) or seed (SECS) error does not
  clear in the recovery, that `tfw_h5_rng_read()` fails on a clock error with
  no CEIS latch and when DRDY never sets, leaving the caller's word untouched,
  and that both succeed and return the data word once only DRDY is set.
