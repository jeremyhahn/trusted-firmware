### Fixed

- **An ESP32 tone started on another pin sounds on that pin alone** (#48).
  `ledc_channel_config` routes the LEDC channel to the new pin and leaves
  the old one routed too, so a TONE on one pin then another, with or without
  a NOTONE between, sounded on both. The start now resets the pin the
  channel drove, as MODE does; a retune on the same pin resets nothing.
