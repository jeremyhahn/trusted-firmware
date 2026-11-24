### Added

- **`test_sys_info_u16_free_ram_saturates_exactly_past_0xffff` in `test/unit/test_sys.c`** (#627): SYS_INFO is fed free RAM of 0xFFFE, 0xFFFF, 0x10000, 0x10001 and 0xFFFFFFFF; the u16 `free_ram` reads the value up to 0xFFFF and 0xFFFF above it, never a small wrong number, and `free_ram32` stays exact, so moving the saturation bound in `src/caps/sys.c` to 0x10000 now fails the host suite.
