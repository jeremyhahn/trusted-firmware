### Changed

- **The ESP32-P4's OTA rows, run on a soft-tier image** (#552).
  `docs/boards/esp32p4-poe.md` records them: the soft tier's activate gate
  row passes; OTA write, OTA_INFO, trial, confirm and roll back pass on the
  board by hand with the build's sealed image (the soft tier's gate is
  `auth_session_none.c`, so no operator session is needed). The rows
  `OTAAuthLetsTheCommandPastTheGate` and `OTAInfo` fail: the first stages
  an unsealed image of its own, the second wants an element-tier peer. No
  host row drives the soft-tier lifecycle yet.
