### Added

- **A host test for the Cortex-M QEMU suite's driver** (#628): `make scripts-test` runs `test/integration/cortex-m/qemu_test.sh` against a stub QEMU and wirecheck, pinning the usage refusal, the rows and their order, the loader addresses, no platform block on the unprovisioned row, and a failing row named.
