### Added

- **FRDM-IMX95-PRO boot container with our M7 image** (#428, row `imx95`,
  ruling 644). `boards/frdm-imx95-m7/imx-boot-m7.py` replaces the M7 image
  (data, size, SHA-384) of the board's own unsigned imx-boot and keeps every
  other byte. On the bench board the original container is backed up with
  its digest, the new one written at 32 KiB in the eMMC user area and read
  back; after a System Manager reset the ROM, OEI, SM and Linux boot from it
  and the M7 runs our image. Its console is still silent: the LPUART3 pads
  are muxed by the M7 over SCMI pin control, which our image does not do
  yet (`docs/development/bench/frdm-imx95.md`).
