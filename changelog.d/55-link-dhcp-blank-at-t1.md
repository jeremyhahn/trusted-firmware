### Added

- **A test for the network block that will not load at T1** (row #55).
  `test_a_block_that_will_not_load_at_t1_drops_the_connection_and_asks_nothing`
  in `test/unit/test_link_dhcp.c` covers the failed `tfw_netcfg_load()` at
  `src/proto/link_eth.c:89`: the link drops the connection, sends no
  DHCPREQUEST and dials nothing while the block is bad, then redials from the
  lease and renews once the block is back.
