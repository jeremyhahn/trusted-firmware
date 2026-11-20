### Fixed

- **The ESP32-P4's RMT pulse capture keeps an output driven on the same pin**
  (#573). The RX channel opens with `io_loop_back`, so a capture on the tone
  pin reads the LEDC tone instead of detaching it; not yet verified on hardware.
