### Added

- **i.MX 95 `PIN_MODE` OUTPUT keeps the bank's other directions** (row
  `device-capabilities`). A host unit test seeds other `PDDR` bits of the shared
  RGPIO bank, sets one line to OUTPUT and asserts only that line's bit changes.
