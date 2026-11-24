### Fixed

- **The FRDM-i.MX95 M7 board page gives `IMX_BOOT=1`** (#473): the boot
  container's image, linked and measured at ITCM 0 in its own `BUILD_DIR`
  (`make BOARD=frdm-imx95-m7 IMX_BOOT=1 BUILD_DIR=build/imx95-imxboot
  firmware`). The page named only the 0x00008000 slot, which the default build
  still uses.
