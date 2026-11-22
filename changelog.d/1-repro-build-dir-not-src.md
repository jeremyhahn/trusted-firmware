### Fixed

- **`scripts/repro_build.py` refuses a `--dir` that is, or holds, the source
  tree** (#1). Each `--dir` is removed before the clean copy goes in, so such
  a directory used to take the whole checkout with it, uncommitted work
  included; it now exits 2 before anything is removed.
