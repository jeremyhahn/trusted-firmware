### Added

- **`IMX_BOOT=1` links the FRDM-i.MX95 M7 image at ITCM 0** (#473), the boot
  container's load address, and measures it from there; the default build stays
  at the 0x00008000 slot. It needs its own `BUILD_DIR` (`make
  BOARD=frdm-imx95-m7 IMX_BOOT=1 BUILD_DIR=build/imx95-imxboot firmware`) and is
  refused in the default `build/`. The container itself is trusted-imx95's.
