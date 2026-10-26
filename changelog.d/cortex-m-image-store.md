### Added

- **An image store on both QEMU Cortex-M boards** (row `cortex-m`, #161, #75,
  #52). `OTA_STORE := mem` keeps the store in 264 KiB of the code bank that no
  loaded image covers, so QEMU's reset leaves it alone, as it leaves the
  platform block. `src/store/mem.c` is the application's side and
  `boot/hal/cortex-m/boot_ota_store_cortex-m.c` is tfw-boot's. Both follow a
  NOR's erase and program rules, and `test_store_mem` runs both over one host
  array. `mk/boot.mk` adds `mps2-an505` with OTA in boot and a store row on
  each board. P-256 is verified on every row, and none of them is measured
  yet. `docs/cortex-m/README.md`, "The image store", has the layout and the
  QEMU sources.
- **The staged image in the QEMU wire run, and the OTA cycle** (row
  `cortex-m`, #161). On both MPS2 boards the wire run also boots the
  application built with `OTA_STAGE=1` under the store row, and runs the
  firmware, OTA and app rows there. `TestQEMUOTACycle` stages the base
  application that `cm_image` signed, activates it, resets through QMP, and
  checks in that boot that ATTEST reports the staged image's measurement.
  OTA_INFO and OTA_CONFIRM are not linked on a single-slot stager. The rows,
  `TestFirmwareOTAInfo` among them, check that they are refused as
  `ERR_UNKNOWN_CMD`, so every OTA row runs on every board.

### Fixed

- `mk/ccdeps.sh` keeps the header that `-include` or `-imacros` names.
  Before, it dropped every `*.h` argument, so the option took the next source
  as its file.
