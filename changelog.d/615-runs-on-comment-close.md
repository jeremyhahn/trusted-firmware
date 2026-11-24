### Fixed

- **The runner-label test strips a ` #` comment and closes a multi-line flow
  only outside a quoted label or `${{ }}` expression** (#615).
  `scripts/test_runner_labels.py` now reads
  `runs-on: [self-hosted, "${{ contains(' # ', inputs.x) && 'linux' || 'x64' }}", qwne]`
  and a `{group: "${{` mapping whose `}}` ends a later line inside the open
  quote as whole values, so it reports `qwne`. A `#` or `}}` inside a quoted
  label or an expression used to end the value early and hide the unknown
  labels after it, and a quoted label `"a #b"` was cut to `a`.
