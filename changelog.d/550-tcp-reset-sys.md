### Fixed

- **`make hw-test PORT=tcp:<ip:port>` runs the boot-event and Sys rows** (#550): `RESET_PORT=<serial port>` (`TFW_HW_RESET_PORT`) resets a tcp: board by a DTR pulse before the boot-event row dials it again; without it that row skips, naming `RESET_PORT`, and never passes. `TestFirmwareSys` asserts the framer counters do not move across its own commands, so a malformed burst an earlier row sent no longer fails it on a board nothing reset. Host tests run both on a simulated tcp: peer.
