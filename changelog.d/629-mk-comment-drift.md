### Fixed

- **Seven make comments now say what their rules do** (#629): a Cortex-M QEMU run covers four cases (plus hsm-* on an HSM=1 image), the tone, PULSE and 1-Wire rows run in the chain's order, `CYCLE_TEST` lists every test `qemu_wire_test.sh` runs, only `docker-build-toolchain`, `-esp32` and `-optee` label and tag through `scripts/docker-tag.sh`, and `bench-key` keeps a key only at mode 600 or 400.
