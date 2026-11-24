### Fixed

- **The ESP32 boot hold's bound is held by a host test** (#23). The test that
  held output past `TX_BUF` (512 bytes) is discarded whole sent a byte after
  the one that overflows, and that extra byte cleared the hold, so an
  off-by-one at the bound (writing `tx_buf[512]` and sending the first host
  513 bytes cut) passed every host test. Two tests now pin both edges: exactly
  `TX_BUF` held bytes reach the first host whole and in order, and one more,
  with nothing sent after it, leaves the first host nothing. No firmware, wire
  format or status code changed.
