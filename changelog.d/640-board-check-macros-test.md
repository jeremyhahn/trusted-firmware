### Added

- **`scripts/test_board_check_macros.py`, a test of board-check's `#define` and macro readers** (#640): comments stripped, the first `#define` wins, only the first branch of an `#if` group counts at every depth, and macros expand longest name first with argument count and nesting checked. `make scripts-test` runs it.
