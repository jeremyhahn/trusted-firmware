### Added

- **Host tests prove a refused or error-flagged STM32H5 PKA sign wipes d and k**
  (Refs #599). After the PKA refuses a sign, and after each SR error flag stops
  one, the private key and nonce operands in the PKA RAM read back as zero and
  the caller's signature buffer is left alone.
