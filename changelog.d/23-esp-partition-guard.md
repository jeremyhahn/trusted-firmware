### Added

- **`make scripts-test` checks both ESP32 partition tables** (#23).
  `scripts/test_esp32_partitions.py` reads `boards/esp32p4-poe/partitions.csv`
  and `boards/esp32-qemu/partitions.csv` as plain CSV, with no ESP-IDF: the
  rows parse, ascend and do not overlap, start above the partition table at
  `CONFIG_PARTITION_TABLE_OFFSET`, put the app slots on 64 KB and inside the
  board's flash (below 16 MB on esp32p4-poe), and hold the four `tfw_*` labels
  `src/hal/esp32/hal_esp32.h` names, each once with its own custom subtype,
  `tfw_plat` and `tfw_eeprom` encrypted. The `tfw_plat` and app offsets and
  the erase region `docs/boards/esp32p4-provisioning.md` gives must be the
  table's. Bad tables built from the real ones show each check reports its
  fault. No partition table, firmware or wire format changed.
