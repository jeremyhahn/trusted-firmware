### Fixed

- **`OTA_BEGIN` has a per-boot budget** (#232, the peer-binding ADR's
  decision 1 B). After `TFW_OTA_BEGIN_BUDGET` begins since the last reset or
  successful `OTA_ACTIVATE` (default 4, set per board in `board_profile.h`),
  `OTA_BEGIN` answers `TFW_ERR_BAD_STATE` (19) before it erases anything, so a
  looping peer cannot wear the store. The count is kept in RAM only, on the
  AVR stager and on ESP32 alike.
- **A second `OTA_ACTIVATE` while one is pending** (m1100 (3), m1117
  (1)). It answers success and changes nothing only when no `OTA_AUTH` has
  arrived since the pending activation and its `[mode:1]` matches. After any
  new `OTA_AUTH`, with the same signature or a different one, it is a new
  activation attempt and answers `TFW_ERR_BAD_STATE` (19), as does a different
  mode.
- **`EEPROM_WRITE` over the network block takes the auth gate** (#233,
  decision 2 B). A write that overlaps `[TFW_NETCFG_ADDR, TFW_NETCFG_ADDR +
  TFW_NETCFG_LEN)` calls `tfw_auth_gate` first. Outside an operator auth
  session on the element tier, it answers `TFW_ERR_AUTH_REQUIRED` (17) and
  writes nothing. The soft tier's gate admits every write.
- **`tfwctl netcfg set --serial <hex> --operator-key <pem>`** opens the operator auth session before its `EEPROM_WRITE` (#233).
