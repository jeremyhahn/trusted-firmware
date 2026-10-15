# AVR Device Backend — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Feature: `xkms-adapter` · Last verified: 2026-10-15
> Conflicts with this document are defects. Fix them here first.

No CLI surface. The backend is reachable only through the Go API: it is
constructed around a link the caller has already opened, and `xkmsctl` has no
verb that opens a serial port.

The backend registers with go-xkms as type `tfw` when a program imports package
`xkms` (see `api.md`), and go-xkms never imports this module, so
`xkmsctl backends list` will not show it. Whether a daemon links the module is
that daemon's decision: there is no configuration yet that says which port a
device is on.

The device's own vendor CLI, `tfwctl`, ships with `trusted-firmware` and is the
tool for talking to a part directly.
