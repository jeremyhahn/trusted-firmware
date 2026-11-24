### Fixed

- **`docs/device-capabilities/api.md` states the zero-length EEPROM replies** (#627).
  A `len` 0 `EEPROM_READ` is `TFW_OK` with an empty `data` (its `addr` still
  checked and auth-gated); an `EEPROM_WRITE` with no `data` is
  `TFW_ERR_BAD_ARGC` at dispatch.
