### Changed

- **Bench inventory from fact** (#475). `docs/development/bench/inventory.md`
  now has one row per bench board (13) with four columns: connected now (the
  bench host's USB on 2026-11-16), probed, flashed with our firmware, and HIL
  passing, each cell citing its bench page section, board page or commit.
  Records only on an open PR's branch are marked pending. A "Confirmed
  working" line names the one board with a passing `make hw-test` (the Mega
  2560, since regressed) and a "Needs connecting" list names the boards with
  no adapter on the bench host. The FRDM-IMX95 is an explicit exception to the
  page's no-permission rule: nothing on it is written without the owner (CA
  ruling 583). The Pico 2 W's connection is unconfirmed, not "not connected":
  its absent ID is the BOOTSEL one. `docs/plans/475.md` holds the row's plan. No
  code changed.
