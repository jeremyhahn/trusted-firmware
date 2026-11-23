### Changed

- **Host SDK requirement 15 is marked met** (row host-sdk, #629).
  `docs/host-sdk/engineering.md` said every verb on every link was on open
  PR #180; that PR is on `main`. The row now names
  `sdk/go/cmd/tfwctl/e2e_links_test.go`, its `TestE2ELinks*` tests, which run
  each command family on serial and on TCP, and the `ethAdmits` allow-list
  that mirrors `tfw_link_admits` in `src/proto/link_eth.c`.

### Added

- `TestE2ELinksHSM` in `sdk/go/cmd/tfwctl/e2e_links_test.go` runs the seven
  `hsm-*` verbs on serial and on TCP against the element image and checks
  that a verb refused over TCP never reaches the HSM, so requirement 15
  covers the HSM channel.
