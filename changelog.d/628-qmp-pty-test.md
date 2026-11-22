### Added

- **`qmp_pty.py` has a test** (#628): `scripts/test_qmp_pty.py` drives it against a fake QMP monitor, pinning the pty path it prints and an exit 1 with a reason for each QMP failure, never a hang; `make scripts-test` runs it.
