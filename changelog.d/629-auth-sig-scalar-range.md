### Fixed

- **`AuthResponse` refuses a signer's signature whose r or s is outside 1..n-1** (row `host-sdk`): a zero r or s, or one at or above the P-256 group order n, is an `*AuthSignError` and nothing is sent to the device.
