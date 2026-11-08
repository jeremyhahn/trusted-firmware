### Fixed

- **The Devcontainer image build resolves every module from the public
  proxy.** Its `go mod download` step stopped at
  `github.com/jeremyhahn/arduino-generic/sdk/go`, a private module that
  `xkms` pulled in only through go-xkms v0.64.1-alpha, and asked for a
  credential the hosted runner does not have. `xkms` now requires go-xkms
  v0.64.2-alpha, built on the public trusted-firmware SDK, so no private
  module is left in the graph and the step needs no module credential.
