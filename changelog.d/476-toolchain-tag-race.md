### Fixed

- **`make docker-build-toolchain` checks the image on the tag it owns** (#476): the `cat /etc/ci-toolchain.txt` check runs on `$(TOOLCHAIN_IMAGE)-$(IMAGE_BUILD_ID)` before that build-id tag is removed, not on the shared class tag `$(TOOLCHAIN_IMAGE)` after it, which another CI leg or a disk prune on the shared runner daemon could remove in between (PR #471's Security job failed at `mk/docker.mk:186`).
