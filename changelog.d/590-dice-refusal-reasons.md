### Fixed

- **The DICE refusal tests now assert which check refused** (#590). Each
  `VerifyDICEAttestation` and `DICEAttestationObject` refusal case names its
  reason, an object labelled alg EdDSA is refused, and a nonce or signature one
  byte too long is refused, so the authData, alg and input-length checks cannot
  be deleted unseen.
