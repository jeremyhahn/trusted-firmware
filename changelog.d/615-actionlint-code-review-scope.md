### Changed

- `make lint-actionlint` ignores shellcheck SC2015 (`A && B || C`) in
  `.github/workflows/code-review.yml` only, through a `paths:` entry in
  `.github/actionlint.yaml`. The file is the handbook's code-review template
  and is not edited in this repo; the fix belongs upstream. No other workflow
  carries an ignore, and shellcheck stays on for every workflow, so any other
  finding still fails the lint. `scripts/test_lint_actionlint.py` pins the
  scope. (Refs #615)
