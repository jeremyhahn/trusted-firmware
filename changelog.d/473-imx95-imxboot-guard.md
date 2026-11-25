### Fixed

- **The FRDM-i.MX95 M7 `IMX_BOOT=1` build-dir guard compares paths** (#473):
  `BUILD_DIR=build/`, `./build` or `build//` is refused like `build`, so the
  ITCM-0 image never reuses the ELF linked at the slot. `IMX_BOOT` other than
  `0` or `1` (`yes`, `true`, `2`) is refused instead of silently linking at the
  0x00008000 slot.
