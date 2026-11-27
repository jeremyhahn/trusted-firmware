### Changed

- **The `debian:trixie-slim` base image pin moves to the index digest the tag names now** (#1045, #1044): `sha256:a72b4433…` -> `sha256:8aa5a7bf…` (same tag, re-pushed upstream) in `scripts/tool-pins.sh` and the devcontainer Dockerfile's `ARG BASE_DIGEST` default.
- **The govulncheck allowlist drops GO-2026-6911** (#1045, #1044), which nothing in `xkms/` reaches any more.
