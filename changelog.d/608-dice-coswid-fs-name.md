### Fixed

- The SDK's DICE verifier takes the measurement only from the one "app" file, as ApplicationEvidence reads it; a signed alias whose CoSWID names another file, or none, is refused; `tz_s_api.h` no longer declares the retired `tfw_s_version` veneer (#608).
