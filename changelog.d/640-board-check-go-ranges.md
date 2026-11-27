### Added

- **board-check compares the board-id registry's Ranges table with
  `sdk/go/boardid.go`'s `Family()` ranges** (#640). A range or family name the
  two do not share, a `Family()` case on a constant the file does not define,
  or a `Family()` with no range case the check can read fails. `make
  scripts-test` runs it.
