### Fixed

- **The SDK's dice verifier checks the alias certificate through the DeviceID certificate to the roots** (#608), so an expired, not-yet-valid or unknown-critical-extension alias is refused.
