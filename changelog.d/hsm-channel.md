### Added

- **The HSM channel commands, `0x76` to `0x7A`** (row `hsm-self-defence`,
  slice 4). `HSM_HEARTBEAT`, `HSM_REPORT` (S4 or S5), `HSM_VERDICT`,
  `HSM_SELECT` and `HSM_LOAD` carry the A55's messages to the decision engine
  on the wire protocol, and each replies with the engine's state:
  `[flags][selected][refused:4]`, the flags being set held, locked and wiped.
  `HSM_LOAD` takes a signed policy envelope in chunks of up to 60 bytes into a
  1024-byte buffer and verifies it under the tenant key on the last chunk; a
  total past the buffer is `TFW_ERR_RANGE` and a signature that does not verify
  is `TFW_ERR_CRYPTO`, both counted in `refused`. `src/core/hsm_cmd.c` is
  linked only where `TFW_FEATURE_HSM` is on, today tfwsim-atecc608a; every
  other image answers `TFW_ERR_UNKNOWN_CMD`, so it is unchanged on the wire.
  Literal vectors pin the frames in `testdata/vectors.h` and its Go mirror.
- **`sdk/go`: `Device.HSMHeartbeat`, `HSMReport`, `HSMVerdict`, `HSMSelect`,
  `HSMLoad` and `HSMState`**, with `CmdHSM*` and the flag constants.
  `hsm_tfwsim_test.go` runs them against tfwsim-atecc608a, from a verified
  load to an S4 lockdown.
