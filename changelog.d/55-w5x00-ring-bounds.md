### Added

- **Host tests for the W5x00 driver's ring and free-space bounds** (#55). The
  test_w5100 and test_w5500 binaries now read a received size of exactly the
  2 KB ring on the TCP and the UDP socket without dropping either, send a
  datagram that exactly fills the free space, and check that a datagram sent
  after one unanswered ARP reads its own result, not NOARP.
