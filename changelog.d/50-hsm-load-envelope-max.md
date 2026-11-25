### Fixed

- **A host test sends HSMLoad its largest envelope** (#50): no test sent `HSMEnvelopeMax` (1024 bytes), so an off-by-one refusal of the bound, or a chunk total or offset written as one byte, passed every test. The new test sends 1024 bytes and checks all 18 frames carry total 1024 and their offset's high byte. Tests only.
