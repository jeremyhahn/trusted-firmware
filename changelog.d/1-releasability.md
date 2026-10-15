### Added

- **`make releasability`** (#1) fails on a private-fleet address, hostname,
  owner path or owner account name in any file that would be published (the
  tracked files minus the `export-ignore` entries in `.gitattributes`), and
  prints `file:line: class` for each. `make releasability-test` proves it on
  planted values and proves `git archive` drops the agent files and the
  forge-only workflows. Both are part of `make ci`.
