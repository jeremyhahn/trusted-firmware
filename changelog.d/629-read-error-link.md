### Fixed

- **`Device.Do` reports a read error that is not a timeout, such as a closed port, as a `*LinkError` at once** (row `host-sdk`): it no longer spins on the failing read until its deadline and returns a `*TimeoutError`.
