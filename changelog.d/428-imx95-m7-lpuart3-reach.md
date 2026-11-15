### Changed

- **i.MX95 M7: the image on the board is ours, and it reaches LPUART3** (row
  `imx95`). Bench record section 13: the M7 entry of the eMMC boot container
  equals a rebuild of our image byte for byte, the LPUART3 divisor is right
  for the 24 MHz root, and the M7's SCMI shared memory, read through the SM
  monitor, shows its pad request answered, so the M7 writes LPUART3 without
  a fault while port 2 stays silent. No driver change, no board write.
