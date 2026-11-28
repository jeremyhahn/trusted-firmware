### Fixed

- **The runner-label test reads only job runners, past a stray quote** (#615).
  `scripts/test_runner_labels.py` opened a quoted scalar at a quote inside a
  plain scalar (`env: {NOTE: say "hello}`) and so skipped the rest of the
  file, read a `runs-on` action input or `workflow_call` input as a runner,
  and folded a flow label's escaped line break into a blank. It now opens a
  quote only at a value start, reads only `jobs.<job>.runs-on`, and decodes a
  multi-line flow label from its raw lines.
