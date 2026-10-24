### Fixed

- **The CI images are rebuilt for libbrotli1 CVE-2026-111497**: a dated `DEBIAN_REFRESH` build arg in the devcontainer `base` stage moves the image hashes and the apt layer's cache key, so the `apt-get upgrade` runs again and installs 1.1.0-2+deb13u1, and `trivy image` passes on main again.
