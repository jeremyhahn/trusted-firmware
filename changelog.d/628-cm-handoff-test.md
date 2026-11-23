### Added

- **A host test pins the Cortex-M application's handoff block to its `.handoff` section** (#628): `test/unit/hal/test_cm_handoff.c`, run by `make host-test`, fails if the block in `src/hal/cortex-m/hal_handoff.c` loses its section attribute.
