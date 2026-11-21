### Changed

- **`qemu_tz_test.sh`'s run has a test** (#628): `scripts/test_qemu_tz_run.py` drives it with a stub QEMU and pins the address check, both loaders, each required UART line, the secure SRAM read and an unexpected fault refused.
