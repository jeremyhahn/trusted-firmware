### Fixed

- **The i.MX 95 M7's console no longer stalls on a full transmit ring**
  (SPEC row `imx95`). With no UART interrupt taken, a full ring made every
  retry report full, and nothing sent the queued bytes. The retry now sends
  what the transmitter can take before it checks for room.
- **`IMX_BOOT=1` builds into its own directory** (`soft-imxboot`), so
  switching it relinks the image rather than reusing the one linked for
  the other mode.
