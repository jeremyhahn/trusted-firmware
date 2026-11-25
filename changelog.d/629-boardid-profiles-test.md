### Added

- **`sdk/go/boardid_profiles_test.go`, a test that `tfw.BoardID` mirrors the firmware's board ids** (#629): every `boards/*/board_profile.h` defines `TFW_BOARD_ID` once, its `String()` is the board's directory name and no two boards share an id; every id `tfw.BoardID` knows has a board directory.
