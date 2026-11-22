### Fixed

- **The Sys row's reset-cause test pins the selection the row uses** (#629).
  `TestSysRowCausesFollowTheTargetsReset` drives `peerBoard.causes`, the
  causes `TestFirmwareSys` hands `sysRow`: the ESP32-P4 over TCP accepts a
  software and a power-on restart and not a watchdog or panic one, a serial
  board reset by Start accepts only its own cause, and a board always reset
  accepts the same either way. The `resetCauseHolds` helper the row never
  called is removed (Codex P2 on #568).
