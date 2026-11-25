### Fixed

- **`scripts/govulncheck-gate.py` refuses an OSV id listed twice for one
  module** (#640). It kept the last of the two entries, so the gate printed
  only the later reason and the earlier reviewed one was silently dropped; it
  now fails naming both line numbers, as it fails an entry with no reason. The
  same id under two modules is still read for each.
