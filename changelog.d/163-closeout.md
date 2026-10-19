### Changed

- **The Go coverage floors hold per package** (#163). `make sdk-coverage` and
  `make xkms-coverage` used to fail only when the module total was under 90 %.
  A well-covered package could carry a thin one past that. They now also fail
  when any one package is under the floor, which is the owner's standard (m655).
  `docs/development/testing.md` records where each part of that standard is met,
  with the counts before and after: sleeps in test files went from 16 to 0, and
  the simulator-driven tests left the lower-layer packages for `tfwctl` E2E.
