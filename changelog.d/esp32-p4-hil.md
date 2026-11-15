### Added

- **ESP32-P4 bench run for row 48** (#48). `docs/development/bench/esp32-p4.md`
  records HIL-0 on the board with this tree's image: the re-read, the build,
  the four writes, and layer 0's ES256 check passing on the console. With a
  static-address overlay on the development VLAN the IP101 link came up and
  `wirecheck -board 0x80 -tier 0` passed over tcp/21574 (PING, CAPS,
  SYS_INFO, ATTEST refused on the none tier). `espefuse summary` before and
  after differs in no eFuse line: no eFuse was written. `docs/plans/48.md`
  holds the row's plan. No code, wire command or vector changed.
