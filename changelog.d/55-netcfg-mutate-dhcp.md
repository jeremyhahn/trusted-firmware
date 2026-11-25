### Fixed

- **sdk/go's network-block mutation tests check DHCP** (#55). Their by-hand
  decoder and comparator left out `NetConfig.DHCP`, and the past-the-CRC test
  called the flags byte reserved; they now decode DHCP from flags bit 0, as
  docs/ethernet-transport/api.md says, and a changed flags byte must decode
  to the original addresses with DHCP equal to that bit.
