### Fixed

- **`VerifyDICEAttestation` refuses a UEID of the wrong shape** (#590). A
  claim 256 that is not 17 bytes starting 0x01 (the RAND type byte, then the
  16-byte device id) is refused as "the UEID is not 0x01 and a 16-byte device
  id" instead of verifying with an identifier the device never writes.
