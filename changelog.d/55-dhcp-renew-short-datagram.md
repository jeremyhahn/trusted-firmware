### Fixed

- **The DHCP suite pins a renewal's refusal of an out-of-step datagram** (#55).
  `test/unit/test_dhcp.c` checks that a renewal reply whose UDP header claims
  more than the controller holds makes `tfw_dhcp_renew` return
  `TFW_ERR_INTERNAL`, close the UDP socket, leave the link's connection alone
  and forget the lease, so the next poll asks again from INIT. Before, a renewal
  that kept the socket and the lease on an out-of-step receive ring left every
  DHCP suite green.
