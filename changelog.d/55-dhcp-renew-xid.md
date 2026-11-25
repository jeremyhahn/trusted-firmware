### Fixed

- **The DHCP suite pins the renewal's new xid** (#55). `test/unit/test_dhcp.c`
  checks that a renewal at T1 asks under an xid other than the first
  exchange's, that a DHCPACK under the first xid is not taken as its answer
  (RFC 2131 §4.1), and that one under the renewal's xid renews the lease. Before,
  dropping the `new_xid()` call in `tfw_dhcp_renew` left every DHCP suite green.
