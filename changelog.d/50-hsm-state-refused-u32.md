### Fixed

- **`HSMState.Refused` is pinned over all four bytes** (row `hsm-self-defence`).
  The SDK's only decode vector had zero upper bytes in `refused` and no locked
  or wiped flag, so a decoder that read two bytes, the wrong offset or a masked
  `flags` passed. A new test decodes a reply with every flag set and a distinct
  value in each byte of `refused` through every HSM state verb.
