### Changed

- **The xkms adapter registers the AVR backend with go-xkms as `tfw`** (#28).
  `xkms/go.mod` requires go-xkms `v0.53.0-alpha`, which carries the
  external-backend hook (go-xkms#1439 step 1). Importing
  `github.com/jeremyhahn/trusted-firmware/xkms`, blank if nothing else is needed,
  makes `tfw` a backend type that `AutoInitialize` constructs from
  `AutoConfig.BackendConfigs["tfw"]`. That entry carries `device` (the attached
  `*tfw.Device`, required) and `config` (an `*avr.Config`, optional).
  - **Breaking: Key IDs beginning `avr:` no longer resolve to this backend.** The
    backend's `Type()` and its keys' store type are now `tfw`, so its Key IDs
    read `tfw:...`. go-xkms keeps `avr` for its own built-in backend until
    go-xkms#1439 step 3, and a backend type is held once per process. A stored
    `avr:` Key ID that meant this device has to be rewritten as `tfw:`. The
    adapter does not migrate it.
  - go-xkms `v0.53.0-alpha` still requires `arduino-generic/sdk/go` for that
    built-in backend, so it stays in `xkms/go.mod` as an indirect requirement
    until step 3.
