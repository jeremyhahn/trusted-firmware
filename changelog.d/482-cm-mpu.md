### Added

- **MPU-only isolation on the M7** (#482): on `mps2-an500`, `src/hal/cortex-m/hal_mpu.c` arms the PMSAv7 MPU with a null guard at 0, a stack guard at the stack floor and tfw-boot's region hidden from the application; a violation ends in `PANIC`. `test/unit/test_cm_mpu.c` checks the region table; `TestQEMUMPUNullRead` is the QEMU wire row.
