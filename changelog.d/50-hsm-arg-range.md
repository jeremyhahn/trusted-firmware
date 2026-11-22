### Fixed

- **The SDK's `HSMVerdict` refusal was tested at the single verdict 2**
  (#50, `sdk/go/hsm.go`). A check of `verdict == 2` passed every test while
  verdicts 3..255 would reach the wire, and neither the verdict nor the
  `HSMReport` signal refusal had its reported Arg, Min or Max checked. New
  tests refuse verdicts 2, 3, 0x7F and 0xFF and signals 0, 3, 6 and 0xFF,
  each with its argument name and range and nothing sent.
