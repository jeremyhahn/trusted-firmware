### Added

- **i.MX95 M7 bench register dump, `REG_DUMP=1`** (row `imx95`, bench only,
  off by default and in release images). It turns the step marks on and,
  after each mark, copies LPUART3's VERID, PARAM, GLOBAL, BAUD, STAT and
  CTRL, with the polls the mark waited for TDRE, into MU5 SRAM past the
  System Manager's three SMT channels, where the SM monitor's `md` reads
  them (neither the SM nor the A55 can read LPUART3 itself). Layout in
  `soc_regs.h`; host test `test_imx95_regdump` on the register model, which
  gains LPUART VERID, PARAM and a plain-memory area.
