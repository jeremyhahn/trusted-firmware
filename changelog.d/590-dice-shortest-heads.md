### Fixed

- **`VerifyDICEAttestation` refuses a CBOR head that is not the shortest
  form** (#590). The "dice" object has one encoding, the one
  `DICEAttestationObject` writes: a long-form head, such as `0xb8 0x03` for
  the top map or `0x59 0x00 0x20` for the authData length, is refused as
  "not a dice attestation object" instead of verifying.
