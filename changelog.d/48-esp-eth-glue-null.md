### Fixed

- **The ESP32 Ethernet start refuses a netif glue ESP-IDF could not make** (#48).
  `src/hal/esp32/hal_eth.c` passed `esp_eth_new_netif_glue`'s result to
  `esp_netif_attach` unchecked, so an allocation failure there dereferenced
  NULL on the P4 instead of ending the start with `TFW_ERR_INTERNAL` and its
  "no link" warning, as a failed netif, MAC, PHY or driver install does;
  `test/unit/test_esp_eth_mutate.c` now fails the glue in both arms.
