### Changed

- **The ESP32-P4's OTA rows, run on a soft-tier image** (#552).
  `docs/boards/esp32p4-poe.md` records them: the soft tier's activate gate
  passes; OTA staging fails on the board with `crypto provider failed`;
  OTA_INFO, trial, confirm and roll back cannot run there, because no P4
  tier links the operator session.
