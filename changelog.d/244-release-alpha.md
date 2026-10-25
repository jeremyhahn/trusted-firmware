### Added

- **`make release-alpha` cuts the next internal alpha** (#244). It refuses
  unless the checkout is a clean `main` equal to `dev/main`, increments PATCH of
  the newest `vX.Y.Z-alpha`, refuses unless the new version sorts above every
  tag of the `v…` and `sdk/go/v…` series, cuts the annotated, signed tags
  `vX.Y.Z-alpha` and `sdk/go/vX.Y.Z-alpha` on HEAD with git's configured key,
  pushes both to `dev`, never forced, and prints the `consumer-check` step.
  `xkms/` keeps its own series. `DRY_RUN=1` prints the tags and changes nothing.
  `scripts/release_alpha.py`; `docs/development/releasing.md`; pinned by
  `scripts/test_release_alpha.py` against a scratch repository, no network.
