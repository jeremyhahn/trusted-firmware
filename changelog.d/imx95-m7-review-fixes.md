### Fixed

- **The i.MX 95 M7's console no longer stalls on a full transmit ring**
  (SPEC row `imx95`). With no UART interrupt taken, a full ring made every
  retry report full, and nothing sent the queued bytes. The retry now sends
  what the transmitter can take before it checks for room.
- **`IMX_BOOT=1` builds into its own directory** (`soft-imxboot`), so
  switching it relinks the image rather than reusing the one linked for
  the other mode.
- **`boards/frdm-imx95-m7/imx-boot-m7.py` has a host test**
  (`test_imx_boot_m7`, in `make host-test`) on a synthetic ELF and boot
  container. It checks that only the M7 slot and its entry change, and that
  a wrong load address, reset target, stack or image size is refused.
