### Fixed

- **`VerifyDICEAttestation` refuses nil roots** (#590). A nil
  `*x509.CertPool` no longer means the host's system CAs: the verifier refuses
  it with "no trusted roots for the DeviceID certificate", so a DeviceID
  certificate under a public CA cannot pass the chain check.
