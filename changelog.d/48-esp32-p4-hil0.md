### Changed

- **ESP32-P4 bench HIL-0 re-run on main's image** (#48, row `esp32-p4`).
  `docs/development/bench/esp32-p4.md` records a read-only re-read before any
  write (chip, flash and every security eFuse equal to the factory record),
  then the `sdkconfig.hil0` build, the four images written and the four data
  sectors erased, and the boot: layer 0 verified the sealed app's trailer and
  the app started. No eFuse was written. The Ethernet link stays unchecked:
  the build has no static address and the bench network has no DHCP server.
