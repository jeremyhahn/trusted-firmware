### Fixed

- **Host tests pin that each STM32H5 `tfw_crypto_init` re-decides the PKA**
  (#599), so a PKA that timed out is used again and one that no longer starts
  is left to software.
