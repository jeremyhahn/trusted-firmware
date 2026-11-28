### Fixed

- **The runner-label test reads a quoted label as written and reports a
  `runs-on:` key inside a flow-mapping job** (#615).
  `scripts/test_runner_labels.py` now unquotes a label once, after it handles
  an anchor, tag, alias or comment on the raw text, so `runs-on: "*qwne"`
  reports `*qwne` (it was skipped as an alias, in every scalar, list and
  `labels:` form), and `"linux # qwne"`, `"&a linux"` and `"'linux'"` report
  as written (they were cut to `linux`). A job written as a flow mapping,
  `jobs: {build: {runs-on: qwne}}`, used to be skipped; its `runs-on` key now
  fails the test as `<runs-on in a flow mapping>`.
