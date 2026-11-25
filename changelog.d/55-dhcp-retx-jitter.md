### Added

- `test_discover_retransmissions_are_jittered_not_fixed` in `test/unit/test_dhcp.c`: the first four DHCPDISCOVER retransmissions do not all wait the same offset from 4, 8, 16 and 32 s, as `src/net/dhcp.c:232` draws a uniform -1..+1 s per send (RFC 2131 section 4.1) (Refs #55).
