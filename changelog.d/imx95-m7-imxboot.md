### Added

- **FRDM-IMX95-PRO boot container with our M7 image** (#428, row `imx95`,
  ruling 644). The bench run replaced the M7 image (data, size, SHA-384)
  of the board's own unsigned imx-boot, keeping every other byte, with a
  one-off bench tool. The tool is not in this repository: boot-container
  packaging belongs to trusted-imx95 (`docs/multi-arch/imx95-design.md` R12,
  #473). This repository's share is `IMX_BOOT=1`, which links and measures
  the application at ITCM 0. On the bench board the original container is backed up with
  its digest, the new one written at 32 KiB in the eMMC user area and read
  back; after a System Manager reset the ROM, OEI, SM and Linux boot from it
  and the M7 runs our image. Its console is still silent: the LPUART3 pads
  are muxed by the M7 over SCMI pin control, which our image does not do
  yet (`docs/development/bench/frdm-imx95.md`).
