### Fixed

- **The runner-label test keeps a quoted label with a comma or bracket inside
  a `${{ }}` expression whole** (#615). `scripts/test_runner_labels.py` now
  splits a flow list or a flow mapping only on its top-level commas, so
  `runs-on: [self-hosted, "${{ format('runner-{0}', matrix.arch) }}"]` and
  `{group: g, labels: "${{ format('a', b) }}"}` are read as one expression
  label each, and skipped, where it used to split the label at the comma (or
  end a `labels:` list at a `]` inside `"${{ matrix.os[0] }}"`) and report the
  fragments as unknown labels.
