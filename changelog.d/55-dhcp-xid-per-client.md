### Added

- `test_two_clients_started_in_the_same_millisecond_ask_under_different_xids` in `test/unit/test_dhcp_robust.c`: two clients started in the same millisecond with different MACs send their DHCPDISCOVER under different xids, as `src/net/dhcp.c:536` mixes the MAC into the seed and `src/net/dhcp.c:541` draws the xid at start (Refs #55).
