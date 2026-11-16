### Fixed

- **The i.MX 95 M7's boot-container image is linked where the container
  loads it** (SPEC row `imx95`). `make BOARD=frdm-imx95-m7 IMX_BOOT=1` links
  the application at ITCM 0, where the System Manager loads the M7 image and
  takes its vector. Packaging it into the boot container is the
  trusted-imx95 project's, not this repository's.
- **The SCMI pin-control encoder no longer writes past its buffer.**
  `tfw_scmi_pinctrl_settings_configure` refuses more configs than fit the
  32-byte payload (TFW_SCMI_E_TOO_LONG, before any write). Its maximum, two
  configs, is derived from the payload size and tested at the boundary.
  `test_imx95_console_pins` now runs in `make host-test`.
