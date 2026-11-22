### Added

- **A test pins the W5100's socket 1 map against the datasheet** (#55). The
  DHCP client's socket registers sit at 0x0500 and its TX and RX pointers
  wrap into 0x4800-0x4FFF and 0x6800-0x6FFF, so a TX1 write that dropped the
  ring mask and ran into socket 2's buffer now fails a test.
