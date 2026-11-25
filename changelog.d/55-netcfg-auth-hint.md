### Added

- **Unit tests for the `tfwctl netcfg set` operator hint** (row
  `ethernet-transport`). `TestNetcfgSetNamesTheOperatorInputsWhenTheWriteNeedsAuth`
  and `TestNetcfgSetPassesOtherWriteRefusalsThrough` pin
  `sdk/go/cmd/tfwctl/netcfg.go:47-48` in the default `go test`. An auth refusal
  names `--serial` and `--operator-key` and still wraps the device's `Status`.
  Any other write refusal passes through without the hint.
