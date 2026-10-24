### Added

- **Every exported symbol of the Go SDK on one page** (row host-sdk).
  `docs/host-sdk/api.md` gains an `## Exported symbols` section listing all 347
  exported functions, types, methods, constants and variables of `tfw`,
  `serialport` and `tfwsim`, build-tag-gated files included, each with its
  signature and the first sentence of its doc comment. That includes
  `tfwsim.StartProvisioningWith`, `StartElementApp`, `StartElementVia` and
  `StartElementEthernet`, which the page did not name before.
  `scripts/api-doc-check.py`, run by `make scripts-test`
  (`scripts/test_api_doc_check.py`), fails when a symbol is exported and has no
  row, or when a row or a `pkg.Name` on the page names one that does not exist.

### Changed

- **Doc comments on the SDK's exported symbols** (row host-sdk). The `Error`,
  `Is`, `Unwrap` and `String` methods and the constant groups that had none
  now carry one; `TimerSet`'s names its parameter `dur`.

  Coverage did not change (`go test -tags tfwsim -cover ./...` in `sdk/go`):
  `sdk/go` 99.9%, `cmd/tfwctl` 98.6%, `cmd/tfwprov` 95.1%, `internal/peer`
  100%, `internal/simproc` 95.0%, `serialport` 100%, `tfwsim` 91.7%. Every
  public package was already at or above 90%, so no test was added.
