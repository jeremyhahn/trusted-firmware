### Added

- **`ApplicationEvidence` and `AliasUCCS` refusals each assert their own reason** (#629): `sdk/go/appevidence_reasons_test.go` pins the exact `EvidenceError.Reason` of every refusal, so a reason swapped for another, or a dropped check that lets a later one fire, fails.
