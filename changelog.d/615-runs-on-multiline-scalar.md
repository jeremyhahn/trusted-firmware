### Fixed

- **The runner-label test reads a runner label value that spans lines before
  it checks it** (#615).
  `scripts/test_runner_labels.py` used to report the header of a block scalar,
  `runs-on: >-` then `codex`, as the unknown label `>-`, and an expression
  folded over lines, `runs-on: ${{ matrix.os` then `}}`, as its pieces. It now
  decodes a `>`/`|` block scalar and joins a plain or quoted scalar's
  continuation lines, as the `runs-on:` value, a `labels:` value, an empty
  key's next-line value or a list item, and reports a label at its key's line;
  an expression is skipped whole, and the next job's `runs-on:` is still read.
