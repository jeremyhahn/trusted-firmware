### Added

- **The ESP32 bootloader's partition-table walk is fuzzed on the host** (#48,
  row `esp32-p4`). `test_boot_hal_esp32_mutate` lays out seeded random
  partition tables, with tables cut short by an entry without magic, labels
  that extend or shorten the wanted one, duplicates and a full table, and holds
  the platform-block read, the activate-authorization read and the
  platform-block write to a reference walk and a shadow flash, at every
  partition and sector edge.
