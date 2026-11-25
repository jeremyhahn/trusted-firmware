### Added

- **The nRF54L15 UART tests leave stale events and send the TX ring past its
  wrap** (#518). The boot UART tests leave a stale RXTO and TXDRDY and a live
  UARTE before an unlisted rate, and the app UART tests send the TX ring past
  its wrap, check the re-init NVIC disable and take a byte that came with an
  error.
