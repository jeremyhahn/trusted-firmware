### Added

- **The Go SDK's error paths under test** (#75, #7). `sdk/go/errorpaths_test.go`
  pins the refusals an importer meets: an unknown status or OTA state or
  crypto tier reads as unrecognised and never as a known value; OTA, provision,
  attest and auth argument and short-reply refusals, a refused later page, a
  failing or malformed `crypto.Signer`; network-block validation through
  `WriteNetConfig`; a bad listen address and a closed TCP peer; resync past a
  corrupt frame. No wire vector changed, no dependency added, no defect found.

  Statement coverage, `go test -cover ./...` in `sdk/go` with the host
  simulators built (`make tfwsim tfwsim-atecc608a tfwsim-app-example`; the
  `TestAgainst*`, `TestElement*`, `TestSoftwareTier*` and `tfwsim` package tests
  need them and run in `make sdk-test`; without them the total is 46.0%):

  | package | before | after |
  |---|---|---|
  | `sdk/go` | 73.2% | 88.5% |
  | `cmd/tfwctl` | 56.5% | 56.5% |
  | `cmd/tfwprov` | 39.3% | 39.3% |
  | `internal/peer` | 65.5% | 65.5% |
  | `internal/simproc` | 29.4% | 29.4% |
  | `serialport` | 0.0% | 0.0% |
  | `tfwsim` | 84.8% | 84.8% |
  | **total** | **59.6%** | **65.5%** |

  Largest uncovered functions left: `cmd/tfwctl/main.go:145` `run`,
  `cmd/tfwprov/main.go:76` `main`, `cmd/tfwctl/peripherals.go:36`
  `runPeripheral`, `cmd/tfwprov/template.go:45` `loadTemplate`,
  `cmd/tfwprov/steps.go:98` `runCommit`; in the library, `commands.go:549`
  `PulseCapture` and `serialport/serialport.go:47` `Open`.
