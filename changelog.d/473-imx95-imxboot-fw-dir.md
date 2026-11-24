### Fixed

- **frdm-imx95-m7: IMX_BOOT=1 builds in its own `-imxboot` directory** (#473), so switching modes in one BUILD_DIR never hands a container the ELF linked at the slot.
