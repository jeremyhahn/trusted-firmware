### Fixed

- **`OTA_BEGIN` refuses when no SPI NOR part is fitted** (#167). With nothing
  on the store's select, the READ STATUS busy poll read the empty bus as ready,
  so `OTA_BEGIN` and every `OTA_DATA` answered `TFW_OK` and only `OTA_END`
  failed. `OTA_BEGIN` now reads the store's JEDEC ID (0x9F) before it erases
  anything, and answers the new status `TFW_ERR_NO_STORE` (20) when the
  manufacturer byte is 0x00 or 0xFF, which JEP106 assigns to no vendor. Nothing
  is erased and the stager stays idle. Any other manufacturer is accepted.
