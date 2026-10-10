### Added

- **`ATTEST_CSR`, command 0x64** (#30). Proof of possession of the DeviceID key
  at ACME finalize, option (c) of the possession ruling on #6.
  - Request `[b:32]`, response `[sig:64]`: `r‖s`, the DeviceID key's P-256
    signature over SHA-256 of a PKCS#10 `CertificationRequestInfo`. Any other
    request length is `TFW_ERR_BAD_ARGC`. One exchange: nothing is paged and
    nothing is kept, so it does not disturb an `ATTEST_CERT` in progress.
  - CAPS feature bit 15, `TFW_FEATURE_ATTEST_CSR`. Selected only beside
    `ATTEST_CERT`; a board profile can turn it off alone.
  - The device builds the request itself from a fixed template: 365 bytes, version
    0, an empty subject, its `SubjectPublicKeyInfo`, and an `extensionRequest`
    holding a critical subjectAltName `otherName` `id-on-permanentIdentifier`
    and the critical `tcg-dice-UCCS-evidence` extension carrying the claims set
    with `b` as the nonce. The host rebuilds the same bytes; it chooses `b` and
    nothing else.
  - The permanent identifier is the lowercase hex of the full 17-byte UEID, type
    byte included: 34 hex digits, per go-xkms#1441.
  - `src/caps/csr.c` is a second table for the one template walker,
    `src/caps/tmpl.c` (`tfw_tmpl_emit`, `tfw_tmpl_digest`), which the alias
    certificate now also goes through. No new static RAM; the key buffer doubles
    as the signature buffer.
- **Vectors** (#30), literals from an independent encoder, read back by OpenSSL:
  `TFWV_CSR_CRI` (365 bytes) and `TFWV_CSR_CRI_DIGEST` in `testdata/vectors.h`,
  beside `TFWV_ALIAS_PUBKEY` and `TFWV_UCCS` (151 bytes) in
  `testdata/uccs_vectors.h`. `test_csr` also pins the alias certificate beside
  them and checks that none of the three signed structures parses as another.
- Board docs, `docs/boards/atmega32u4.md`, recording which commands that part
  leaves out.

### Changed

- **Only the ATmega2560 element tier carries `ATTEST_CSR`** (#30). The ATmega328P
  profile turns it off by name because it puts that part's element image over
  its flash budget (16 884 against 16 384 bytes); its alias certificate stays.
  The ATmega32U4 profile turns it off by name under the owner's ruling on #6
  (2026-10-09). The ATtiny85 and the ATmega32U4 have no `ATTEST_CERT`, and the
  command follows it. Everywhere else 0x64 answers `TFW_ERR_UNKNOWN_CMD` and CAPS
  bit 15 is clear.
- **The host build no longer forces `ATTEST_CSR` on** (#30). CAPS bit 15 is set
  only where `ATTEST_CSR` is linked: the 2560 element tier and the simulator's
  element tier. `make board-sweep` pins this per board.
