### Changed

- **The esp32 CI image is slim** (#470): an `esp32-build` stage settles ESP-IDF's tools for the boards' chips (esp32, esp32c3, esp32p4) and drops its git history, and the `esp32` stage copies `/opt/esp`, Go and the module cache onto the ESP-IDF image's own Ubuntu 24.04, by digest.
