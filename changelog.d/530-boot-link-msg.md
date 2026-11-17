### Fixed

- **A failed `tfw-boot` link names its real cause** (#530). `mk/boot.mk`
  reported every failed boot link as "it does not fit in the boot section"
  and showed only overflow lines, so an `-Werror=frame-larger-than` error at
  the LTO link (#497) read as a size problem. The new
  `scripts/boot_link_report.sh` keeps that message, `boot-core-size` and the
  budget text for a region overflow or "will not fit" line only; any other
  failure prints "tfw-boot does not link for BOARD:" and link.err's error
  lines (`error:`, frame-larger-than, vla, alloca, undefined reference).
  `scripts/test_boot_link_report.py` (`make scripts-test`) pins both cases.
