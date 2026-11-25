### Added

- **`test_the_last_chunk_of_the_part_reads_and_writes` in `test/unit/test_storage.c`** (#627): an eight-byte EEPROM_WRITE and EEPROM_READ ending exactly at `TFW_EEPROM_SIZE`, and a one-byte read of the final byte, return `TFW_OK` with the bytes written and read back, so turning `>` into `>=` in either handler's end-of-part check (`tfw_storage_cmd_read` line 79 and `tfw_storage_cmd_write` line 108 in `src/caps/storage.c`) now fails the host suite.
