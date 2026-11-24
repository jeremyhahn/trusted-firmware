### Fixed

- **`make lwip-check` fails on a symlink to a directory in the vendored tree** (#1):
  `scripts/vendor_sums.py` skipped it, so an unlisted one passed the check; it
  is now a tree entry the list must name, and a listed one cannot match.
