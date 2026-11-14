### Added

- **ESP32-P4 bench run for row 48** (#48). `docs/development/bench/esp32-p4.md`
  records HIL-0 steps 1 to 4 on the board with this tree's image: the
  re-read, the build, the four writes, and layer 0's ES256 check passing on
  the console. The wire over Ethernet was not run: no board address was
  handed to the lane and the bench network has no DHCP server. `espefuse
  summary` before and after is identical: no eFuse was written.
  `docs/plans/48.md` holds the row's plan. No code, wire command or vector
  changed.
