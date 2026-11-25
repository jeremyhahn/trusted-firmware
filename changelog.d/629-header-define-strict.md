### Changed

- **The SDK's header pins read only a whole literal `#define` value** (row `host-sdk`): an expression such as `2u + 1u` in an `include/tfw` header now fails the Go test instead of matching its first number, and a `TFW_CMD_*` added to `protocol.h` in any spelling, decimal included, with no SDK `Cmd` fails it by name.
