### Added

- **`Device.AttestCSR`** (#31). `AttestCSR(ctx, b [AliasNonceLen]byte)` returns the
  64-byte `r‖s` signature the device's DeviceID key made over its
  CertificationRequestInfo (`ATTEST_CSR`, #30), as a `[CSRSigLen]byte`.
  - Transport only. This repo does not own certificate profiles: go-xkms
    reproduces the 365-byte CertificationRequestInfo itself, verifies the
    signature over it and builds the PKCS#10 request (go-xkms#1441). `sdk/go`
    holds no template, no ASN.1, no OIDs and no Go mirror of the CSR vectors; the
    C vectors and C tests stay and pin what the device emits.
  - `b` is the same 32 bytes handed to `AttestCert`. No alias certificate is
    minted: the call is `CAPS`, then `ATTEST_CSR`.
  - A device whose CAPS do not report `FeatureAttestCSR` is refused with
    `StatusErrUnsupported`, wrapped, before any attestation command is sent. A
    reply of any length but 64 is a `*ShortReplyError`.
    `sdk/go` still has no in-house dependencies.
- **`FeatureAttestCSR`** (#31), CAPS feature bit 15, the `sdk/go` name for the
  firmware's `TFW_FEATURE_ATTEST_CSR`, and **`CSRSigLen`** (64), the length of the
  reply.
- **`tfwctl attest-csr --nonce <hex32>`** (#31). Prints the signature as 128 hex
  digits on stdout, or `{"signature": <hex>, "len": 64}` with `--json`. The nonce
  is a flag, not a positional argument.
- **`tfwsim.StartElementVia`** (#31). `StartElement` with the connection to the
  simulator passed through a `wrap` function before the `Device` sees it, so a
  test can stand between a host and a real device and rewrite a frame in flight.
- Documentation of `Device.AttestCSR` and `tfwctl attest-csr` in
  `docs/dice-attestation` (`api.md`, `cli.md`, `user.md`, `engineering.md`) and
  `docs/host-sdk/api.md`.
