### Fixed

- **The public-releasability gate scans a tracked symlink's target** (#1).
  `git archive` publishes a symlink as its target text, which
  `scripts/releasability.sh` used to skip, so a link to an owner path passed
  the gate. A match is now reported as `link: class (symlink target)`.
