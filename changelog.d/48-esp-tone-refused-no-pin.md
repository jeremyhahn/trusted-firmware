### Added

- **The ESP32 tone's mutation test checks a refused start claims no pin**
  (#48). A start the LEDC timer or channel refuses leaves
  `tfw_esp_tone_gpio()` at -1, and one refused while a tone runs on another
  pin leaves that tone running and the RMT loopback on its pin.
