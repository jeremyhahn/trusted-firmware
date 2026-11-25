### Added

- **A host test that the DHCP client's `tfw_eth_udp_open` refuses a chip select
  with no digital port** (#55): `test_udp_open_refuses_a_chip_select_with_no_port_and_never_writes`
  in `test/unit/test_w5x00_nocs.c`, built for the W5100 and the W5500, expects
  `TFW_ERR_UNSUPPORTED` and no register write, as the start case does.
