### Fixed

- **A host test now reaches the eat_nonce, alias-key and claims checks of
  `VerifyDICEAttestation`** (#590). It builds its own DICE chain, so each
  refusal past the alias signature is tested: an alias certificate minted for
  another challenge, a non-ECDSA alias key, and each malformed claims set.
