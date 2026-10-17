### Fixed

- **The Devcontainer image job meets its timeout on a cold runner.** It ran
  every image build and stage in series and, with a cold BuildKit cache (run
  55218: 55 minutes to build the three images), missed its 90 minutes in
  `esp32-qemu-test`. It is now three jobs that start at once, each building
  only the images it runs: `image-toolchain` (`make build`, `make test`),
  `image-security` (`make security`, `make deps-check`, `trivy image`) and
  `image-esp32` (`make esp32-build`, `make esp32-qemu-test`), with timeouts
  of 80, 85 and 90 minutes. CI Gate needs all three. `make
  docker-build-toolchain` and `make docker-build-esp32` build one image each;
  `make docker-build` still builds all three. Every stage runs with the same
  flags.
