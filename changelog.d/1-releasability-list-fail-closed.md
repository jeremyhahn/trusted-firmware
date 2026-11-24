### Fixed

- **The public-releasability gate no longer passes the tree on a pattern list
  it cannot read in full** (#1). `scripts/releasability.sh` now reads a last
  rule with no final newline, and exits 2 on a regex `grep -E` refuses
  instead of letting that rule match nothing.
