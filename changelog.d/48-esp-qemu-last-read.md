### Fixed

- **The ESP32 QEMU harness no longer reports a line missing that QEMU printed
  just before it exited** (#48). `Qemu.wait_for` read the log, then waited up
  to 0.25 s for QEMU to exit, and on the exit (or at the deadline) failed
  without reading the log again, so output written in that window was never
  looked at. It now reads the log one last time before it fails.
