### Added

- **The i.MX95 bench record answers whether the M7 may use LPUART3** (#428).
  `docs/development/bench/frdm-imx95.md` section 12: the board's System
  Manager config (imx-sm `lf-6.18.20-2.0.0`, `mx95frdm-pro`) gives LPUART3
  and its pads to LM1 (domain 4), and the live TRDC agrees (read/write on
  LPUART3's MBC block for the M7's domain; clock on at 24 MHz; pads muxed).
  No SM configuration change is needed. Read-only: nothing was written to
  the board. No firmware, wire command or vector changed.
