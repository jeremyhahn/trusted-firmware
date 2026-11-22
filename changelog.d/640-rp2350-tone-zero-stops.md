### Fixed

- **TONE with 0 Hz stops a playing tone on the Pico 2 W** (#640): the RP2350's
  `tfw_hal_tone_start` refused hz 0 with `TFW_ERR_RANGE` and left the tone
  running; it now stops it, as `include/tfw/hal.h` and every other port say.
