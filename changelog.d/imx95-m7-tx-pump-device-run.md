### Changed

- **i.MX 95 M7 transmit-pump run on the board** (row `imx95`). The bench
  record holds the device run of the relinked, transmit-pumped M7 image.
  The container check passed and the image booted from the eMMC. The pads
  are at ALT1 and the LPUART3 clock is on, but the M7 console still shows one
  `0x00` and no frame. The A55 cannot read the M7 DTCM (TRDC), so the next
  step is a read path or step marks on the console itself.
