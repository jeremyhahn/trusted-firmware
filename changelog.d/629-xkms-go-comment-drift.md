### Fixed

- **`xkms` Go doc comments match the code** (#629). The `xkms/avr` package and
  the `xkms` re-exports now name the right sentinel and method for each refusal:
  a non-SHA-256 digest is `ErrSigningUnsupported`, `ErrInvalidAttestation` lists
  its three checks, `ErrNoDeviceKey` covers an unclassified crypto tier, and
  `KeyCN` is answered by `GetKey` and `AttestKey`. `AttestKey`'s check order,
  `AVRAttestationConfig`'s `PermittedFlags` and `AVRAttestation`'s fields are
  described as they are.
