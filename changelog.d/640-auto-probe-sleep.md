### Added

- **A test for the auto provider's probe sleeping the element after a probe that
  answers** (row #640). When the element's own init then cannot wake the part,
  the test checks that the ATECC608A is asleep, so deleting that sleep now fails
  `test_crypto_auto_mutate`.
