### Fixed

- **`tfw_boot_floor_store` no longer reports a floor it did not store** (#75).
  On a build without an element it returned `TFW_OK` and raised the block's
  in-RAM floor even when `tfw_boot_platform_store` had refused to write over a
  block this boot did not load, or the EEPROM write failed. It now returns
  `TFW_ERR_BAD_STATE` for the refused block and the write's error otherwise,
  and leaves `rollback_floor` at the value EEPROM holds, so `tfw_boot_settle`
  keeps the block `CONFIRMED` and the next reset retries the advance.
