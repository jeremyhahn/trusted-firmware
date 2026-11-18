### Fixed

- **The `Caps` and `I2CConfig` hardware rows know the `none` crypto tier**
  (#576). A `CRYPTO=none` image, such as the ESP32-P4 PoE at HIL-0, made both
  rows stop at "no facts" for its tier. `Caps` now expects such an image to
  report no algorithms. `I2CConfig` expects it to apply the clock, as the
  software tier does. Both rows are now in that board's `make hw-test` row
  list. The board's hardware re-run waits on #561.
