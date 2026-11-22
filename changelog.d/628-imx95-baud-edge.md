### Added

- **A host test pins the i.MX 95 LPUART divisor's 3 % limit** (#628).
  test_imx95_soc_calc now checks that 1000 baud from a 4850 Hz clock (970
  baud, exactly 3 % slow) is accepted with BAUD 0x04020001, and that from
  4849 Hz (969 baud, 3.1 % slow) it is refused with the output untouched.
