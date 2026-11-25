### Changed

- **The SDK's crypto tiers, CAPS feature bits, algorithm bits, pin flags, network block and image trailer constants are pinned to `include/tfw`** (row `host-sdk`): a Go test holds them to `types.h`, `config.h`, `protocol.h`, `board.h`, `eth.h` and `boot.h`, and a CAPS bit added to `config.h` with no SDK `Feature` constant fails it by name.
