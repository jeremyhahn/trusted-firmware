### Added

- **The network-block decode test pins the flags byte and one-octet peers**
  (#55). `test/unit/test_netcfg_mutate.c` now checks that `tfw_netcfg_t.flags`
  is `raw[5]` whole in every decoded block, so a lost bit such as
  `TFW_NETCFG_DHCP` fails the bit-flip, version/reserved and hostile-field
  tests. The edge-address test adds a peer of 192.0.0.0 and of 0.0.0.20, each
  of which must decode as a provisioned peer. `src/net/netcfg.c` is unchanged;
  no wire format, status code or EEPROM layout changed.
