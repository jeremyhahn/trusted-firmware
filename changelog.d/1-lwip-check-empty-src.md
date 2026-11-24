### Fixed

- **`lwip-upstream-check` no longer passes a vendored lwIP with no file
  under `src/`** (#1). Such a tree compared only `COPYING` and `README` and
  passed, or skipped as "no network" when offline; it is now a FAIL, found
  before any fetch is tried.
