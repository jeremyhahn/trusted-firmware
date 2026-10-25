### Changed

- **The tfwsim harness writes the network block where the firmware reads it**
  (#231). `tfwsim.StartElementEthernet` wrote the block at the constant
  `NetConfigAddr`; it now asks the simulator binary, which answers the new
  `tfwsim --netcfg-addr` with its build's `TFW_NETCFG_ADDR` (0x140), so the
  harness assumes no address. A binary built before this flag fails the test
  and says to run `make tfwsim`. `NetConfigAddr` stays: `EEPROMReservedEnd`,
  0x140, where the bootloader's reserved span ends.
