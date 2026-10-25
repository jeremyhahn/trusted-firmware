### Changed

- **`xkms` builds against `sdk/go` v0.4.1-alpha** (#231). The adapter's
  `xkms/go.mod` required v0.4.0-alpha, whose tfwsim harness writes the network
  block at the fixed 0x0C0; v0.4.1-alpha asks the simulator for the address, so
  the `xkms/avr` field-link tests follow the firmware's `TFW_NETCFG_ADDR`.
