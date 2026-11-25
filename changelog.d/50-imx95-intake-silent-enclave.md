### Fixed

- **The i.MX 95 HSM intake's S2 poll is now pinned for an enclave that never
  answers** (#50). An `hsm_get_event` that times out raises nothing and keeps
  the open session, so the next pass polls that same session without a
  second `SESSION_OPEN`. A session open that times out leaves no session,
  polls nothing, and is sent again on the next pass. The unit tests' enclave
  model can now stay silent on a chosen request, so a change that dropped
  the session or raised S2 on a timeout fails them.
