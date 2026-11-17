### Fixed

- **A hardware row on a `tcp:` PORT no longer hangs** (#551, #536): the conn
  the test peer dials for a `tcp:` PORT keeps a 200ms read floor, as a serial
  PORT does, so a row that wraps the conn and hides its read deadline (the
  CAPS row that clears the ATTEST_CSR bit) gets a timeout, not a read that
  blocks until the test binary's 10-minute timeout, while it waits for a
  BOOT event a running board never sends.
