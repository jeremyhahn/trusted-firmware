### Added

- **End-to-end tests for the self-defence row's R1 lockdown and R4 lone
  verdict** (#50). On the element simulator, with no policy set held,
  `hsm-report ima` answers Locked, the A6 record holds the signal and the four
  lockdown actions (PHY off, power off, keys lock, record) and the heartbeat
  stays Locked; `hsm-verdict compromised` alone is one decision entry and
  locks nothing.
