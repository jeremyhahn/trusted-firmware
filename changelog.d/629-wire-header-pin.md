### Changed

- **The SDK's wire constants are pinned to the firmware headers** (row `host-sdk`): a Go test holds the SDK's frame constants, protocol version and every non-HSM command id to `include/tfw/protocol.h` and `TFW_MAX_PAYLOAD` in `include/tfw/config.h`, and a command added to the header with no SDK id fails it by name.
