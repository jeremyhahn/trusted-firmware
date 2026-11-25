### Added

- **DHCP test that a discarded reply leaves the next one queued** (#55):
  `test_a_discarded_reply_leaves_the_next_one_queued_behind_it` in
  `test/unit/test_dhcp.c` puts an offer the client passes over and a good
  offer on the socket together before one poll, then checks the good offer is
  still queued and is taken on the next poll. Before this change a client that
  dropped everything queued behind the datagram it read
  (`src/net/dhcp.c:436`) left every DHCP suite green; it now fails on both
  chips.
