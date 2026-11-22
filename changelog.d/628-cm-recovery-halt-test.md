### Changed

- **The Cortex-M recovery halt has a host test** (#628): `boot/hal/cortex-m/test/test_boot_cm_recovery_halt.c`, in `make boot-test`, pins `boot/hal/cortex-m/recovery_halt.c` (BOOT_OTA=0): interrupts masked before the first byte, the refusal reason as one EVENT on entry and again after every whole frame, no command answered, nothing booted. No firmware, wire format or status code changed.
