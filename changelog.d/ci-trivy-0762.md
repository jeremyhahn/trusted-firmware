### Changed

- **trivy is pinned at v0.76.2** (was v0.76.1), with both release-archive digests from upstream's checksums file, and `go.yaml.in/yaml/v3` in `sdk/go` moves to v3.0.6, so `make deps-check` has nothing behind without a reason.
