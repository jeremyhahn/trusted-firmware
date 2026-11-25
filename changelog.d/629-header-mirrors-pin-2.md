### Changed

- **The SDK's reset causes, attest/alias page sizes, signature length, HSM state flags and envelope max, timer and I2C bounds, default baud and element slots are pinned to their C headers** (row `host-sdk`): a Go test holds them to `types.h`, `protocol.h`, `crypto.h`, `config.h`, `src/core/suit.h` and `atecc.h`, and fails naming the macro when one drifts.
