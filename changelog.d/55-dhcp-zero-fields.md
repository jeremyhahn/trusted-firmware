### Added

- **DHCP tests that the unused fields of every client message are zero**
  (#55): `sent_as` in `test/unit/test_dhcp.c`, which checks every
  DHCPDISCOVER, DHCPREQUEST and DHCPDECLINE the client sends, now also checks
  that secs, the chaddr padding, sname and file are zero (RFC 2131 Table 5).
  Before this change a client that put the xid in secs (`src/net/dhcp.c:284`)
  or filled chaddr, sname and file with non-zero bytes (`src/net/dhcp.c:291`)
  left every DHCP suite green; both now fail on both chips.
