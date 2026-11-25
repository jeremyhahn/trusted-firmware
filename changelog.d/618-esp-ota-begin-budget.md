### Changed

- The ESP32 OTA_BEGIN budget is tested to be spent by a failed esp_ota_begin, untouched by a refusal before the erase, and refilled by a successful activate (#618).
