### Fixed

- **The DHCP suite pins a renewal's new lease length** (#55).
  `test_a_renewal_ack_that_shortens_the_lease_is_held_to_its_new_length` in
  `test/unit/test_dhcp.c` has the server shorten the lease from 3600 s to
  600 s on the DHCPACK to a renewal at T1, and checks that the client takes
  the new length (RFC 2131 §4.4.5): the next renewal is due 300 s after T1 and
  the lease ends 600 s after it. Before, a renewal that kept the first lease's
  length (`src/net/dhcp.c:482` taken only while requesting) left every DHCP
  suite green.
