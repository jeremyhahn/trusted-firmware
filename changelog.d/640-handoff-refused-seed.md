### Fixed

- **The handoff mutation suite checks the DeviceID seed** (#640). `test/unit/test_handoff_mutate.c` now builds its valid record with a seed, reads it back through `tfw_boot_handoff_device_seed` on every valid install, and counts a refused record that still hands out a seed (or writes `out`) as acted on as valid. Three new tests cover a refused record after a valid one dropping the seed the valid one left, a valid record without a seed holding none, and a seed whose only non-zero byte is the last being held whole. No firmware code changes.
