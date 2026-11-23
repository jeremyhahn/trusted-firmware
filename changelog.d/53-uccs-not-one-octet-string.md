### Fixed

- **`AliasUCCS`'s refusal of a UccsEvidence that is not one OCTET STRING is pinned by a host test** (#53): `sdk/go/appevidence_uccs_test.go` now feeds `AliasUCCS` an empty SEQUENCE, a SEQUENCE of an INTEGER, a SET, a context-specific `[0]` and `[16]`, and a primitive SEQUENCE, and requires the reason "tcg-dice-UCCS-evidence is not a UccsEvidence" with no claims for each, so dropping the class, tag or constructed check, or returning the SEQUENCE body on a failed element decode, fails a test; a one-element UccsEvidence still returns its `uccs` byte for byte.
