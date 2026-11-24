### Added

- **Cortex-M MPU table: a test holds the smallest tfw-boot region** (#628). `test_cm_mpu` gives the table a 32-byte tfw-boot region, the smallest PMSAv7 region `hal_mpu.h` accepts, and checks it is hidden (SIZE 4) from an application and left open, with the null guard still set, for an image inside it.
