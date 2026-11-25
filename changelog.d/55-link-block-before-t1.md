### Fixed

- **The link suite pins that the network block is read only when a lease
  falls due** (#55).
  `test_a_block_that_will_not_load_before_t1_leaves_the_connection_up` in
  `test/unit/test_link_dhcp.c` breaks the network block's CRC while the link
  is connected and polls up to 1 ms before T1: the connection stays up, nothing
  is closed and no DHCP datagram is sent. Before, a link that reloaded the
  block on every connected poll (`src/proto/link_eth.c:88` read as `if (1 &&`),
  and so dropped a working connection whenever the block read bad between lease
  events, left every link suite green.
