### Fixed

- **The MCXN94x LPUART baud search accepts every rate within 3%** (#628).
  `src/hal/cortex-m/mcxn94x/lpuart_baud.c` truncated `baud / 100` before
  multiplying by 3, so the allowance fell up to 2 baud short of 3% (and was 0
  below 100 baud): 773195 baud from FRO12M, 2.9999% slow, was refused. The
  error is now compared to 3% exactly; rates more than 3% off are still refused.
