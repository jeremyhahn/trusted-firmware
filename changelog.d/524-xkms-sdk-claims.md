### Changed

- **The `xkms/avr` adapter reads claim 273 through `sdk/go`** (#524). After
  `dice.Verify` accepts the alias chain, `AttestKey` decodes the measurement
  with `tfw.AliasUCCS` and `tfw.ApplicationEvidence`, the decoders of the
  module that owns the claim format, and reports it as
  `Attestation.ApplicationEvidence`. A claim 273 that `sdk/go` refuses (absent,
  not one "app" file, not a 32-byte SHA-256 value) now refuses the attestation
  with `sdk/go`'s `*tfw.EvidenceError` (`tfw.ErrBadFormat`). No version
  changes: the pinned `sdk/go` v0.4.2-alpha already exports both.
