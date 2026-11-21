### Fixed

- **`docs/application-sdk/api.md` states every return of the config calls**
  (#640). `tfw_app_config_read` and `tfw_app_config_write` are also
  `TFW_ERR_RANGE` when `TFW_APP_CONFIG_BASE + off + len` passes the end of the
  EEPROM, and `TFW_OK` for a zero-length access in range without touching the
  EEPROM; a host test now pins the part-end refusal.
