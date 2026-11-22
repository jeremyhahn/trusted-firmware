### Fixed

- **xkms-adapter and device-onboarding docs match the code** (row `host-sdk`,
  #629). The `tfwprov --commit` command in the onboarding guide puts
  `--yes-i-mean-it` before `<port>`, so it no longer prints usage; the
  `xkms/avr` pages, package comments and `go.mod` comment say the backend is an
  `AttestingKeyProvider` that calls `dice.Verify`, name the
  `ErrSigningUnsupported` and `ErrInvalidAttestation` refusals it actually makes,
  and cite the go-xkms version required; the onboarding pages cite the files
  that hold the 328P `ATTEST_CSR` exclusion, the Ethernet id allow-list, the
  `TFWV_APPEV_WITH_UCCS` vector and `PROV_WRITE_PUBKEY` (`0x7B`).
