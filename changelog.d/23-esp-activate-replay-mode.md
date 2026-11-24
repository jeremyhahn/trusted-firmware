### Fixed

- **A repeated ESP32 OTA_ACTIVATE is pinned to the mode the install was activated with** (#23): a host test now checks that `tfw_otactl_cmd_activate` in `src/hal/esp32/hal_ota.c`, asked again with no new OTA_AUTH after a mode-1 activate, answers OK for mode 1 and BAD_STATE for mode 0, and writes no second activate record or boot partition, so losing the remembered mode (which turns a host's retry after a lost reply into a refusal) fails the OTA host suite.
