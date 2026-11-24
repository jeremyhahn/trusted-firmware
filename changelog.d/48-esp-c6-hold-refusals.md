### Added

- test/unit/test_esp_selfcheck.c pins the ESP32-P4 self-check's C6 enable check against refused gpio calls: a refused re-hold, hold release, drive low or pad direction each fails `c6_enable`, and after a refused setup call the pad is never read and is still re-held (Refs #48).
