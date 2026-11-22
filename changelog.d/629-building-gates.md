### Fixed

- **`docs/development/building.md` re-verified against `mk/stages.mk` and the boards** (#629): the gate table names every prerequisite of `build`, `test`, `integration-test-host`, `security-novuln` and `vuln`, with `go-govulncheck` under `vuln` in scans.yml; the Firmware section names every board directory, each AVR board's tiers and the three architectures; the CI paragraph names ci.yml's jobs and moves deps-check and trivy image to scans.yml.
