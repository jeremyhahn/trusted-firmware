### Fixed

- **The xkms adapter's open evidence choice follows the mint flow** (#629).
  `docs/xkms-adapter/engineering.md` no longer says the alias certificate is
  built at boot: each `ATTEST_CERT` mints it over the Verifier's nonce, and
  `ATTEST_CSR` reuses the measurement that mint kept, so the owner's choice is
  when the record digest is taken and whether the UCCS template may grow.
