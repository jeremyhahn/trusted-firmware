### Changed

- **Three toolchain-image pins move to current upstream releases** (#1045) in `scripts/tool-pins.sh`: `gh` v2.103.3 -> v2.105.0 (both linux SHA-256 lines from upstream's checksums file), `golang.org/x/tools` for gosec v0.52.0 -> v0.53.0, and the `golang:1.27.4-alpine3.24` index digest `sha256:0a5f7fe7…` -> `sha256:70bd7dde…` (same tag, re-pushed upstream).
