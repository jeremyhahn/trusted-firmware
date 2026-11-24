### Added

- **The tcp-listen: hw-test port is tested to be let go and to bind as
  documented** (#599). The STM32H573I-DK's ETH_MAC=1 rows run
  `make hw-test PORT=tcp-listen:<host-port>`; new host tests pin that the
  port is let go after a failed reset, a silent board and a taken dial (the
  board's later dials are refused and the next row's listener binds it), that
  a busy port fails at once naming itself, and that a bare port binds every
  interface.
