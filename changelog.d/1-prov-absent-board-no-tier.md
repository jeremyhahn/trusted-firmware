### Fixed

- **`prov-absent-check` no longer passes over a board for which make prints
  no crypto tier** (#1). An empty `make board-tiers` answer left the board
  with no image planned, not even the example application, and the gate
  still passed on the other boards; such a board is now an error naming it.
