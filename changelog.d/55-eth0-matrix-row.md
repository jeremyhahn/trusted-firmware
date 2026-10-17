### Added

- **The serial-only builds in the Ethernet test matrix** (#55, #7): the matrix
  in `docs/ethernet-transport/test-matrix.md` has rows for the Mega's and the
  Nano's `CRYPTO=soft` image without `ETH`. The Mega's row names the dated
  passes that ran it (the 10-15 first pass, fd7a696, train/ota75e) and what
  each showed; the Nano's waits for Optiboot by ISP. The per-board steps say
  which of them apply to the serial-only build.
