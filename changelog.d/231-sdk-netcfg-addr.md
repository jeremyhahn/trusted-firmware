### Changed

- **The tfwsim harness writes the network block where the firmware reads it**
  (#231). `tfwsim.StartElementEthernet` wrote the block at the constant
  `NetConfigAddr` (0x0C0); it now asks the simulator binary, which answers the
  new `tfwsim --netcfg-addr` with its build's `TFW_NETCFG_ADDR`, so the same
  `sdk/go` drives today's firmware (0x0C0) and #231's (0x140). A binary built
  before this flag fails the test and says to run `make tfwsim`.
  `NetConfigAddr` stays, documented as the address firmware before #231 reads
  the block at.
