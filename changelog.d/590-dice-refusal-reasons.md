### Fixed

- **The DICE evidence refusal tests now assert which check refused** (#590).
  Each `DICEAttestationObject` refusal case names its reason, and a nonce or
  signature one byte too long is refused, so the input-length checks cannot be
  deleted unseen.
