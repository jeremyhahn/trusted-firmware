### Changed

- **A test holds the network block's write to the session** (#629). The
  `test_auth_mutate` test `test_a_network_block_write_waits_for_the_session`
  sends an EEPROM_WRITE into the network block without a session and expects
  AUTH_REQUIRED with the block unchanged, then the same write after an
  operator response and expects it to land. It holds the `return st;` after
  `tfw_auth_gate()` in `tfw_storage_cmd_write` (`src/caps/storage.c`), which
  no test failed without before.
