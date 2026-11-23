### Fixed

- **`docs/dice-attestation/user.md` says the evidence's DeviceID and FWHash are
  live** (#629): the platform serial and tfw-boot's handoff measurement, zero
  only where their source is absent. The tfwsim serial, the per-board tier
  defaults, `make firmware-attested`, the `tfwctl attest` verb and the
  `AttestCSR` refusal rows follow main.
