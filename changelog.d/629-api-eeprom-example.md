### Fixed

- **`docs/host-sdk/api.md`'s error-handling example compiles** (#629): `EEPROMWrite` returns one value, so the example is `err := dev.EEPROMWrite(...)`; the page names the package `tfw` (not `ag`) and points at its own [Exported symbols](docs/host-sdk/api.md#exported-symbols) for signatures, not `engineering.md`.
