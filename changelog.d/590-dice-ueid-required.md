### Fixed

- **`VerifyDICEAttestation` refuses a claims set with no UEID** (#590). An
  alias certificate whose claims set has no claim 256, or a claim 256 that is
  not a byte string, is refused as "no UEID (claim 256)" instead of verifying
  with a nil UEID: the evidence must name the device it attests.
