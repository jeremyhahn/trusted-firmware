### Changed

- **`make xkms-security` reads govulncheck's findings against `scripts/govulncheck-allow.txt`**: a vulnerability reachable from `xkms/` fails unless the list names it with a reason, and an entry nothing reaches is reported. GO-2026-6911 (mcp-go v1.5.0, no fixed version) is the one entry until xkms takes go-qrdb v0.58.2-alpha.

### Fixed

- **The schedule-guard workflow test finds the guarded jobs by their `needs: schedule-guard`**, not by a bigdisk runner, which no job names since the hosted runners.
