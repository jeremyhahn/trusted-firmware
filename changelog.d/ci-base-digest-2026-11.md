### Changed

- **The devcontainer base image is pinned at the current `debian:trixie-slim` digest** (`a72b4433de1c`, was `f6b08b09a057`), and its hold is gone: the AVR toolchain comes from `apt-get install` at image build time, unpinned, so its version follows Debian's archive on the build day, not the base digest. The PR's CI image build runs the size budgets and vectors against it.
