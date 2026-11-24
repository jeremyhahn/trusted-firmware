### Fixed

- **The esp32-qemu board page cites `mk/arch/esp32-targets.mk` at its current
  lines.** Its four cites of the build, the test, the test's three builds and
  the test's description were one line early since #760 added a comment line
  above them. `make scripts-test` now finds those four spans by their content
  and fails when a cite on the page names any other lines, or when the page's
  `mk/arch/esp32.mk` cites stop naming the ESP-IDF and QEMU pins it quotes.
  No build target, firmware or wire format changed.
