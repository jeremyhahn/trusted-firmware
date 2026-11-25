### Changed

- **The i.MX 95 port's A2 stops the A55's logical machine** (#50). It asks
  the System Manager for LMM_SHUTDOWN, forceful, where the board names the
  LM's id, and falls back to switching the A55's power domain off when the
  stop is not acknowledged; the board's id is zero, so nothing changes on the
  board yet.
