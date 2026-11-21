### Added

- **`scripts/test_release_alpha_series.py`, a test of `make release-alpha`'s tag arithmetic** (#629): a series keeps only its own prefix's versions, the next alpha bumps PATCH of the newest plain -alpha by semver precedence, an equal tag is refused as non-monotonic, and no alpha at all is refused.
