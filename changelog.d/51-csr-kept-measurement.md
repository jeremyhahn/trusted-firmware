### Changed

- **`ATTEST_CSR` signs the measurement its certificate carries** (#51). The
  request's claim 273 is now the 32 bytes the `ATTEST_CERT` mint for the same
  nonce placed and kept in RAM, not a fresh `tfw_app_evidence` call, so an
  application whose evidence changes between the two commands no longer makes
  go-xkms refuse the possession signature. `b` with no certificate minted for
  it, including after a reset, answers `TFW_ERR_BAD_ARGC`, as an `ATTEST_CERT`
  page request with no certificate behind it does. No status code is added
  and no RAM: the mint already kept the nonce and measurement for its pages.
