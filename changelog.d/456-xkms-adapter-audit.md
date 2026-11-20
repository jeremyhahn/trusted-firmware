### Changed

- **`xkms-adapter` requirement audit** (#456): `docs/xkms-adapter/engineering.md` gains a requirements table (each line with its code and test) and six remaining slices, and its module pins now read go-xkms `v0.64.2-alpha` and `sdk/go` `v0.4.2-alpha`, as `xkms/go.mod` does.
- **`xkms-adapter` slices 2 and 4** (#456): the `enrollOnce` comment names `TestSoftRootEnrollsThroughDeviceAttest01`, and `xkms/avr/boundary_test.go` fails on any `net` or serial import in the package's non-test files, pinning "reaches the wire only through `sdk/go`".
