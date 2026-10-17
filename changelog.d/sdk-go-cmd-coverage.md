- `tfwctl` and `tfwprov` are under test on their argument and refusal paths: every subcommand's bad and
  missing arguments, an unknown subcommand, device refusals on `tfwsim`, `tfwprov`'s template refusals, a commit
  that fails part way, and both tools' exit codes (2 for usage, 1 for a failure). Statement coverage: `cmd/tfwctl`
  56.5% → 79.0%, `cmd/tfwprov` 39.3% → 86.8%.
- Fixed: `tfwprov --commit` on a part whose data zone is locked printed `tfwprov: tfwprov: the data zone is already
  locked; ...`; the prefix is now printed once.
- Fixed: `tfwprov --dry-run` on a locked config zone warned that `--commit` would fail at the config lock; it now
  says `--commit` skips the config steps, and on a locked data zone that `--commit` refuses the part.
