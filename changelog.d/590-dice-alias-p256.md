### Fixed

- **`VerifyDICEAttestation` refuses an alias key that is not P-256** (#590).
  An alias certificate whose ECDSA key is on another curve, such as P-384, is
  refused as "the alias key is not P-256 (ES256)" instead of being checked on
  its own curve, though the object's alg is ES256 and the device's alias key
  is always P-256.
