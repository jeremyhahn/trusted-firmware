### Fixed

- **The caps-bit refusal row runs on TCP boards again** (#48): the
  `esp32p4-poe` run selects the row that clears the ATTEST_CSR bit in CAPS and
  expects the host to refuse ATTEST_CSR as unsupported. The meddler that
  rewrites CAPS passes the read deadline through, and a target that does not
  reset is opened with no BOOT wait, so the row no longer waits for a BOOT a
  TCP connect never sends. A host test drives it over a pipe that sends no
  BOOT. The bench run on a P4 is still to come.
