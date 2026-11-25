### Added

- **test_hsm_loop pins one pass's order and each line's own level** (#50).
  A pass past the heartbeat deadline with S1 rising locks down for S6 before
  S1's rule runs, and a line released in the pass another rises raises only
  the one that rose.
