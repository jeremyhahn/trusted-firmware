### Fixed

- **`AliasUCCS` reads a UccsEvidence of exactly one `uccs`, as go-xkms does** (#53): `sdk/go/appevidence.go` refused bytes after the `tcg-dice-UCCS-evidence` SEQUENCE but returned the claims of a SEQUENCE that carried a second element after its one OCTET STRING, which go-xkms's `dice.ParseUccsEvidence` refuses; it now refuses that value with the reason "UccsEvidence carries more than its one uccs", so `tfwctl`'s alias output and the verifier read one certificate the same way.
