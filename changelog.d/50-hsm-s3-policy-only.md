### Added

- **A host test that S3 is measured only when a policy set loads** (#50,
  `docs/hsm-self-defence/`). After a set with expected values, a script load
  and a refused envelope each leave the measurement count where the set left
  it, so a later load cannot raise S3, or lock down, on input that carries no
  measurement.
