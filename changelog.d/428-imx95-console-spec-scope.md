### Changed

- **The i.MX 95 M7 console is LPUART3, as SPEC row `imx95` names it**
  (#428). Base `0x42570000`, M7 interrupt 64, and the SCMI pad request asks
  for SM pins 18 and 19 (GPIO_IO14/15) at ALT1. `soc_regs.h` asserts the
  base and interrupt at build time. The FRDM-IMX95-PRO's console port is
  wired to LPUART7 (bench record sections 14 and 15); moving the console
  there is a SPEC change for the owner.

### Removed

- **The FRDM-IMX95-PRO boot-container packager** (`imx-boot-m7.py` and its
  host test). The boot container belongs to the trusted-imx95 project. The
  `IMX_BOOT=1` build, which links the M7 image at ITCM 0 for a container,
  stays.
