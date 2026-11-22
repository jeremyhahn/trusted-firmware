### Added

- **tfwctl tests for a refused `hsm-load`** (#50). `hsm-load`, `hsm-report`
  and `hsm-verdict` return the device's refusal and a dead link with no state
  printed; a two-chunk envelope the device refuses with `StatusErrCrypto` on
  the last chunk returns that status; an empty envelope file or one over
  `HSMEnvelopeMax` returns the SDK's `ArgRangeError` before any frame is sent.
