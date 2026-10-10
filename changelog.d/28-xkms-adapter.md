### Added

- **The xkms adapter module** (#28). `github.com/jeremyhahn/trusted-firmware/xkms`
  is the AVR device as an xkms key provider, moved here from go-xkms's
  `pkg/backend/avr` rather than rewritten. It is a separate module from `sdk/go`
  on purpose: it imports go-xkms (Layer 3) and `sdk/go` (Layer 0), and `sdk/go`
  must stay free of in-house dependencies.
  - Provenance: the paths were imported verbatim, in their own commit, from
    go-xkms tag `v0.51.0-alpha` (`73ea01410dc9791c97dd2ae105945d26b1a12e0a`):
    `pkg/backend/avr/*`, `sdk/go/backends_avr*.go`,
    `docs/avr-device-backend/*` and `pkg/acme/client/device_attest_dice_e2e_test.go`.
    Later commits move them into `xkms/` (`avr/`, `backends_avr*.go`, `e2e/`) and
    `docs/xkms-adapter/`, and adapt them.
  - The adaptation is the rename (`sdk/go` import, the `tfw` alias, `tfwsim`, the
    `tfw` certificate subjects), protocol 2 (the fake CAPS header claims 2.0, the
    major-refusal test covers 1 and 3) and the `uint32` `FreeRAM`.
  - `xkms/go.mod` requires go-xkms `v0.51.0-alpha` and replaces the SDK with
    `../sdk/go`. `xkms/register.go` marks where the go-xkms#1439 registration
    goes and implements nothing.
  - The end-to-end test enrols the real firmware compiled for the host
    (`tfwsim`, `tfwsim-atecc608a`), not a binary somebody downloaded.
- **`make xkms-test` and `make xkms-coverage`** (#28), their own targets and not
  part of `make ci`: `make docker-ci` runs `make ci` in a container with no route
  to the private go-xkms module.
- **An `xkms` CI job** (#28), joined to the `CI Gate`, which runs both targets. It
  runs on the runner rather than in the toolchain image, because the image cannot
  reach the private go-xkms module and a credential must not be baked into a
  layer.

### Changed

- **The CI image's Go toolchain is 1.27.2** (#28), was 1.26.5, because go-xkms
  requires Go 1.27.1 or later. `GOTOOLCHAIN` stays `local`, and the `xkms` job
  pins the same version.
