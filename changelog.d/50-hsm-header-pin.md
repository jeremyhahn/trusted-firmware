### Added

- **The SDK's HSM channel constants are held to the firmware headers**
  (#50). A Go test reads protocol.h, config.h, hsm.h, hsm_record.h and
  suit.h and checks the SDK's HSM command ids, signals, verdicts, state flags
  and lengths, record kinds, load chunk and envelope maximum against them, so
  a renumbered command or a changed length fails plain `go test` instead of
  leaving the A55 sending what the M7 does not expect.
