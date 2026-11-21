### Fixed

- **The shed-ota default-row probe has no build side effect** (#640).
  `scripts/test_boot_shed_ota.py`'s dry run of `make bootloader` for the
  ATmega2560's default soft row no longer runs tfw-boot's ELF recipe, which
  GNU make executes under `-n` because it holds `$(MAKE)`: it passes `-o` for
  the ELF, `MAKE=true`, `ARCH_CC=true` and a scratch `BUILD_DIR`, checks the
  dry run's exit status, and fails if anything under `build/` is created or
  changed.
