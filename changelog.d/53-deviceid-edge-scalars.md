### Fixed

- **A test now pins that the P-256 DeviceID derivation and signer read all
  32 scalar bytes** (#53). A seed in range at its edges, one with a zero
  first byte, two, and n-16, is its own scalar, gives micro-ecc's point and
  signs a signature that verifies. Before, a scalar check that read only the
  first byte, or skipped the last, passed every test; the first would have
  given the 1 seed in 256 with a zero first byte a different DeviceID.
