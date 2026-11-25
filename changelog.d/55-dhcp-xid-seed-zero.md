### Added

- **DHCP test that a seed folding to zero still gives a non-zero xid** (#55):
  `test_a_seed_that_folds_to_zero_still_draws_a_nonzero_xid` in
  `test/unit/test_dhcp.c` starts the client with a MAC and boot time whose
  xorshift seed folds to 0, then checks the first DHCPDISCOVER carries a
  non-zero xid. Before this change a client that dropped the zero-seed guard
  (`src/net/dhcp.c:538`), and so sent xid 0 with no retransmission jitter,
  left every DHCP suite green; it now fails on both chips.
