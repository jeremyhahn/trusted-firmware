### Added

- **The policy payload reader and the decision engine** (row
  `hsm-self-defence`, slice 2). `src/core/hsm_payload.c` reads ADR D5's policy
  set and D6's script exactly, in deterministic CBOR only, and refuses an
  unknown key, signal or action or a non-deterministic encoding with
  `TFW_ERR_UNSUPPORTED` and a bound past its limit with `TFW_ERR_RANGE`.
  `src/core/hsm_engine.c` decides every row of the response table and R1 to
  R7: lockdown is the default and latched, a wipe needs a deterministic signal
  with a "compromised" verdict inside the window and never follows heartbeat
  loss, a script the engine does not hold locks down, and a set below the
  sequence floor, or at it with another digest, is refused with
  `TFW_ERR_PROTECTED_REGION`. Both are portable C with no HAL call and no
  dynamic allocation; the actions go through the port header
  `src/core/hsm_port.h`, and only the host tests build them until slice 3.
  `tfw_suit_integrated_digest` gives the manifest digest the floor compares.
  No new status, no wire change.
