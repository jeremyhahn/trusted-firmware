### Added

- **The ESP32 reset-cause test pins a handed-off cause past `TFW_RESET_PANIC`** (row #48): `test/unit/test_esp_sys_mutate.c` proves `tfw_hal_reset_cause()` returns a handed-off 7, 0x80 or 0xFF unchanged for every `esp_reset_reason()`, neither refined by the chip nor folded to UNKNOWN, since the reset-cause values extend (`include/tfw/types.h`).
