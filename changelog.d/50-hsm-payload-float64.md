### Fixed

- **A float64 in a tamper-response policy set or script is refused as a type
  the CDDL lacks** (#50). The payload reader returned `TFW_ERR_RANGE` for a
  float64 whose high bytes were not zero and `TFW_ERR_UNSUPPORTED` otherwise;
  it now returns `TFW_ERR_UNSUPPORTED` whatever the value, as
  `src/core/hsm.h` says. An integer past 32 bits is still `TFW_ERR_RANGE`.
