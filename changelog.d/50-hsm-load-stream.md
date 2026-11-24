### Added

- **The SDK's HSM_LOAD chunk stream is checked against the M7's reassembly
  rules** (#50). A unit test runs every envelope length from 1 to 1024 bytes
  through a model of the M7's reassembly, so a trailing empty chunk or an
  oversize chunk fails on the host and not on the device. The A6 record
  read-all is tested on a record that grows, shrinks or answers short while it
  is read.
