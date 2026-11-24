### Fixed

- **The ESP32-C3 and ESP32 layer 0 reads a timer-group-1 watchdog reset as a
  watchdog** (#48). `RESET_REASON_CORE_MWDT1`, the system reset at the end of
  ESP-IDF's interrupt watchdog, read as an unknown reset cause, and the
  application passed that on to the host.
