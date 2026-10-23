### Changed

- **`AttestKey` gates on the cached `CAPS`** (row `xkms-adapter`). Where `CAPS`
  says the device cannot attest, the `xkms/avr` backend now refuses before it
  sends a frame, so the call agrees with `Capabilities().Attestation`. A
  firmware built without `FeatureAttest` is `ErrCommandUnavailable`, a build to
  change. One with `FeatureAttest` but without the alias certificate on a P-256
  signer is `ErrUnsupportedByDevice`, a part or tier to change. A device whose
  `CAPS` says it can attest still goes to the wire, and its own refusal maps as
  before. `Capabilities().Attestation` now also requires `FeatureAttest`.
