### Added

- **The AVR tick test takes `tfw_hal_millis()` through every byte of its
  32-bit count** (#635): across a carry out of each of the low three bytes
  and through the 600 s valve run, so a `ticks` counter narrowed to 16 or 24
  bits now fails `make host-test`.
