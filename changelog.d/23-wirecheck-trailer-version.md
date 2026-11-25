### Fixed

- **`wirecheck` refuses an ESP32 image whose trailer_version is not 1**
  (#23). It checked only the TFIM magic, so it signed OTA_ACTIVATE and
  OTA_CONFIRM over the image_version of a trailer layout it does not know.
