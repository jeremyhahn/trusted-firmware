### Fixed

- **ESP32 OTA_ACTIVATE reads the trailer only inside the uploaded stream**
  (#23). `check_staged()` in `src/hal/esp32/hal_ota.c` bounded ESP-IDF's image
  end against the slot only, so a session that uploaded an image and digest
  with no trailer activated on an earlier session's trailer left on flash. It
  now refuses with `TFW_ERR_RANGE` unless the trailer lies within the length
  OTA_BEGIN announced, as `ota_stage.c` takes the stream's last
  `TFW_BOOT_TRAILER_LEN` bytes. No wire format or status code changed.
