### Fixed

- **An ESP32 board whose Ethernet does not start now says so** (#23).
  `tfw_esp_eth_start` (`src/hal/esp32/hal_eth.c`) logs one warning, `no link:
  Ethernet did not start, no wire on tcp/21574`, on every failed start and
  nothing on a good one; before, a dead PHY, a bad RMII clock or a static
  address not taken left the board with no wire and no message. No wire
  format, status code or call site changed.
