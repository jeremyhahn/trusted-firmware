### Changed

- **sdk/go builds and parses DICE evidence; go-xkms verifies it** (#53). `DICEAttestationObject` builds the "dice" object and `ParseDICEAttestationObject` reads it back; no sdk/go verifier.
