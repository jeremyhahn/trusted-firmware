### Fixed

- **`scripts/cli-doc-check.py` reads every default-FlagSet definer** (#1). A
  tfwctl flag defined with `flag.Int64Var`, `flag.UintVar`, `flag.Uint64Var`,
  `flag.Float64Var`, `flag.TextVar` or `flag.BoolFunc` was never read, so
  `docs/host-sdk/cli.md` could leave it out and the check still passed. No
  shipped flag was missed: tfwctl uses only `flag.Bool`, `flag.Duration` and
  `flag.Int` today.
