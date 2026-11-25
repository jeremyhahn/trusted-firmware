### Added

- **`TestNetConfigWithDHCPStillRefusesWhatItCannotEncode` pins the endpoint
  exception in sdk/go/netcfg.go:134** (#55). A DHCP network block with an
  unset endpoint address and a non-zero port is refused, not written as the
  zero peer the device treats as absent, and every row now checks the field
  named in the refusal.
