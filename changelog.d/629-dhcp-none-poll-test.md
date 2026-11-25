### Added

- **Tests of `src/net/dhcp_none.c`'s `tfw_dhcp_poll`** (#629). A block that
  asks for DHCP is refused with its IP, mask and gateway zeroed and its MAC,
  peer and port kept; a static block comes back untouched, field by field.
