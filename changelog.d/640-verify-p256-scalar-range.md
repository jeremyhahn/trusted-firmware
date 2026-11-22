### Added

- **The tenant-key verify's scalar-range refusals have tests** (#640).
  `test/unit/test_verify_p256.c` now proves `tfw_crypto_verify_operator`
  refuses a signature whose r or s is zero or equal to the P-256 group order n
  (FIPS 186-5 section 6.4.2 step 1), so a provider change that dropped the
  check fails the suite.
