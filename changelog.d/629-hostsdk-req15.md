### Changed

- **Host SDK requirement 15 is marked met** (row host-sdk, #629).
  `docs/host-sdk/engineering.md` said every verb on every link was on open
  PR #180; that PR is on `main`. The row now names
  `sdk/go/cmd/tfwctl/e2e_links_test.go`, its `TestE2ELinks*` tests, which run
  each command family on serial and on TCP, and the `ethAdmits` allow-list
  that mirrors `tfw_link_admits` in `src/proto/link_eth.c`.
