### Added

- **A DHCP test that a retransmitted DHCPREQUEST keeps the lease's start**
  (#55): `test/unit/test_dhcp.c` lets the first DHCPREQUEST go unanswered,
  ACKs the resend, and checks that T1 falls a half-lease after the first
  request, as `src/net/dhcp.c` counts it (RFC 2131 §4.4.1). A client that
  restarted the lease clock at each resend, and so held its address past the
  server's view of the lease, now fails on both chips.
